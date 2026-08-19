# WiFi OTA 可执行方案

面向本仓库（ESP32-S3、ESP-IDF v5.1.2、16MB Flash、SoftAP `ESP32-Morse` + 可选 APSTA）。  
**当前固件尚未实现 OTA**；本文是按本工程现状可直接落地的方案：改什么、怎么触发、如何防半砖、踩坑怎么处理。

相关文档：[工程结构.md](工程结构.md)、[调试编译烧录.md](调试编译烧录.md)。

---

## 0. 现状与目标

| 项 | 现状 | OTA 后 |
|----|------|--------|
| 分区表 | `partitions_4mb.csv`：仅 `factory` 4MB | `otadata` + `ota_0`/`ota_1` 各 4MB |
| 升级方式 | 仅 USB `idf.py flash` | USB 首次 + 之后 WiFi |
| 防失败 | 无 | 双槽 + 镜像校验 + **App Rollback 必开** + USB 兜底 |
| 网络 | SoftAP `192.168.4.1`；可选 STA 进局域网（MQTT） | 同左；网页上传 / 服务器下发 |

**原则：**

1. 改分区表后，**第一次必须 USB 全量烧录**（bootloader + 分区表 + app）。
2. 之后日常只 `idf.py build`，用 WiFi 推送 `build/esp32s3_morse.bin`（应用镜像，不是整片 flash）。
3. 两路触发并存：**网页上传**（连热点即可）+ **服务器触发**（香橙派 HTTP/MQTT）。
4. **防变砖为必做项**（见 §1.2、§5），不是可选项。

---

## 1. 如何配置、升级源文件

### 1.1 分区表（必改）

把 `partitions_4mb.csv` 换成双 OTA 槽（16MB Flash，应用区仍按现工程 4MB）：

```csv
# Name,   Type, SubType, Offset,  Size, Flags
# 16MB Flash; dual OTA slots 4MB each (matches current factory size).
nvs,      data, nvs,     ,        0x6000,
phy_init, data, phy,     ,        0x1000,
otadata,  data, ota,     ,        0x2000,
ota_0,    app,  ota_0,   ,        4M,
ota_1,    app,  ota_1,   ,        4M,
```

`sdkconfig` 已是 `CONFIG_PARTITION_TABLE_CUSTOM=y` 且指向该文件，一般不用改路径。

**不要**用 menuconfig 默认的「Two OTA partitions」模板：应用区约 1.75MB，开 Tank 时容易不够。本方案自定义 4M+4M。

双槽含义：正在运行的槽只读；OTA 永远写到**另一块**。写坏/校验失败时，只要不切换启动分区，旧固件仍可跑。

### 1.2 防变砖配置（必开，与实现绑定）

本方案对「升级失败变砖」采用四层防御。实现时**全部落地**，不得把 rollback 当成可选项跳过。

#### 层 1：双槽隔离（分区表）

- 新镜像只写入 `esp_ota_get_next_update_partition()` 返回的空闲槽。
- 传输中断、断电：未完成的槽无效，当前启动槽不变 → 复位后仍是旧固件。

#### 层 2：镜像校验（写 Flash API）

严格顺序（**禁止**在 `esp_ota_end` 失败后仍 `set_boot_partition`）：

```text
esp_ota_get_next_update_partition()
  → esp_ota_begin()
  → 循环 esp_ota_write()
  → esp_ota_end()                 // 失败则 abort，不切换启动分区
  → esp_ota_set_boot_partition()  // 仅 end 成功后
  → esp_restart()
```

`esp_ota_end` 会检查镜像魔数、芯片类型、完整性等。故意传坏文件时必须：**旧固件继续运行**。

#### 层 3：App Rollback（menuconfig 必开）

```bash
idf.py menuconfig
# Bootloader config → Enable app rollback support
# → CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y
```

行为简述：

1. OTA 成功并重启后，新固件处于 **pending verify**（待验证）。
2. 应用必须在「关键能力已起来」之后调用：

   ```c
   esp_ota_mark_app_valid_cancel_rollback();
   ```

   建议封装为 `ota_mark_valid()`，调用时机：**SoftAP（及可选 STA）已 init，且 `morse_http_start()` 成功之后**。  
   **不要**一进 `app_main` 就 mark——那样崩溃固件也会被当成有效，rollback 形同虚设。
3. 若新固件在 mark 之前崩溃/看门狗复位，bootloader 判定验证失败 → **自动回滚到上一槽**。

当前仓库 `sdkconfig` 里该项为未启用；落地 OTA 时必须打开并随代码一并验证。

#### 层 4：USB 救砖（最终兜底）

分区表/bootloader 仍可通过下载口覆盖时：

```bash
. "$HOME/esp/v5.1.2/esp-idf/export.sh"
idf.py -p /dev/cu.wchusbserial10 flash
```

能挡住 / 挡不住见 [§5](#5-防变砖说明能恢复与不能恢复)。

### 1.3 新增组件 `components/ota/`（建议结构）

```
components/ota/
├── CMakeLists.txt
├── include/ota_update.h
└── ota_update.c
```

**`CMakeLists.txt` 依赖：**

```cmake
idf_component_register(
    SRCS "ota_update.c"
    INCLUDE_DIRS "include"
    REQUIRES app_update esp_http_client esp_https_ota nvs_flash
)
```

**对外接口建议（实现时按此写）：**

| 函数 | 作用 |
|------|------|
| `ota_from_http_stream(...)` | 网页 POST 原始 `.bin`：边收边 `esp_ota_write`；失败则 `esp_ota_abort` |
| `ota_from_url(const char *url)` | 从服务器 URL 拉包（`esp_https_ota` / HTTP） |
| `ota_mark_valid(void)` | 关键路径就绪后取消回滚（**必调**；内部即 `esp_ota_mark_app_valid_cancel_rollback`） |

从 URL 拉取时（局域网 HTTP）还需在 menuconfig：

```text
Component config → ESP HTTPS OTA
  → Allow HTTP for OTA (CONFIG_OTA_ALLOW_HTTP=y)
```

仅内网调试可开；上公网务必 HTTPS + 证书校验。

### 1.4 网页与 HTTP API（挂到现有 `web`）

在 `components/web/morse_http.c`：

1. 首页增加「固件升级」卡片 → 页面 `/ota`（显示当前 `esp_app_desc` 版本）。
2. 注册 `POST /api/ota`：`Content-Type: application/octet-stream`，body 为整份 `.bin`（避免 multipart 边界解析）。
3. 注册 `POST /api/ota_url`：body `{"url":"http://192.168.101.73:8080/esp32s3_morse.bin"}`，内部调 `ota_from_url`。
4. 加大接收超时（固件 1～2MB，热点上传可能几十秒）：

```c
config.recv_wait_timeout = 60;   // 秒
config.max_uri_handlers = 28;    // 现有 24，预留新路由
```

`components/web/CMakeLists.txt` 增加对 `ota` 的 `REQUIRES`。

`main/main.c` 启动顺序建议：

```text
nvs →（外设）→ wifi_softap_init → morse_http_start → ota_mark_valid()
```

### 1.5 服务器触发所需配置（与现 MQTT 共用 STA）

`app_config.h` 已有：

- `MQTT_STA_WIFI_SSID` / `MQTT_STA_WIFI_PASS`：STA 进局域网  
- `MQTT_BROKER_URI`：如 `mqtt://192.168.101.73:1883`

OTA 建议再增加（实现时写入 `app_config.h`）：

```c
/** 默认固件 URL（香橙派静态文件）；可被 /api/ota_url 或 MQTT 覆盖 */
#define OTA_DEFAULT_URL   "http://192.168.101.73:8080/esp32s3_morse.bin"
/** MQTT：发布到此 topic 触发按 URL 升级，payload 为 URL 或空（用默认 URL） */
#define OTA_MQTT_TOPIC    "esp32s3/ota"
/** 可选：简易口令，请求头 X-OTA-Token / MQTT 首行约定 */
#define OTA_TOKEN         "change-me"
```

服务器触发依赖 **APSTA 已拿到局域网 IP**（与 MQTT 相同前提）。仅连 SoftAP、未配 STA 时，只能用网页上传。

### 1.6 menuconfig 学习开关（可选）

在 `main/Kconfig.projbuild` 增加：

```
config DEMO_OTA
    bool "WiFi OTA 升级页"
    default y
```

关掉则不编 OTA 页与 API，便于对照体积。默认建议开。  
注意：关掉 `DEMO_OTA` **不等于**关掉 rollback；只要仍走 OTA 升级路径，rollback 配置应保持开启。

### 1.7 首次与日常烧录命令

**首次（改分区表 / 新 OTA 代码落地）：必须 USB 全量**

```bash
cd "/Users/zhouguan/Desktop/esp32/ESP32-S3"
. "$HOME/esp/v5.1.2/esp-idf/export.sh"
idf.py -p /dev/cu.wchusbserial10 build flash monitor
```

确认：

- 分区含 `ota_0` / `ota_1` / `otadata`
- 日志可见 rollback 相关配置已生效（或 `sdkconfig` 中 `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`）
- HTTP 起来，热点可连；启动末尾会调用 `ota_mark_valid`（首次 USB 烧录的固件同样建议 mark，避免状态异常）

**日常（仅改业务代码）：**

```bash
idf.py build
# 产物：build/esp32s3_morse.bin
# 再用下面「触发升级」任一路推送，无需 USB
```

**不要** OTA `bootloader.bin` / `partition-table.bin` / 整片合并镜像；只推 **app** 的 `esp32s3_morse.bin`。

### 1.8 香橙派侧准备升级源文件

在香橙派（示例 IP `192.168.101.73`）提供静态下载：

```bash
# 把 Mac 上编好的 bin 拷到派上，例如：
scp build/esp32s3_morse.bin orangepi@192.168.101.73:~/esp32s3/ota/

# 香橙派上起 HTTP（任选一种）
cd ~/esp32s3/ota
python3 -m http.server 8080
# 则 URL = http://192.168.101.73:8080/esp32s3_morse.bin
```

也可放 Nginx / Node-RED 静态目录，只要板子 STA 能 GET 到该 URL。

建议在派上保留上一份已知好的 `.bin`（如 `esp32s3_morse.prev.bin`），远程升挂后可用 USB 或（若 SoftAP 仍可用）网页回滚到旧包。

---

## 2. 如何触发升级

### 2.1 方式 A：Web 网页触发（连 SoftAP）

适用：手机/电脑连 `ESP32-Morse`，**不依赖路由器**。

1. 手机连热点 `ESP32-Morse`（开放热点时无密码）。
2. 浏览器打开 `http://192.168.4.1/ota`（或首页卡片）。
3. 选择本机上的 `esp32s3_morse.bin`，点「开始升级」。
4. 前端用 `fetch` **直接 POST 二进制**（推荐）：

```javascript
const file = input.files[0];
const buf = await file.arrayBuffer();
const r = await fetch('/api/ota', {
  method: 'POST',
  headers: {
    'Content-Type': 'application/octet-stream',
    // 'X-OTA-Token': 'change-me',  // 若已实现口令
  },
  body: buf,
});
// 成功后板子会重启；热点会短暂断开，几秒后再连
```

5. 串口（若仍接 USB）可看到 OTA 进度与重启；灯/网页恢复后即新固件；确认版本号已变。

**网页「按 URL 升级」**（板子已 STA 进局域网时）：

```bash
curl -X POST http://192.168.4.1/api/ota_url \
  -H 'Content-Type: application/json' \
  -d '{"url":"http://192.168.101.73:8080/esp32s3_morse.bin"}'
```

注意：手机连 SoftAP 时，手机浏览器访问的是 `192.168.4.1`；真正去拉文件的是 **ESP 的 STA**，不是手机。

### 2.2 方式 B：服务器触发（香橙派）

前提：`MQTT_STA_WIFI_SSID` 非空，板子已 APSTA 拿到局域网 IP；香橙派已托管 `.bin`。

#### B1. HTTP：服务器「通知板子去拉」

板子提供局域网可达的 API（STA IP，串口日志里有；也可在网页显示）。  
香橙派执行：

```bash
# 把板子 STA IP 换成实际值，例如 192.168.101.50
curl -X POST http://192.168.101.50/api/ota_url \
  -H 'Content-Type: application/json' \
  -d '{"url":"http://192.168.101.73:8080/esp32s3_morse.bin"}'
```

或固定用默认 URL：

```bash
curl -X POST http://192.168.101.50/api/ota_url \
  -H 'Content-Type: application/json' \
  -d '{}'
```

**如何发现板子 IP：** 看串口 `STA 已获得 IPv4`；或路由器 DHCP 列表；或后续在固件里用 mDNS（`esp32-morse.local`）。

#### B2. MQTT：发布即升级（推荐与现有 Mosquitto 统一）

实现约定：

- Topic：`esp32s3/ota`（与 `OTA_MQTT_TOPIC` 一致）
- Payload：完整 URL，或空字符串 / `default` → 使用 `OTA_DEFAULT_URL`
- QoS：1；板端订阅后调用 `ota_from_url`，成功则重启

香橙派触发示例：

```bash
# 空 payload = 默认 URL
mosquitto_pub -h 127.0.0.1 -t 'esp32s3/ota' -m '' -q 1

# 指定本次包地址
mosquitto_pub -h 127.0.0.1 -t 'esp32s3/ota' -q 1 \
  -m 'http://192.168.101.73:8080/esp32s3_morse.bin'
```

Node-RED：`mqtt out` 节点同一 topic 即可一键升级。

固件侧注意：MQTT 回调里**不要直接长时间阻塞写 Flash**，应投递到独立 OTA 任务，避免拖垮 MQTT 客户端。

#### B3. 两种服务器触发对比

| | HTTP `/api/ota_url` | MQTT `esp32s3/ota` |
|--|---------------------|--------------------|
| 需要知道板子 IP | 是 | 否（经 broker） |
| 依赖 Mosquitto | 否 | 是 |
| 适合 | 脚本、临时调试 | 日常运维、多设备 |
| SoftAP-only | 可用 `192.168.4.1`，但拉外网 URL 仍需 STA | 必须 STA |

### 2.3 触发方式总览

```text
                    ┌─ 手机连 SoftAP ── POST /api/ota（上传 bin）
  新固件 .bin ──────┤
                    ├─ 任意端 ── POST /api/ota_url（板拉 URL）──┐
                    │                                            ├─► ota 写 Flash
                    └─ 香橙派 mosquitto_pub ── topic esp32s3/ota ┘     │
                                                                       ▼
                                                         end 成功 → 切槽 → 重启
                                                                       │
                                                         pending verify │
                                                                       ▼
                                              WiFi+HTTP 就绪 → mark_valid
                                              崩溃未 mark → bootloader 回滚旧槽
```

---

## 3. 升级过程可能遇到的问题与解决

| 现象 | 可能原因 | 处理 |
|------|----------|------|
| 改完分区仍无法 OTA / 启动异常 | 只烧了 app，分区表未更新 | USB 全量 `idf.py flash`（不要只用 `app-flash`） |
| `esp_ota_get_next_update_partition` 为 NULL | 仍是旧 `factory` 表 | 检查 `partitions_4mb.csv` 与烧录是否含 partition-table |
| `esp_ota_end` 失败 / invalid image | 推了错文件（合并镜像、截断、非本工程 bin） | 只用 `build/esp32s3_morse.bin`；核对大小；应保持旧固件可跑 |
| 上传中途 httpd 断开 | 超时、热点信号差、手机省电断 WiFi | 加大 `recv_wait_timeout`；靠近板子；关「WLAN 助手」类优化 |
| 写完重启仍是旧固件 | 未 `esp_ota_set_boot_partition`；或 `end` 失败却误以为成功 | 查 OTA 日志；仅 `end == OK` 再 set + restart |
| 新固件启动后反复重启，最终回到旧版 | rollback 生效：新固件崩溃且未 mark | **正常防砖行为**；修固件后再升。若误判：确认 mark 时机是否过晚/未调用 |
| 新固件「半活」：能跑但无 WiFi/HTTP | 已 mark 或未开 rollback；远程无法再升 | 见 §5；USB `flash` 救回 |
| 开了 rollback 却从不回滚 | 过早 `mark_app_valid`；或未真正启用配置 | mark 放在 HTTP 启动之后；确认 `sdkconfig` |
| STA 拉 URL 失败 | 未配 SSID、未拿到 IP、香橙派未监听、URL 错 | `wifi_sta_wait_for_ip`；派上 `curl` 自测；服务绑 `0.0.0.0` |
| `CONFIG_OTA_ALLOW_HTTP` 未开 | `esp_https_ota` 拒绝 `http://` | menuconfig 打开 Allow HTTP for OTA |
| 镜像大于 ota 分区 | 槽太小或固件暴涨 | 保持 4M 槽；关 Tank；`idf.py size` |
| SoftAP 上传时板子还在播摩斯/刷屏 | Flash 写入与业务抢资源 | 升级前停播放队列；OTA 期间可关灯效 |
| 谁都能升级 | 热点无密码，接口裸奔 | SoftAP 设 `WIFI_PASS`；`X-OTA-Token` |
| USB 口被占用无法救砖 | monitor 占串口 | `Ctrl+]` 退出后再 `flash` |
| 路径搬迁后编译怪 | 旧 `build/` 缓存 | `rm -rf build && idf.py reconfigure && idf.py build` |

**救砖最低路径：** USB 线 + `idf.py -p PORT flash` 刷回已知好固件。双槽 + rollback 搞不定时再走这条；分区表正确、下载口可用时，USB 几乎总能覆盖。

---

## 4. 其他

### 4.1 建议落地顺序（实现 checklist）

1. 改 `partitions_4mb.csv`；menuconfig **打开** `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`（及内网所需的 `CONFIG_OTA_ALLOW_HTTP`）。
2. USB 全量烧录，确认双槽启动正常。
3. 实现 `components/ota`（含 abort / 仅 end 成功才切槽）+ `POST /api/ota` 网页上传。
4. `main` 在 WiFi + HTTP 就绪后调用 `ota_mark_valid()`。
5. SoftAP 自测：好包升级成功；坏包不切槽；故意崩溃包验证 rollback。
6. 实现 `ota_from_url` + `POST /api/ota_url`；香橙派托管验证。
7. MQTT 订阅 `esp32s3/ota`，`mosquitto_pub` 验证。
8. 版本号展示、`DEMO_OTA`、口令与文档收尾。

### 4.2 版本号

在工程里维护 `PROJECT_VER`，OTA 页显示当前版本，避免「升完不知道有没有换」。

```c
#include "esp_app_desc.h"
const esp_app_desc_t *d = esp_app_get_description();
// d->version
```

### 4.3 安全（本学习工程最低要求）

- SoftAP 设密码（`wifi_softap.h` 的 `WIFI_PASS` ≥ 8 位）。
- `/api/ota*` 加固定 Header，例如 `X-OTA-Token: <秘密>`，与 MQTT 共用同一口令。
- 仅局域网 HTTP；不把未加密 OTA 暴露到公网。

### 4.4 与现有组件关系

| 组件 | 关系 |
|------|------|
| `wifi` | SoftAP 供网页上传；STA 供 URL/MQTT 触发 |
| `web` | 注册 `/ota`、`/api/ota`、`/api/ota_url` |
| `mqtt_lan` | 可扩展为常驻订阅，或独立 OTA 订阅任务 |
| `board` / `app_config.h` | `OTA_DEFAULT_URL`、`OTA_MQTT_TOPIC`、`OTA_TOKEN` |
| `ota` | 写槽、校验、mark_valid |
| USB / `idf.py` | 改分区、首次落地、最终救砖 |

### 4.5 明确不做（本方案范围外）

- 不通过 OTA 更新 bootloader / 分区表（改布局仍须 USB）。
- 不把 `managed_components` 或整仓当升级包；只升编译出的 app `.bin`。
- 不依赖公网 CDN（默认走香橙派局域网）。
- 不承诺「无 USB 永远可救」——见 §5。

### 4.6 验收标准

- [ ] USB 首次烧录后，分区存在 `ota_0`/`ota_1`/`otadata`。
- [ ] `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`。
- [ ] SoftAP 网页上传好包 → 重启 → 版本与功能正确 → 日志有 mark_valid。
- [ ] 故意传坏文件 → `esp_ota_end` 失败且**不**切换启动分区，旧固件仍可运行。
- [ ] （可选强化）故意升「启动即崩溃」包 → 复位后自动回到旧槽。
- [ ] 香橙派 + `POST /api/ota_url` 成功。
- [ ] `mosquitto_pub -t esp32s3/ota ...` 成功（若已实现 MQTT 触发）。
- [ ] USB 仍可随时 `flash` 覆盖。

---

## 5. 防变砖说明：能恢复与不能恢复

本方案防的是常见 **半砖**（坏包、传一半、新固件秒崩），不是绝对「永不砖」。

| 场景 | 预期结果 | 依赖 |
|------|----------|------|
| 传一半断电 / 连接断开 | 仍跑旧固件 | 双槽；未成功 `set_boot` |
| 坏镜像 / 截断 / 错文件 | `esp_ota_end` 失败，仍跑旧固件 | 层 2 校验 |
| 新固件启动后崩溃（未 mark） | 复位后回旧槽 | 层 3 rollback **已开** + 未过早 mark |
| 新固件能跑但 WiFi/HTTP 全挂，且已 mark | 远程无法再 OTA | **USB 救砖** |
| 误 OTA 了非 app 镜像但校验未拦住（极少） | 可能起不来 | USB；验收时用坏包回归 |
| 分区表/bootloader 被改坏 | 可能进不了正常 app | USB 全量 flash；OTA 流程本身不应写这些 |
| Flash 硬件损坏 / 严重供电异常 | 可能无法下载 | 硬件排查 |

**实现纪律（防砖 checklist）：**

1. 分区必须是双 OTA，禁止继续用单 `factory` 做 WiFi 升级。  
2. `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` 为发布默认。  
3. 仅 `esp_ota_end == ESP_OK` 后才 `set_boot_partition` + `restart`。  
4. `ota_mark_valid()` 放在 WiFi + HTTP（关键远程路径）成功之后。  
5. 开发机保留上一份已知好 `.bin` + USB 线。  
6. 验收必须包含「坏包不切槽」；建议包含「崩溃包回滚」。

---

## 附录：关键文件一览（实现时会动到）

| 文件 | 动作 |
|------|------|
| `partitions_4mb.csv` | 改为双 OTA |
| `components/ota/*` | 新建（写槽 / URL / mark_valid） |
| `components/web/morse_http.c` | 路由与页面 |
| `components/web/CMakeLists.txt` | `REQUIRES ota` |
| `components/board/include/app_config.h` | OTA URL / topic / token |
| `main/main.c` | HTTP 就绪后 `ota_mark_valid()` |
| `main/Kconfig.projbuild` | 可选 `DEMO_OTA` |
| `sdkconfig`（menuconfig） | **必开** rollback；内网开 `CONFIG_OTA_ALLOW_HTTP` |
| 香橙派 `~/esp32s3/ota/` + `http.server` / Mosquitto | 升级源与触发 |

实现完成后，把本文「建议接口」与真实 URI/topic 对齐更新即可。
