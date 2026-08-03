#ifndef _DISCOVERY_H
#define _DISCOVERY_H

#include <Arduino.h>
#include <WiFiUdp.h>

/** UDP 广播发现端口（与 CSPS-Web 项目保持一致） */
#define DISCOVERY_PORT 9999

/**
 * @brief 初始化 UDP 广播发现服务
 *
 * 在 DISCOVERY_PORT (9999) 上启动 UDP 监听。
 * 需在 WiFi 连接成功后调用。
 */
void discovery_init();

/**
 * @brief 处理 UDP 广播发现请求
 *
 * 收到任意 UDP 数据包后，回复包含设备名称、IP 和 MAC 的 JSON。
 * 需在主循环中频繁调用。
 */
void discovery_handle();

#endif
