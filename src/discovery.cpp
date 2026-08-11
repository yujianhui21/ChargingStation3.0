#include <discovery.h>
#include <WiFi.h>

static WiFiUDP udpDiscovery;  // 独立于 NTP 的 UDP 实例
static bool udp_started = false;  // UDP 监听是否已启动

void discovery_init()
{
    if (udp_started)
    {
        return;  // 已启动，避免重复绑定端口
    }

    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println(F("[发现] WiFi 未连接，跳过初始化"));
        return;
    }

    udpDiscovery.begin(DISCOVERY_PORT);
    udp_started = true;
    Serial.printf("[发现] UDP 广播已启动 (端口 %d)\n", DISCOVERY_PORT);
}

void discovery_handle()
{
    if (!udp_started)
    {
        return;  // 未启动（WiFi 未连接），避免对未绑定 socket 调用 parsePacket
    }

    // 解析所有待处理的 UDP 数据包
    while (udpDiscovery.parsePacket())
    {
        char buf[256];
        int len = udpDiscovery.read(buf, sizeof(buf) - 1);
        if (len > 0)
        {
            buf[len] = '\0';
        }
        else
        {
            continue;
        }

        // 收到任意广播即回复设备信息
        String ip = WiFi.localIP().toString();
        String mac = WiFi.macAddress();
        String json = "{\"type\":\"ChargingStation\",\"name\":\"多协议桌面充电站3.0\",\"ip\":\"" + ip
                    + "\",\"mac\":\"" + mac + "\"}";

        udpDiscovery.beginPacket(udpDiscovery.remoteIP(), udpDiscovery.remotePort());
        udpDiscovery.print(json);
        udpDiscovery.endPacket();

        Serial.printf("[发现] 回复 %s → %s\n",
                      json.c_str(),
                      udpDiscovery.remoteIP().toString().c_str());
    }
}
