#include <ota_service.h>

void ota_init()
{
    // 设置 OTA 主机名（与 mDNS 主机名一致）
    ArduinoOTA.setHostname("charging-station");

    // 可选：设置 OTA 密码（取消注释以启用）
    // ArduinoOTA.setPassword("admin");

    // 升级开始回调
    ArduinoOTA.onStart([]()
    {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH)
        {
            type = "固件";
        }
        else  // U_SPIFFS
        {
            type = "文件系统";
        }
        Serial.println("[OTA] 开始更新 " + type);
    });

    // 升级完成回调
    ArduinoOTA.onEnd([]()
    {
        Serial.println(F("[OTA] 更新完成"));
    });

    // 升级进度回调
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
    {
        Serial.printf("[OTA] 进度: %u%%\r", progress / (total / 100));
    });

    // 升级错误回调
    ArduinoOTA.onError([](ota_error_t error)
    {
        Serial.printf("[OTA] 错误[%u]: ", error);
        if (error == OTA_AUTH_ERROR)
        {
            Serial.println(F("认证失败"));
        }
        else if (error == OTA_BEGIN_ERROR)
        {
            Serial.println(F("启动失败"));
        }
        else if (error == OTA_CONNECT_ERROR)
        {
            Serial.println(F("连接失败"));
        }
        else if (error == OTA_RECEIVE_ERROR)
        {
            Serial.println(F("接收失败"));
        }
        else if (error == OTA_END_ERROR)
        {
            Serial.println(F("结束失败"));
        }
    });

    ArduinoOTA.begin();
    Serial.println(F("[OTA] 已就绪"));
    Serial.printf("[OTA] 上传命令: pio run --target upload --upload-port %s\n",
                  WiFi.localIP().toString().c_str());
}

void ota_handle()
{
    ArduinoOTA.handle();
}
