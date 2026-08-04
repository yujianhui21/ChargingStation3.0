#include <main.h>

void setup()
{
    #ifdef ENABLE_DEBUG
    Serial.begin( 115200 ); /* prepare for possible serial debug */
    #endif

    gpio_init(); /* Initialize the GPIO */
    flash_init(); /* Initialize the flash */
    load_web_config(); /* Load the web configuration */

    display_init(); /* Initialize the display and UI */
    ina219_init(); /* Initialize the INA219 sensor */
    ina3221_init(); /* Initialize the INA3221 sensor */
    adc_init(); /* Initialize the ADC */    
    key_init(); /* Initialize the encoder input device */
    RGB_init(); /* Initialize the RGB LED */
    lvgl_group_init(); /* Initialize the LVGL group */

    wificonnect(); /* Connect to the wifi (带超时；同时初始化WiFi/TCP-IP栈，供后续Web服务使用) */
    time_server_init(NTPServer.c_str(), TimeZone, SyncTime); /* Initialize the time server */
    weather_init(qWeather_Key, CityCode, qWeather_ApiHost); /* Initialize the weather server */

    setupWebSocket();  // 初始化WebSocket
    setupWebServer();  // 初始化Web服务器

    // 初始化网络服务（init 幂等，WiFi 未连接时跳过，配网成功后自动补启动）
    mdns_service_init();  // mDNS: charging-station.local
    discovery_init();     // UDP 广播发现 (端口 9999)
    ota_init();           // ArduinoOTA 固件升级

    // 初始化所有开关
    setTypeC32Output(USBC32_Switch);
    setTypeC1Output(USBC1_Switch);
    setUSBAOutput(USBA_Switch);
    setFan(fan_switch);
    setFanTempControl(tempcontrol_fan);
    setChargeLed(led_enabled);
}

void loop()
{
    display_task(); /* let the GUI do its work */
    web_server_run(); /* let the web server do its work */
    time_server_update();
    wificonfig();
    mdns_service_update();        /* mDNS 周期维护 */
    discovery_handle();   /* UDP 广播发现处理 */
    ota_handle();         /* OTA 升级处理 */
}
