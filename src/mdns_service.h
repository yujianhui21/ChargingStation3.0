#ifndef _MDNS_SERVICE_H
#define _MDNS_SERVICE_H

#include <Arduino.h>
#include <ESPmDNS.h>

/**
 * @brief 初始化 mDNS 服务
 *
 * 注册主机名为 charging-station，使设备可通过
 * http://charging-station.local 直接访问。
 * 需在 WiFi 连接成功后调用。
 */
void mdns_service_init();

/**
 * @brief mDNS 周期性维护
 *
 * 处理 mDNS 查询请求，需在主循环中频繁调用。
 */
void mdns_service_update();

#endif
