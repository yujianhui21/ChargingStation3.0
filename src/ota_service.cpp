#include <ota_service.h>

#include <WiFi.h>
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <lvgl.h>
#include "ui/ui.h"

// ui_font_OTA 由 src/ui/fonts/ui_font_OTA.c 定义（未在生成的 ui.h 中声明）
#ifdef __cplusplus
extern "C" {
#endif
LV_FONT_DECLARE(ui_font_OTA);
#ifdef __cplusplus
}
#endif

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

// ===== OTA 屏幕显示 =====
static const uint32_t OTA_UI_PUMP_MS = 20;        // LVGL 刷新泵间隔(ms)
static const uint32_t OTA_UI_UPDATE_MS = 100;     // 进度条/百分比刷新间隔(ms)
static const uint32_t OTA_UI_HIDE_MS = 15000;     // 会话结束后无新连接则自动隐藏覆盖层(ms)

static const char *OTA_PREF_NS = "ota";
static const char *OTA_KEY_MD5 = "md5";
static const char *OTA_KEY_OFF = "off";

static WiFiServer otaServer(OTA_PORT);
static Preferences otaPrefs;
static bool started = false;

// ===== OTA 全屏覆盖层控件（位于所有屏幕之上，升级时提示用户） =====
static lv_obj_t *ota_ui_panel = NULL;         // 覆盖层根容器
static lv_obj_t *ota_ui_title = NULL;         // 状态标题（中文）
static lv_obj_t *ota_ui_bar = NULL;           // 进度条
static lv_obj_t *ota_ui_pct = NULL;           // 百分比文字
static lv_obj_t *ota_ui_hint = NULL;          // 底部提示
static unsigned long ota_ui_idle_start = 0;   // 会话结束时刻；0 表示无需自动隐藏

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

// ===== 显示全屏覆盖层（已存在则仅更新标题） =====
static void otaUiShow(const char *title)
{
    if (ota_ui_panel != NULL)
    {
        lv_label_set_text(ota_ui_title, title);
        return;
    }

    lv_obj_t *panel = lv_obj_create(lv_layer_top());   // 顶层图层：盖在所有屏幕之上
    ota_ui_panel = panel;
    Serial.println(F("[OTA] 屏幕显示升级界面"));
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, lv_disp_get_hor_res(lv_disp_get_default()),
                    lv_disp_get_ver_res(lv_disp_get_default()));
    lv_obj_set_pos(panel, 0, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x101010), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(panel, LV_OPA_90, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);   // 拦截触屏/按键，避免误操作底层控件

    ota_ui_title = lv_label_create(panel);
    lv_obj_set_style_text_font(ota_ui_title, &ui_font_OTA, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ota_ui_title, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(ota_ui_title, title);
    lv_obj_align(ota_ui_title, LV_ALIGN_TOP_MID, 0, 26);

    ota_ui_bar = lv_bar_create(panel);
    lv_obj_set_size(ota_ui_bar, 200, 12);
    lv_obj_set_style_radius(ota_ui_bar, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ota_ui_bar, lv_color_hex(0x333333), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ota_ui_bar, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ota_ui_bar, 6, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ota_ui_bar, lv_color_hex(0x00C853), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ota_ui_bar, 255, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_bar_set_range(ota_ui_bar, 0, 100);
    lv_bar_set_value(ota_ui_bar, 0, LV_ANIM_OFF);
    lv_obj_align(ota_ui_bar, LV_ALIGN_CENTER, 0, 0);

    ota_ui_pct = lv_label_create(panel);
    lv_obj_set_style_text_font(ota_ui_pct, &ui_font_ASCII32MONO, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ota_ui_pct, lv_color_hex(0x00FF88), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(ota_ui_pct, "0%");
    lv_obj_align(ota_ui_pct, LV_ALIGN_CENTER, 0, 44);

    ota_ui_hint = lv_label_create(panel);
    lv_obj_set_style_text_font(ota_ui_hint, &ui_font_OTA, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ota_ui_hint, lv_color_hex(0xAAAAAA), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(ota_ui_hint, "请勿断电");
    lv_obj_align(ota_ui_hint, LV_ALIGN_BOTTOM_MID, 0, -14);
}

// ===== 更新状态标题 =====
static void otaUiSetState(const char *title)
{
    if (ota_ui_panel != NULL)
    {
        lv_label_set_text(ota_ui_title, title);
    }
}

// ===== 更新进度条与百分比 =====
static void otaUiSetProgress(uint32_t written, uint32_t total)
{
    if (ota_ui_panel == NULL || total == 0)
    {
        return;
    }
    uint8_t pct = (uint8_t)((uint64_t)written * 100 / total);
    lv_bar_set_value(ota_ui_bar, pct, LV_ANIM_OFF);
    lv_label_set_text_fmt(ota_ui_pct, "%d%%", pct);
}

// ===== 隐藏并销毁覆盖层 =====
static void otaUiHide(void)
{
    if (ota_ui_panel != NULL)
    {
        lv_obj_del(ota_ui_panel);
        ota_ui_panel = NULL;
        ota_ui_title = NULL;
        ota_ui_bar = NULL;
        ota_ui_pct = NULL;
        ota_ui_hint = NULL;
        Serial.println(F("[OTA] 屏幕隐藏升级界面"));
    }
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

    // 请求合法：显示全屏升级提示，取消自动隐藏计时
    otaUiShow("正在升级固件");
    ota_ui_idle_start = 0;
    lv_task_handler();   // 立即渲染，擦除分区期间界面保持显示

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
        otaUiSetState("升级失败");
        ota_ui_idle_start = millis();
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
            otaUiSetState("升级失败");
            ota_ui_idle_start = millis();
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
    unsigned long lastLvPump = 0;    // LVGL 刷新节流
    unsigned long lastUiUpdate = 0;  // 进度刷新节流
    static uint8_t buf[OTA_WRITE_CHUNK];

    while (written < totalSize)
    {
        if (!client.connected())
        {
            saveProgress(written, reqMd5);
            Serial.printf("[OTA] 连接断开，已保存进度 %u/%u\n", written, totalSize);
            otaUiSetState("连接中断，正在重连");
            ota_ui_idle_start = millis();
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
                if (millis() - lastLvPump >= OTA_UI_PUMP_MS)
                {
                    lv_task_handler();
                    lastLvPump = millis();
                }
            }
            avail = client.available();
            if (avail == 0)
            {
                saveProgress(written, reqMd5);
                Serial.printf("[OTA] 接收空闲超时，已保存进度 %u/%u\n", written, totalSize);
                otaUiSetState("连接中断，正在重连");
                ota_ui_idle_start = millis();
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
            otaUiSetState("升级失败");
            ota_ui_idle_start = millis();
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

        // 更新屏幕进度并泵 LVGL（限频，保证传输期间界面实时刷新）
        if (millis() - lastUiUpdate >= OTA_UI_UPDATE_MS)
        {
            otaUiSetProgress(written, totalSize);
            lastUiUpdate = millis();
        }
        if (millis() - lastLvPump >= OTA_UI_PUMP_MS)
        {
            lv_task_handler();
            lastLvPump = millis();
        }

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
        otaUiSetState("升级失败");
        ota_ui_idle_start = millis();
        client.println("E set-boot-failed");
        return;
    }

    otaPrefs.remove(OTA_KEY_OFF);
    otaPrefs.remove(OTA_KEY_MD5);
    otaUiSetState("升级完成，正在重启");
    client.println("OK");
    Serial.println(F("[OTA] 更新完成，即将重启"));
    lv_task_handler();   // 刷新一次，让"升级完成"先显示出来
    delay(300);
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

    // 会话已结束且长时间无新连接：隐藏覆盖层，恢复原界面
    if (ota_ui_panel != NULL && ota_ui_idle_start != 0
        && millis() - ota_ui_idle_start >= OTA_UI_HIDE_MS)
    {
        otaUiHide();
        ota_ui_idle_start = 0;
    }

    WiFiClient client = otaServer.available();
    if (client)
    {
        handleClient(client);
    }
}
