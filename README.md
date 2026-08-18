| Supported Targets | ESP32 | ESP32-C2 | ESP32-C3 | ESP32-C6 | ESP32-H2 | ESP32-S2 | ESP32-S3 |
| ----------------- | ----- | -------- | -------- | -------- | -------- | -------- | -------- |

# Hello World Example（SoftAP + 网页摩斯控制 WS2812）

本工程在 ESP-IDF `hello_world` 基础上，实现 **手机连板子热点 → 浏览器控制板载 WS2812**：

- **Wi‑Fi SoftAP**：SSID `ESP32-Morse`，**开放热点、无密码**（可在 `components/wifi/include/wifi_softap.h` 中改 `WIFI_SSID`；若要加密，将 `WIFI_PASS` 设为至少 8 位）
- **内置网页**：短按大按钮 = 点（·），长按（≥280ms）= 划（—）；另有「字母间 /」「单词间 //」「退格」「清空」「发送播放」
- **灯效**：常态 **黄灯**；**每输入一个点/划**会请求 `POST /api/preview`，板子 **点红灯、划绿灯** 闪一下（与整段播放互斥）；点「发送播放」后整段 **点红划绿** 按摩斯播放，结束回黄灯
- **无源蜂鸣器（可选）**：`app_config.h` 中 `MORSE_BUZZER_GPIO`（默认 **GPIO4**）接三脚模块的 **I/O**，**VCC→3V3**、**GND→GND**；用 **LEDC PWM** 发声，与点/划亮灯时间同步。低电平导通模块设 `MORSE_BUZZER_ACTIVE_LOW 1`，否则试 `0`。不用蜂鸣器将 `MORSE_BUZZER_GPIO` 设为 **-1**
- **TFT（SPI）**：小板丝印多为 **1.69 TFT V1.1**（与 v1.4 同类），驱动 **ST7789V3**，**240(RGB)×280**。首页 **「屏幕 Hello」** → **`/lcd`**，点按钮 **`POST /api/lcd_hello`**（`body` 可为 `{}`）刷新画面，首次请求时初始化 SPI。引脚见 `components/board/include/app_config.h` 的 `LCD_PIN_*`，勿与灯 **GPIO48**、蜂鸣器 **GPIO4** 冲突。若 **只有背光**：先查接线与共地。1.69 / 240×280 在 ST7789 的 **320 行**里常垂直居中，**行偏移应为 (320−280)/2=20**（工程里默认 **`LCD_GAP_Y 20`**）。另可参考 Adafruit 通用 ST7789 初始化打开 **反相（`LCD_INVERT_COLOR 1`）**。仍不行再试 **`LCD_SPI_MODE`**（0/3）、**`LCD_RGB565_LITTLE_ENDIAN`**（0/1）、**`LCD_ST7789_BGR`**、**`LCD_SPI_HOST`**（SPI2/SPI3）、**`LCD_SWAP_XY` / `LCD_MIRROR_*`**。固件已默认开启 **`LCD_ST7789_LVGL_EXTRA_INIT`**（补发 LVGL 常用电源/伽马）；若异常可改 **0** 关闭。硬件上务必核对 **SDA=数据线(MOSI)、SCL=时钟**，**勿与 DC 对调**。
- **全屏图片（Tank）**：首页 **「全屏 Tank」** → **`/tank`**，点按钮调用 **`POST /api/lcd_tank`**，在 ST7789 全屏显示 `assets/tank.png`（离线转成 `components/lcd/tank_rgb565.c`）。换图可运行 `python3 tools/gen_tank_rgb565.py` 重新生成数组。
- **多页面**：`/` 为功能卡片；`/music` 为简谱 1～7 与《两只老虎》按钮；**`POST /api/tone`**，`body`：`{"note":1}`～`{"note":7}`；**`POST /api/play_twotigers`**（可 body `{}`）。均与摩斯共用蜂鸣器互斥；旋律节奏见 `morse_buzzer.c` 中 `MELODY_NOTE_MS` / `MELODY_GAP_MS`
- **解码**：`POST /api/decode` 根据当前摩斯串 **实时** 返回 `decoded`（输入点/划、`/`、`//`、退格后自动刷新）；`POST /api/play` 仍会整段播放并返回解码结果

> 板载 WS2812 数据脚为 **GPIO48**（与 `components/board/include/app_config.h` 中 `MORSE_LED_GPIO` 一致）。

**1.69 / ST7789V3（240×280）与 ESP32-S3 接线（与 `app_config.h` 中 `LCD_PIN_*` 一致）**

卖家「引脚说明」表顺序（排针 **1→8**）：

| 序号 | 丝印 | 接开发板 | 说明 |
|:----:|------|----------|------|
| 1 | GND | **GND** | 必须与 ESP32 共地 |
| 2 | VCC | **3.3V** | 表注为 **3.3V 正极**；勿误接 5V |
| 3 | SCL | **GPIO12** | SPI 时钟 CLK |
| 4 | SDA | **GPIO11** | SPI MOSI |
| 5 | RES | **GPIO14** | 复位 |
| 6 | DC | **GPIO13** | 数据 / 命令 |
| 7 | CS | **GPIO10** | 片选 |
| 8 | BLK | **3.3V 或悬空** | **默认开背光**；**低电平关背光**。若改 GPIO 控背光须保持高电平才亮 |

手机连上热点后，浏览器打开 **`http://192.168.4.1/`** 进入**功能卡片页**；**摩斯**在 **`/morse`**，**七声简谱**在 **`/music`**，**屏幕 Hello** 在 **`/lcd`**，**全屏图片**在 **`/tank`**（ESP 默认网关多为 `192.168.4.1`；若你改过 DHCP 以串口日志为准）。

### 1.69" ST7789V3 本项目已验证参数（推荐基线）

> 以下是当前工程里已验证「方向正确 + 图片可辨识 + 红蓝正常」的一组参数，建议作为初始值：

- `LCD_PIXEL_CLOCK_HZ = 10MHz`
- `LCD_GAP_Y = 20`
- `LCD_SPI_MODE = 3`
- `LCD_RGB565_LITTLE_ENDIAN = 1`
- `LCD_ST7789_BGR = 0`
- `LCD_INVERT_COLOR = 0`
- `LCD_SWAP_XY = 1`
- `LCD_MIRROR_X = 0`，`LCD_MIRROR_Y = 1`

说明：
- 图片显示链路（`/tank`）在 `lcd_st7789.c` 中对像素做了软件补偿（含反相兜底），用于抵消部分模组在颜色链路上的差异。
- 如果你后续换屏或改了方向宏，可能需要重新微调 `LCD_ST7789_BGR` 与 `LCD_RGB565_LITTLE_ENDIAN`。

**摩斯串约定（与解码一致）**

- 同一单词内多个字母：字母的摩斯之间用 **单个 `/` 分隔**（例：`.../---/...` = SOS）
- 单词与单词之间：用 **`//` 分隔**（例：`.../---/...//.../---/...` = 两个 SOS）

(See the README.md file in the upper level 'examples' directory for more information about examples.)

## How to use example

### 运行步骤（macOS + ESP-IDF v5.1.2）

1) 进入工程目录并加载 ESP-IDF 环境（每个新终端都要做一次）

```bash
cd "/Users/zhouguan/Desktop/esp32/ESP32-S3"
. "$HOME/esp/v5.1.2/esp-idf/export.sh"
```

2) 编译、烧录、查看日志

```bash
idf.py -p /dev/cu.wchusbserial10 build flash monitor
```

- 退出串口监视：在 `monitor` 界面按 **`Ctrl+]`**
- 只重新烧录：`idf.py -p /dev/cu.wchusbserial10 flash`

3) 可选：调整热点名、摩斯速度/亮度

- 热点：`components/wifi/include/wifi_softap.h` 中 `WIFI_SSID`；`WIFI_PASS` 为空则无密码，设 ≥8 位则启用 WPA2
- 灯与时序：`components/board/include/app_config.h` 中 `MORSE_UNIT_MS`、`MORSE_LED_BRIGHT_PLAY`、`MORSE_LED_BRIGHT_IDLE` 等

### 注意事项（很重要）

- **串口占用**：如果烧录报 `Could not exclusively lock port` / `port is busy`，说明你还开着 `monitor` 或其它串口工具占用端口；先退出 `monitor`（`Ctrl+]`）再 `flash`。
- **Flash 大小**：该板通常是 **16MB Flash**。如果你曾见过 “检测到 16MB 但镜像头是 2MB” 的警告，去 `idf.py menuconfig` 里把 `Serial flasher config -> Flash size` 设为 **16MB**。
- **组件依赖（led_strip）**：本工程使用 `led_strip`（通过组件管理器下载到 `managed_components/`）。首次构建需要能访问组件仓库；若环境提示缓存目录问题，确保 `~/Library/Caches/Espressif/ComponentManager` 可创建。
- **GPIO48**：若换板不亮，修改 `app_config.h` 中的 `MORSE_LED_GPIO`。
- **打不开网页**：确认手机已连上 `ESP32-Morse`，关闭「仅使用 Wi‑Fi 时禁止 HTTP」类设置；本服务为 **明文 HTTP**，非 HTTPS。
- **431 Request Header Fields Too Large**：手机浏览器请求头（Cookie 等）可能很长。本工程已在 `sdkconfig` 将 `CONFIG_HTTPD_MAX_REQ_HDR_LEN` / `CONFIG_HTTPD_MAX_URI_LEN` 提到 **2048**；若仍出现可再在 `menuconfig → Component config → HTTP Server` 里加大。
- **`httpd` 栈溢出 / 发送播放死机**：`play` 响应拼装曾占用过大栈空间；已在 `morse_http.c` 加大 `httpd` 任务栈（`stack_size=16384`）并把 JSON 缓冲改为静态区。若仍异常可把 `CONFIG_HTTPD_MAX_URI_LEN` 再加大或精简请求并发。
- **连续发送**：播放请求进队列（默认 4 条），队列满时接口返回 `queue full`。
- **图片偏色/像底片（`/tank`）**：优先看 `app_config.h` 的 `LCD_RGB565_LITTLE_ENDIAN` 与 `LCD_ST7789_BGR`。经验上：  
  - 轮廓对但像底片：先确认 `LCD_RGB565_LITTLE_ENDIAN`（本工程推荐 `1`）  
  - 红色显示成蓝色：切 `LCD_ST7789_BGR`（0/1 互换）  
  - 降 SPI 时钟无变化：通常不是信号线，属于颜色映射链路问题

Select the instructions depending on Espressif chip installed on your development board:

- [ESP32 Getting Started Guide](https://docs.espressif.com/projects/esp-idf/en/stable/get-started/index.html)
- [ESP32-S2 Getting Started Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s2/get-started/index.html)


## 工程目录

业务按 ESP-IDF 组件拆分；细节见 [docs/工程结构.md](docs/工程结构.md)。

```
├── CMakeLists.txt            project(esp32s3_morse)
├── main/main.c               入口：NVS → 灯 → player → Wi‑Fi → HTTP
├── components/
│   ├── board/                app_config + WS2812
│   ├── wifi/                 SoftAP / APSTA
│   ├── mqtt_lan/             局域网 MQTT 测试
│   ├── morse/                解码 + 灯效播放
│   ├── buzzer/               蜂鸣器 / 简谱
│   ├── lcd/                  ST7789 + tank 数组
│   └── web/                  HTTP 门户与 /api
├── assets/tank.png           全屏图源
├── tools/gen_tank_rgb565.py
└── docs/
```

## Troubleshooting

* Program upload failure

    * Hardware connection is not correct: run `idf.py -p PORT monitor`, and reboot your board to see if there are any output logs.
    * The baud rate for downloading is too high: lower your baud rate in the `menuconfig` menu, and try again.

* WS2812 不亮

    * 确认本工程 `MORSE_LED_GPIO`（`app_config.h`）是否与你的板子一致（默认 GPIO48）
    * 降低 `MORSE_LED_BRIGHT_*`（过亮有时会让你误判“没亮/颜色不明显”）

## Technical support and feedback

Please use the following feedback channels:

* For technical queries, go to the [esp32.com](https://esp32.com/) forum
* For a feature request or bug report, create a [GitHub issue](https://github.com/espressif/esp-idf/issues)

We will get back to you as soon as possible.
