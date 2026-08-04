#include <ota_service.h>

#include <WiFi.h>
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

// ===================== 断点续传 OTA =====================
// 协议（TCP 3232，由电脑端主动连接）：
//   电脑 → 设备: "S <固件总字节数> <固件MD5>\n"   请求开始 / 查询进度
//   设备 → 电脑: "O <已写入偏移>\n"               从该偏移续传（全新上传为 0）
//   电脑 → 设备: 固件二进制数据
//   设备 → 电脑: "A <已写入偏移>\n"               每写完一块确认一次
//   设备 → 电脑: "OK\n" 或 "E <错误信息>\n"       传输结束
//
// 已写入偏移 + 固件 MD5 保存在 NVS。断线后电脑以相同 MD5 重新请求，
// 设备返回上次偏移，双方从该处继续。用 esp_partition_write 按绝对偏移
// 写入，天然支持任意位置续传（即使设备在传输中被重启也能续）。

static const uint16_t OTA_PORT = 3232;
static const uint32_t OTA_WRITE_CHUNK = 4096;         // 每次读取+写入块大小
static const uint32_t OTA_NVS_SAVE_INTERVAL = 16384;  // 每写 16KB 存一次进度到 NVS
static const uint32_t OTA_RECV_IDLE_TIMEOUT = 8000;   // 接收空闲超时(ms)，超时视为断线

static const char *OTA_PREF_NS = "ota";
static const char *OTA_KEY_MD5 = "md5";
static const char *OTA_KEY_OFF = "off";

static WiFiServer otaServer(OTA_PORT);
static Preferences otaPrefs;
static bool started = false;

// ===== 读取一行（直到 '\n'），带超时 =====
static String readLine(WiFiClient &client, uint32_t timeoutMs)
{
    String line;
    unsigned long start = millis();
    while (millis() - start < timeoutMs)
    {
        while (client.available())
        {
            char c = client.read();
            if (c == '\n')
            {
                return line;
            }
            if (c != '\r')
            {
                line += c;
            }
        }
        delay(1);
    }
    return line;
}

// ===== 保存续传进度到 NVS =====
static void saveProgress(uint32_t offset, const String &md5)
{
    otaPrefs.putUInt(OTA_KEY_OFF, offset);
    otaPrefs.putString(OTA_KEY_MD5, md5);
}

// ===== 处理单个客户端连接（阻塞式，直到传输完成或断线） =====
static void handleClient(WiFiClient &client)
{
    // 1. 读取控制帧 "S <size> <md5>"
    String line = readLine(client, 3000);
    if (line.length() == 0 || !line.startsWith("S "))
    {
        Serial.printf("[OTA] 非法请求: '%s'\n", line.c_str());
        client.println("E bad-request");
        return;
    }

    int sp1 = line.indexOf(' ');            // "S" 与 size 之间的空格
    int sp2 = line.indexOf(' ', sp1 + 1);   // size 与 md5 之间的空格
    if (sp1 < 0 || sp2 < 0)
    {
        client.println("E bad-request");
        return;
    }
    String sizeStr = line.substring(sp1 + 1, sp2);
    String reqMd5 = line.substring(sp2 + 1);
    reqMd5.trim();
    uint32_t totalSize = (uint32_t)strtoul(sizeStr.c_str(), NULL, 10);
    if (reqMd5.length() != 32 || totalSize == 0)
    {
        client.println("E bad-request");
        return;
    }

    // 2. 决定起始偏移：MD5 相同且已有进度则续传，否则全新开始
    String storedMd5 = otaPrefs.getString(OTA_KEY_MD5, "");
    uint32_t storedOff = otaPrefs.getUInt(OTA_KEY_OFF, 0);
    uint32_t offset = 0;
    if (storedMd5 == reqMd5 && storedOff > 0 && storedOff < totalSize)
    {
        offset = storedOff;  // 续传
    }

    // 3. 目标分区 = 当前未运行的那个 OTA 分区
    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    if (target == NULL)
    {
        Serial.println(F("[OTA] 找不到 OTA 分区"));
        client.println("E no-partition");
        return;
    }

    if (offset == 0)
    {
        // 全新上传：先整分区擦除
        esp_err_t err = esp_partition_erase_range(target, 0, target->size);
        if (err != ESP_OK)
        {
            Serial.printf("[OTA] 擦除分区失败: %s\n", esp_err_to_name(err));
            client.println("E erase-failed");
            return;
        }
        Serial.printf("[OTA] 已擦除分区 %s，全新写入\n", target->label);
    }
    else
    {
        Serial.printf("[OTA] 从偏移 %u (%u%%) 续传\n", offset, offset * 100 / totalSize);
    }

    // 4. 回复起始偏移
    client.printf("O %u\n", offset);

    // 5. 接收并写入固件
    uint32_t written = offset;
    uint32_t lastNvsSave = offset;
    static uint8_t buf[OTA_WRITE_CHUNK];

    while (written < totalSize)
    {
        if (!client.connected())
        {
            saveProgress(written, reqMd5);
            Serial.printf("[OTA] 连接断开，已保存进度 %u/%u\n", written, totalSize);
            return;
        }

        size_t avail = client.available();
        if (avail == 0)
        {
            // 空闲等待；超时视为断线（由电脑侧重连续传，不影响已写入数据）
            unsigned long idleStart = millis();
            while (client.available() == 0 && client.connected()
                   && millis() - idleStart < OTA_RECV_IDLE_TIMEOUT)
            {
                delay(1);
            }
            avail = client.available();
            if (avail == 0)
            {
                saveProgress(written, reqMd5);
                Serial.printf("[OTA] 接收空闲超时，已保存进度 %u/%u\n", written, totalSize);
                return;
            }
        }

        if (avail > OTA_WRITE_CHUNK)
        {
            avail = OTA_WRITE_CHUNK;
        }
        // 不越过固件总长度
        if (written + avail > totalSize)
        {
            avail = totalSize - written;
        }

        int r = client.read(buf, avail);
        if (r <= 0)
        {
            continue;
        }

        esp_err_t err = esp_partition_write(target, written, buf, r);
        if (err != ESP_OK)
        {
            Serial.printf("[OTA] 写入失败: %s\n", esp_err_to_name(err));
            client.println("E write-failed");
            saveProgress(written, reqMd5);
            return;
        }

        written += r;
        if (written - lastNvsSave >= OTA_NVS_SAVE_INTERVAL)
        {
            saveProgress(written, reqMd5);
            lastNvsSave = written;
        }
        client.printf("A %u\n", written);  // 每块确认

        // 进度打印（每 ~512KB）
        if (written % (512 * 1024) < OTA_WRITE_CHUNK)
        {
            Serial.printf("[OTA] 进度: %u%% (%u/%u)\n", written * 100 / totalSize, written, totalSize);
        }
    }

    // 6. 传输完成：切换启动分区并重启
    esp_err_t err = esp_ota_set_boot_partition(target);
    if (err != ESP_OK)
    {
        Serial.printf("[OTA] 设置启动分区失败: %s\n", esp_err_to_name(err));
        client.println("E set-boot-failed");
        return;
    }

    otaPrefs.remove(OTA_KEY_OFF);
    otaPrefs.remove(OTA_KEY_MD5);
    client.println("OK");
    Serial.println(F("[OTA] 更新完成，即将重启"));
    delay(100);
    ESP.restart();
}

void ota_init()
{
    if (started)
    {
        return;  // 已启动，避免重复注册
    }

    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println(F("[OTA] WiFi 未连接，跳过初始化"));
        return;
    }

    otaPrefs.begin(OTA_PREF_NS, false);
    otaServer.begin(OTA_PORT);
    started = true;

    Serial.printf("[OTA] 断点续传服务已就绪 (TCP %u)\n", OTA_PORT);
    Serial.printf("[OTA] 上传命令: python scripts/espota_resume.py -i %s -f firmware.bin\n",
                  WiFi.localIP().toString().c_str());
}

void ota_handle()
{
    if (!started)
    {
        return;
    }

    WiFiClient client = otaServer.available();
    if (client)
    {
        handleClient(client);
    }
}
