#ifndef _OTA_SERVICE_H
#define _OTA_SERVICE_H

#include <Arduino.h>
#include <ArduinoOTA.h>

/**
 * @brief 初始化 ArduinoOTA 固件升级服务
 *
 * 设置 OTA 主机名、密码（可选）和进度回调。
 * 需在 WiFi 连接成功后调用。
 *
 * 上传方式：
 *   pio run --target upload --upload-port <设备IP>
 */
void ota_init();

/**
 * @brief 处理 OTA 升级请求
 *
 * 需在主循环中频繁调用以响应 OTA 升级。
 */
void ota_handle();

#endif
