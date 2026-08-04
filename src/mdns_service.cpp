#include <mdns_service.h>
#include <WiFi.h>

void mdns_service_init()
{
    static bool started = false;
    if (started)
    {
        return;  // 已启动，避免重复注册
    }

    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println(F("[mDNS] WiFi 未连接，跳过初始化"));
        return;
    }

    if (MDNS.begin("charging-station"))
    {
        MDNS.addService("http", "tcp", 80);
        started = true;
        Serial.println(F("[mDNS] charging-station.local 已就绪"));
        Serial.println(F("[mDNS] HTTP 服务已注册 (端口 80)"));
    }
    else
    {
        Serial.println(F("[mDNS] 启动失败"));
    }
}

void mdns_service_update()
{
    // MDNS.update() 是空操作（ESP32 核心在后台处理 mDNS），
    // 保留此接口以便将来扩展
}
