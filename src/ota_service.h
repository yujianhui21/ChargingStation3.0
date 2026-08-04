#ifndef _OTA_SERVICE_H
#define _OTA_SERVICE_H

#include <Arduino.h>

/**
 * @brief 初始化 OTA 断点续传升级服务
 *
 * 在 TCP 端口 3232 上监听。与标准 ArduinoOTA 不同，该实现支持
 * 断点续传：WiFi 信号不稳导致传输中断时，已写入的偏移量会保存到 NVS，
 * 电脑端重新连接后从该偏移继续写入，直到完整传完。
 *
 * 上传方式：
 *   python scripts/espota_resume.py -i <设备IP> -f firmware.bin
 *
 * 需在 WiFi 连接成功后调用（幂等，可重复调用）。
 */
void ota_init();

/**
 * @brief 处理 OTA 升级请求
 *
 * 需在主循环中频繁调用以响应 OTA 升级。
 */
void ota_handle();

#endif
