# ChargingStation3.0 — 多协议桌面充电站 V3.0

桌面充电站 3.0 固件源码及 UI 文件仓库。基于 ESP32-S3，配 240x240 TFT 显示屏，支持 LVGL 图形界面、多协议充电输出控制、功率监测、WiFi 网页控制与天气显示。

**项目发布地址：** https://oshwhub.com/ikalyes/duo-xie-yi-zhuo-mian-chong-dian-zhan-3-0

Copyright ©️ iKalys 2025
License — CERN-OHL-P v2

---

## 硬件规格

| 项目 | 参数 |
|---|---|
| MCU | ESP32-S3 (ESP32-S3-DevKitC-1) |
| 框架 | Arduino (PlatformIO) |
| 显示屏 | 240×240 TFT (TFT_eSPI 驱动) |
| GUI | LVGL 8.3.11 + SquareLine Studio 1.5.1 |
| 分区表 | max_app_8MB.csv (8MB app 分区) |
| Flash | 8MB |

## GPIO 引脚定义

| GPIO | 功能 |
|---|---|
| 1 | ADC0 — USB-A1 电压检测 |
| 2 | ADC1 — USB-A2 电压检测 |
| 3 | ADC2 — NTC 温度检测 |
| 4 | INA3221 SCL (I2C0 / `Wire`) |
| 5 | INA3221 SDA (I2C0 / `Wire`) |
| 6 | INA219 SDA (I2C1 / `Wire1`) |
| 7 | INA219 SCL (I2C1 / `Wire1`) |
| 15 | USB-C 3.2 开关控制 |
| 16 | USB-C 1 开关控制 |
| 39 | 按键 LEFT |
| 40 | 按键 ENTER |
| 41 | 按键 RIGHT |
| 42 | NeoPixel RGB LED 数据 |
| 47 | 风扇控制 (LEDC ch1 PWM) |
| 48 | USB-A 开关控制 |

---

## 构建与开发

### 环境要求

- [PlatformIO](https://platformio.org/) (VS Code 插件 或 CLI)
- Python 3.x

### 编译 & 烧录

```bash
# 编译固件
pio run

# USB 串口烧录
pio run --target upload --upload-port <COM端口>

# OTA 无线烧录（设备需先通过 USB 烧录一次基础固件）
pio run --target upload --upload-port <设备IP地址>

# 串口监视 (115200 baud)
pio device monitor
```

### 生成 BIN 固件文件

执行 `pio run --target export_bins` 可同时编译固件并导出两种发布用 BIN 文件到 `dist/` 目录：

```bash
pio run --target export_bins
```

| 产物 | 说明 | 用途 |
|---|---|---|
| `dist/ChargingStation3.0-ota.bin` | 仅应用固件 (firmware.bin) | OTA 升级、网页刷写 |
| `dist/ChargingStation3.0-factory.bin` | 合并完整镜像 (bootloader + partitions + boot_app0 + firmware) | USB 首次烧录、工厂批量烧录 |

**使用合并镜像烧录（乐鑫 Flash Download Tool / esptool.py）：**

```powershell
esptool.py --chip esp32s3 --port COM3 --baud 921600 write_flash 0x0 dist/ChargingStation3.0-factory.bin
```

`scripts/export_bins.py` 通过 `platformio.ini` 中的 `extra_scripts` 注册为自定义 target，固件有改动时 `export_bins` 会自动先重新编译再导出，无需手动复制。

### 调试

在 `src/main.h` 中通过 `#define ENABLE_DEBUG 1` 控制调试输出，默认开启。

---

## 项目架构

```
ChargingStation3.0/
├── platformio.ini            # PlatformIO 项目配置
├── src/                      # 固件源码
│   ├── main.cpp/h            # 入口：setup() 初始化 + loop() 主循环
│   ├── display.cpp/h         # TFT 显示 + LVGL 初始化、背光、旋转
│   ├── gpio.cpp/h            # GPIO 引脚定义 + 开关控制 + 风扇 PWM 温控
│   ├── adc.cpp/h             # ADC 电压/温度检测 (3通道, NTC, 滑动平均)
│   ├── ina219.cpp/h          # INA219 主电源电流检测 (I2C1/Wire1)
│   ├── ina3221_service.cpp/h # INA3221 3通道电流检测 (I2C0/Wire) + RGB LED
│   ├── key.cpp/h             # 3按键输入 (LEFT/ENTER/RIGHT), LVGL indev
│   ├── lvgl_group.cpp/h      # LVGL 组/屏幕导航管理
│   ├── lvgl_event.cpp/h      # UI 事件处理器 (ui_event_* 回调实现)
│   ├── flash.cpp/h           # NVS (Preferences) 持久化存储
│   ├── variables.cpp/h       # 全局状态变量
│   ├── rgbled.cpp/h          # NeoPixel RGB LED (3颗, 功率→色相呼吸)
│   ├── wifi_service.cpp/h    # WiFi 连接 + WiFiManager 配网门户
│   ├── main_service.cpp/h    # NTP 时间同步 + 天气刷新 (LVGL timer)
│   ├── weather.cpp/h         # 和风天气 API 客户端 (JSON + gzip)
│   ├── web_server.cpp/h      # AsyncWebServer + WebSocket + 嵌入式仪表盘
│   ├── mdns_service.cpp/h    # mDNS 服务 (局域网域名访问)
│   ├── discovery.cpp/h       # UDP 广播发现 (设备自动发现)
│   ├── ota_service.cpp/h     # ArduinoOTA 固件无线升级
│   └── ui/                   # SquareLine Studio 生成的 LVGL UI
│       ├── ui.c/h            # UI 主入口
│       ├── ui_events.c/h     # UI 事件桩 (实现在 lvgl_event.cpp)
│       ├── ui_helpers.c/h    # UI 辅助函数
│       ├── screens/          # 各屏幕实现
│       │   ├── ui_MainScreen.c         # 主仪表盘
│       │   ├── ui_SettingScreen.c       # 设置
│       │   ├── ui_AdvancedSettingScreen.c # 高级设置
│       │   ├── ui_WiFiScreen.c          # WiFi 配置
│       │   └── ui_WeatherScreen.c       # 天气显示
│       ├── fonts/            # 字体文件 (ASCII 16/20/32/40/56, 中文)
│       └── images/           # 图片资源 (天气图标等)
├── UI/                       # SquareLine Studio 工程文件
│   ├── ChargerStation.sll    # SquareLine 工程
│   ├── ChargerStation.spj    # 工程设置
│   └── assets/               # 字体源文件 (TTF + 编译产物)
├── Firmware/                 # 历史固件发布包
└── docs/                     # 文档
```

### 数据流

```
传感器 (INA219, INA3221, ADC)
    │  LVGL timers 定时读取
    ▼
全局变量 (variables.h/cpp)
    │
    ├──► LVGL UI 标签更新 (lv_label_set_text)
    ├──► WebSocket → 浏览器仪表盘实时图表
    └──► RGB LED 颜色 (功率→色相呼吸效果)

用户输入 (LEFT/ENTER/RIGHT 按键)
    │
    ▼
LVGL 输入设备 (key.cpp) → LVGL 组导航 → 事件处理器 (lvgl_event.cpp)
    │
    ├──► GPIO 开关切换 (USB-C, USB-A, 风扇)
    ├──► NVS 设置持久化 (flash.cpp)
    ├──► 屏幕切换 (lvgl_group.cpp)
    └──► WiFiManager 配网触发
```

### 主循环 (loop)

```cpp
void loop()
{
    display_task();       // LVGL GUI 渲染
    web_server_run();     // WebSocket 传感器广播 (100ms 间隔)
    time_server_update(); // NTP 时间同步
    wificonfig();         // WiFi 重配置处理
    mdns_service_update();   // mDNS 周期维护
    discovery_handle();      // UDP 广播发现请求处理
    ota_handle();            // ArduinoOTA 固件升级处理
}
```

---

## 网络服务

### 1. Web 控制面板

设备启动并连接 WiFi 后，浏览器访问设备 IP 或 mDNS 域名即可打开控制面板。

- 实时功率图表（主电源 + Type-C×3 + USB-A×2 + 温度，100 点滚动窗口）
- 系统开关控制（Type-C 输出、USB-A 输出、风扇、温控、充电指示灯）
- 黑暗模式切换
- WebSocket 双向通信 (端口 80，路径 `/ws`)

**WebSocket 协议：**

```json
// 客户端 → 设备：切换开关
{"action":"switch","type":"typeC32","state":true}

// 设备 → 客户端：开关状态（连接时自动发送）
{"switches":{"typeC32":true,"typeC1":true,"usbA":true,"fan":true,"fanTemp":true,"chargeLed":true}}

// 设备 → 客户端：传感器数据（100ms 间隔）
{"time":"01:23","mainPower":{"voltage":12.0,"current":1.5,"power":18.0},"group1":{...},...}
```

### 2. mDNS 局域网域名

设备通过 mDNS 广播主机名，无需知道 IP 即可访问：

```
http://charging-station.local
```

支持系统：macOS (Bonjour 内置)、Linux (Avahi)、Windows 10+ (需安装 Bonjour 或使用 `ping charging-station.local`)

### 3. UDP 广播发现

设备在 UDP 端口 **9999** 上监听，收到任意数据包即回复设备信息。

**测试方法：**

```bash
# Linux/macOS
echo "PING" | ncat -u 255.255.255.255 9999

# Windows PowerShell
$sock = New-Object System.Net.Sockets.UdpClient
$sock.Connect("255.255.255.255", 9999)
$bytes = [Text.Encoding]::ASCII.GetBytes("PING")
$sock.Send($bytes, $bytes.Length)

# Python
import socket, json
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
sock.settimeout(3)
sock.sendto(b"PING", ("255.255.255.255", 9999))
data, addr = sock.recvfrom(1024)
print(json.loads(data))
sock.close()
```

**回复格式：**

```json
{"name":"多协议桌面充电站3.0","ip":"192.168.1.100","mac":"AA:BB:CC:DD:EE:FF"}
```

### 4. OTA 无线固件升级

```bash
# 编译并 OTA 上传
pio run --target upload --upload-port 192.168.1.100

# 串口监控升级进度
pio device monitor
# [OTA] 开始更新 固件
# [OTA] 进度: 50%
# [OTA] 更新完成
```

**注意：** 首次烧录需通过 USB 完成，之后即可通过 OTA 升级。OTA 默认端口 3232 (ArduinoOTA 标准端口)。

---

## WiFi 配网

首次启动或 WiFi 连接失败时，设备进入 AP 模式：

1. 手机/电脑连接 WiFi 热点 `ChargingStation`
2. 浏览器访问 `192.168.4.1`
3. 选择目标 WiFi 并输入密码
4. 可配置：城市代码、和风天气 API Key、NTP 服务器、时区、同步间隔

---

## 天气显示

使用和风天气 (QWeather) API，在 WeatherScreen 显示实时天气、温度、湿度。

配置项（通过 WiFiManager 配网页面设置）：
- `CityCode` — 城市代码 (默认 `101280101`)
- `qWeatherKey` — 和风天气 API Key
- `qWeatherApiHost` — API 主机 (免费用户 `devapi.qweather.com`)

---

## 依赖库

| 库 | 版本 | 用途 |
|---|---|---|
| lvgl | 8.3.11 | GUI 框架 |
| TFT_eSPI | ^2.5.43 | TFT 显示驱动 |
| Adafruit INA219 | ^1.2.3 | 电流传感器 |
| ArduinoJson | ^7.3.1 | JSON 解析 |
| NTPClient | ^3.2.1 | NTP 时间同步 |
| ArduinoUZlib | ^1.0.2 | Gzip 解压 (天气) |
| WiFiManager | ^2.0.17 | 配网门户 |
| INA3221 | ^0.4.1 | 3通道电流传感器 |
| ESPAsyncWebServer | ^3.7.6 | 异步 Web 服务 |
| AsyncTCP | ^3.3.8 | 异步 TCP |
| Adafruit NeoPixel | (bundled) | RGB LED |
| ESPmDNS | (built-in) | mDNS 服务 |
| ArduinoOTA | (built-in) | OTA 升级 |
| WiFiUdp | (built-in) | UDP 通信 |

---

## 固件发布

预编译固件位于 `Firmware/` 目录：

- `桌面充电站3.0固件 - 25.4.26.zip`
- `桌面充电站3.0固件 - 25.6.2.zip`
- `桌面充电站3.0固件 - 25.7.15 - 增加屏幕旋转，优化显示效果，删掉休眠功能，优化温控逻辑.zip`

---

## 许可证

CERN-OHL-P v2 — 任何形式的使用、研究、修改、共享和分发硬件设计及基于这些设计的产品请遵守开源协议。
