#include <wifi_service.h>

#include <mdns_service.h>
#include <discovery.h>
#include <ota_service.h>
#include <web_server.h>

WiFiManager wm;

// 自动连接超时 (ms)：setup 阶段等待 WiFi 连接的最长时间，超时继续启动系统
#define WIFI_CONNECT_TIMEOUT        15000UL
// 配网保存后连接超时 (ms)：门户保存 WiFi 后，等待连接成功的最长时间
#define WIFI_SAVE_CONNECT_TIMEOUT   20000UL

// 配网状态标志：用户在门户保存了 WiFi，等待主循环接管连接
static bool wifisaved_flag = false;

// 非阻塞模式下配置门户跨 loop 周期存活，WiFiManagerParameter 必须为全局/静态，
// 否则门户运行期间参数对象被销毁导致悬空指针崩溃
static WiFiManagerParameter custom_citycode("CityCode", "CityCode", "101280101", 9);
static WiFiManagerParameter custom_qweatherapihost("qWeatherApiHost", "QWeather API Host", "", 32);
static WiFiManagerParameter custom_qweatherkey("qWeatherKey", "QWeather User Key", "", 32);
static WiFiManagerParameter custom_ntpserver("NTPServer", "NTP Server", "pool.ntp.org", 32);
static WiFiManagerParameter custom_timezone("TimeZone", "Time Zone", "8", 2);
static WiFiManagerParameter custom_synctime("SyncTime", "NTP And Weather Update Time(Hour)", "1", 16);
static WiFiManagerParameter p_lineBreak_notext("<p></p>");

// 参数只注册一次，避免门户重复启动时 addParameter 累积重复项
static bool params_registered = false;

// ===== 配网成功后的收尾：更新 UI、同步时间天气、确保网络服务启动 =====
static void onWiFiConnected()
{
    Serial.printf("[WiFi] 连接成功: %s (%s)\n",
                  WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());

    lv_label_set_text(ui_WIFIStatus, "已连接");
    lv_label_set_text(ui_TextWIFIStart, "更新天气时间");
    lv_label_set_text(ui_SSID, WiFi.SSID().c_str());
    lv_label_set_text(ui_IPADDR, WiFi.localIP().toString().c_str());
    lv_img_set_src(ui_ImageWiFi, &ui_img_593743026);

    // 配网结束：重启 Web 服务器（配网门户期间已停止以释放端口 80）
    web_server_start();

    // 若 setup 阶段因未连接而跳过的网络服务，在此补启动（init 幂等）
    mdns_service_init();
    discovery_init();
    ota_init();

    // 时间/天气不在此处直接同步（NTPClient 可能尚未 init），
    // 由 loop() 中的 time_server_update() 在检测到连接后自动同步

    _ui_screen_change(&ui_MainScreen, LV_SCR_LOAD_ANIM_FADE_ON, 100, 0, &ui_MainScreen_screen_init);
    lvgl_group_to_main();
}

// ===== 非阻塞配置门户 =====
// 启动后立即返回，WiFiManager 请求由 wificonfig() 中的 wm.process() 非阻塞处理。
// 配网期间主循环不被阻塞，屏幕持续刷新。
void Webconfig()
{
    if (wm.getConfigPortalActive())
    {
        return;  // 门户已在运行
    }

    Serial.println(F("[WiFi] 启动非阻塞配网门户 (AP: ChargingStation)"));

    // 非阻塞模式：startConfigPortal 立即返回
    wm.setConfigPortalBlocking(false);
    // 3 分钟无客户端连接则门户自动关闭
    wm.setConfigPortalTimeout(180);
    wm.setConnectTimeout(10);
    // 保存后不阻塞连接，由项目主循环接管，避免 UI 卡死
    wm.setSaveConnect(false);

    // 清空旧凭据（不影响项目自身 NVS 中保存的 wifisetting）
    wm.resetSettings();

    // 停止项目自己的 Web 服务器，释放端口 80 给 WiFiManager 配置门户
    web_server_stop();

    // 参数注册（仅首次）
    if (!params_registered)
    {
        wm.addParameter(&custom_citycode);
        wm.addParameter(&custom_qweatherapihost);
        wm.addParameter(&custom_qweatherkey);
        wm.addParameter(&custom_ntpserver);
        wm.addParameter(&custom_timezone);
        wm.addParameter(&custom_synctime);
        wm.setSaveParamsCallback(saveParamCallback);

        std::vector<const char *> menu = {"wifi", "restart"};
        wm.setMenu(menu);
        wm.setMinimumSignalQuality(20);

        params_registered = true;
    }

    // AP + STA 双模式：配网期间若已保存过 WiFi 仍可自动回连
    WiFi.mode(WIFI_AP_STA);
    wm.startConfigPortal("ChargingStation");  // 非阻塞，立即返回
}

String getParam(String name)
{
    // read parameter from server, for customhmtl input
    String value;
    if (wm.server->hasArg(name))
    {
        value = wm.server->arg(name);
    }
    return value;
}

void saveParamCallback()
{
    // 读取 WiFi SSID/密码（WiFiManager 表单字段名: s / p）
    String ssid = getParam("s");
    String pwd = getParam("p");
    if (ssid.length() > 0)
    {
        memset(wifisetting.sta_ssid, 0, sizeof(wifisetting.sta_ssid));
        memset(wifisetting.sta_pwd, 0, sizeof(wifisetting.sta_pwd));
        strncpy(wifisetting.sta_ssid, ssid.c_str(), sizeof(wifisetting.sta_ssid) - 1);
        strncpy(wifisetting.sta_pwd, pwd.c_str(), sizeof(wifisetting.sta_pwd) - 1);
        save_wifi_config();
    }

    // 自定义参数
    CityCode = getParam("CityCode");
    qWeather_ApiHost = getParam("qWeatherApiHost");
    qWeather_Key = getParam("qWeatherKey");
    TimeZone = getParam("TimeZone").toInt();
    NTPServer = getParam("NTPServer");
    SyncTime = getParam("SyncTime").toFloat();

    save_web_config();
    time_server_setting(NTPServer.c_str(), TimeZone, SyncTime);
    weather_init(qWeather_Key, CityCode, qWeather_ApiHost);

    // 通知主循环接管 WiFi 连接（保存后不阻塞门户）
    wifisaved_flag = true;
    wificonfig_flag = false;
}

void wificonfig()
{
    // ===== 状态 1: 门户已保存 WiFi，等待连接接管 =====
    if (wifisaved_flag)
    {
        static unsigned long connect_start = 0;

        // 关闭配置门户
        if (wm.getConfigPortalActive())
        {
            wm.stopConfigPortal();
        }

        // 首次进入：发起连接
        if (connect_start == 0)
        {
            Serial.printf("[WiFi] 开始连接 %s ...\n", wifisetting.sta_ssid);
            WiFi.mode(WIFI_STA);
            WiFi.setAutoReconnect(true);
            WiFi.begin(wifisetting.sta_ssid, wifisetting.sta_pwd);
            connect_start = millis();
        }

        if (WiFi.status() == WL_CONNECTED)
        {
            onWiFiConnected();
            wifisaved_flag = false;
            connect_start = 0;
        }
        else if (millis() - connect_start > WIFI_SAVE_CONNECT_TIMEOUT)
        {
            Serial.println(F("[WiFi] 配网保存后连接超时"));
            lv_label_set_text(ui_WIFIStatus, "连接失败");
            lv_label_set_text(ui_TextWIFIStart, "重新配网");
            WiFi.disconnect();
            web_server_start();  // 恢复 Web 服务器
            wifisaved_flag = false;
            connect_start = 0;
        }
        else
        {
            delay(10);  // 等待连接
        }
        return;
    }

    // ===== 状态 2: 未请求配网，正常维护 =====
    if (wificonfig_flag == false)
    {
        // 意外残留的门户关闭
        if (wm.getConfigPortalActive())
        {
            wm.stopConfigPortal();
            web_server_start();  // 恢复 Web 服务器
        }
        return;
    }

    // ===== 状态 3: 请求配网，确保门户运行 =====
    if (!wm.getConfigPortalActive())
    {
        Webconfig();
        lv_label_set_text(ui_WIFIStatus, "请连接至热点");
        lv_label_set_text(ui_SSID, "ChargingStation");
        lv_label_set_text(ui_IPADDR, "192.168.4.1");
        return;
    }

    // ===== 状态 4: 门户运行中，非阻塞处理请求 =====
    wm.process();

    // 门户已自动关闭（超时）→ 复位配网请求，允许用户再次触发
    if (!wm.getConfigPortalActive())
    {
        Serial.println(F("[WiFi] 配网门户超时关闭"));
        web_server_start();  // 恢复 Web 服务器
        wificonfig_flag = false;
    }
}

void wificonnect()
{
    load_wifi_config();
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);

    // 无已保存配置：不阻塞，直接返回（用户可通过 UI 触发配网）
    if (strlen(wifisetting.sta_ssid) == 0)
    {
        Serial.println(F("[WiFi] 无已保存配置，跳过自动连接"));
        return;
    }

    Serial.printf("[WiFi] 连接 %s ...\n", wifisetting.sta_ssid);
    WiFi.begin(wifisetting.sta_ssid, wifisetting.sta_pwd);

    // 带超时等待，避免 setup() 被无限阻塞
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT)
    {
        delay(10);
        lv_task_handler();
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        onWiFiConnected();
    }
    else
    {
        Serial.printf("[WiFi] 连接 %s 超时，继续启动系统\n", wifisetting.sta_ssid);
    }
}

void wifireset()
{
    wm.resetSettings();
    delete_wifi_config();
    ESP.restart();
}
