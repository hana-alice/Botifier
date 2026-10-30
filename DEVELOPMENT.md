# StackChan 开发准备

更新：2026-10-30。已完整阅读 `STACKCHAN_HANDOFF.txt`，沿用 StackChan + ToF4M 方案。

## 已验证状态

| 项目 | 结果 |
| --- | --- |
| 本机 | Windows 11 x64，PowerShell 7.6.5；本次使用 Windows 原生工具链 |
| 初始目录 | 只有交接文件，无实现代码、构建产物或 Git 仓库 |
| 官方源码基线 | `StackChan/`，`f901cc2969189f3f0e96af1442c0f96e77e1e2c1`，2026-10-29；本地距离提醒改动另行提交 |
| 源码固件版本 | 1.6.0，见 `StackChan/firmware/CMakeLists.txt` |
| ESP-IDF | v5.5.4，commit `735507283d5b2f9fb363a1901172dbd9e847945d` |
| Python | IDF 使用已有 Python 3.12.10 创建的本地 venv；系统原默认是 3.14.3 |
| 工具目录 | `.tools/esp-idf-v5.5.4` 和 `.tools/espressif`，未修改系统级 PATH |
| 设备 | COM38，USB VID 303A / PID 1001；启动日志确认 StackChan 1.5.1 / ESP-IDF v5.5.4 |
| C++ 主机测试 | 原版运动数学 + 真实 ToF 驱动的模拟 I²C 测试，2/2 通过；驱动覆盖 11 组场景 |
| 电脑端测试 | 52 项通过；无硬件模拟产生 1 次提醒 |
| 当前固件构建 | 含距离采样，成功，`StackChan/firmware/build/stack-chan.bin`，3,734,928 bytes |
| 固件刷写 | 本次没有刷写；源码版本不代表设备运行版本 |
| 配网 | 用户确认更换为 AX300 后连接成功；热点排查结束，不再作为开发阻塞 |
| ToF4M | 用户确认已到货、尚未接线；已核对红色 PORT A 位于机头左侧 USB-C 下方 |

完整版本、依赖 commit 和固件 SHA256 见 [environment.json](environment.json)。
设备配网另见 [WIFI_DIAGNOSIS.md](WIFI_DIAGNOSIS.md)。

## 当前演示与退出问题

已准备两个使用原厂 DANCE 蓝牙接口的演示：表情灯光动作秀、电脑倒计时提醒。
入口和使用说明见 [demos/stackchan-ble/README.md](demos/stackchan-ble/README.md)。
Windows 编译通过，实际程序 dry-run 的 JSON、动作范围、结束姿态及非法参数检查通过。
尚未扫描、连接或向机器人发送演示命令，不代表设备 1.5.1 已验证可用。

用户随后报告：在机器人应用市场尝试 App 后全屏，上下滑无法退出；具体 App 名尚不明确。
用户已确认短按 RST 后恢复，开始距离提醒开发。
普通重启不等于恢复出厂，也不能保证下载 App 一定退出。

源码核对：`Hal::launchApp()` 走 OTA，下载项可能独立运行，不保证继承原厂 Home 控件。
原厂 Home 手势从显示区域最底部 20px 内开始、向上超过 50px，松手显示按钮后再点击。
本地当前分区表只有 ota_0/ota_1，无独立 factory 分区；未读取设备分区表或确认另一槽的内容，
不能据此直接改启动分区。未找到已启用的、可确认返回原厂主菜单的特殊物理按键组合。
这些是当前 1.6.0 源码结论，不把它当作机内当前 App 的已验证退出协议。
RST 短按重启、长按约 3 秒进入下载模式的操作依据为
[官方硬件说明](https://docs.m5stack.com/zh_CN/StackChan)。

## 开发入口

从 `F:\stackchan` 的 PowerShell 执行：

```powershell
# 激活当前 shell 的本地 ESP-IDF 环境
. .\scripts\Enter-DevEnv.ps1

# 构建当前固件，并保存日志；不刷写
.\scripts\Build-Firmware.ps1

# 运行运动数学与 ToF 驱动主机测试
.\scripts\Test-Host.ps1

# 运行电脑端测试与无通知模拟；不连接机器人
.\scripts\Test-DistanceReminder.ps1

# 演示电脑提醒，弹出明确标记为“模拟”的静音通知
.\scripts\Start-DistanceReminder.ps1 -Simulate

# 只读检查电脑热点；明确区分配置频段和驱动回报的信道
.\scripts\Get-HotspotStatus.ps1

# 捕获设备日志；过滤含凭据字段的日志行，不向串口发送命令
python .\scripts\Capture-DeviceLog.py --port COM38 --seconds 120 --log .\logs\device.log
```

串口打开可能引发 USB_UART_CHIP_RESET。本次首次用 .NET 打开串口时捕获过该复位；
后续 pyserial 设 DTR=true、RTS=false 的采集窗口未观察到新复位。不能据此保证任何设备或驱动下都不会复位。
串口号以再次枚举结果为准。

`Test-Host.ps1` 使用现有 VS2026 自带 CMake 4.3.1/MSVC，而非系统 CMake 3.30.2。
原测试源码在 MSVC 下需要 C++20、`/D_USE_MATH_DEFINES /EHsc`，仅在配置参数中指定，未改上游源码。
测试覆盖运动坐标数学及 ToF 驱动的寄存器事务、数据新鲜度、状态码和故障路径，不能证明物理传感器、Wi-Fi、舵机或整机功能通过。

## 首次准备中发现的问题

1. 官方 `firmware/README.md` 仍要求 ESP-IDF 5.5.4、`fetch_repos.py` 后执行 `idf.py build`。
2. `fetch_repos.py` 拉齐六个固定依赖，但 Windows Git 系统配置 `core.autocrlf=true`，
   导致小智补丁检查失败且脚本仍返回成功。已单独检查并使用 `git apply --ignore-space-change`
   应用原仓库补丁，随后反向 `--check` 成功。没有修改系统 Git 配置或上游补丁。
3. 因此不能仅凭依赖脚本退出码判断补丁已应用。核对命令：

   ```powershell
   git -C .\StackChan\firmware\xiaozhi-esp32 apply --reverse --check --ignore-space-change F:\stackchan\StackChan\firmware\patches\xiaozhi-esp32.patch
   ```

   若从不同工作目录执行，请将补丁路径改为绝对路径。
4. GitHub 工具下载曾无进展。通过 ESP-IDF 自带支持的
   `IDF_GITHUB_ASSETS=dl.espressif.com/github_assets` 官方镜像完成工具安装。
   安装日志在 `logs/idf-tools-install-mirror.log`、`logs/idf-python-install.log`。
5. ESP-IDF 5.5.4 的 `idf.py` 不接受 `-j`。已删除构建脚本中该未经核对的选项，复用官方 `build` 入口。
6. 首次原版构建时上游主仓库干净；此后距离提醒代码在本地分支开发。
   `firmware/xiaozhi-esp32` 的九个改动文件来自官方必需补丁，属于预期状态，不纳入本次功能提交。

构建存在上游警告，例如枚举位运算弃用、未使用函数、`_IO` 宏重定义；本次未借准备工作修改它们。

## 距离提醒第一版

- 已初始化根 Git 仓库并保留原厂源码子仓库历史；两个开发分支均为 `feature/distance-reminder`，未推送。
- 固件在 HAL 初始化后启动独立采样任务，不随普通菜单卸载；外部 PORT A 使用 I²C0 / SDA2 / SCL1，
  内部 `board_get_i2c_bus()` 的 I²C1 / GPIO12、11 保持原用途。已检查源码中的控制器分配，物理接线待验证。
- ToF 驱动采用 ST 官方 VL53L1X 寄存器配置，Long 模式、50 ms 测量预算、100 ms 周期。
  只把有效状态及 40–4000 mm 范围的读数标记有效；读失败、无目标、超时都不会复用旧读数。
- `SC_DISTANCE` 单行 JSON 通过现有 stdout 发布。主控制台 UART0、次级 USB Serial/JTAG，
  已在默认配置中固定次级 USB 输出；USB 输入不作为控制通道。
- 电脑端负责三样本中值、持续过近计时、迟滞、冷却、暂停、设置保存和 Windows 中文通知。
  默认阈值 40 cm、持续 5 秒、恢复 48 cm、提醒间隔至少 60 秒；默认静音。
- 完整固件构建通过，二进制 SHA256：
  `7e8abe2683a943240296cf8f9f0f9971f5fb0fc0292bb40b39ecae217adc5d42`，分区剩余约 28%。
  构建日志：`logs/firmware-20261030-120304.log`。
- C++ 主机测试 2/2 通过，日志：`logs/host-tests-20261030-115859-314-test.log`。
  电脑端 52 项通过，模拟状态流为 safe → near → 1 次模拟提醒 → 无效等待 → safe。
- 已实际运行 Windows 原生静音通知助手，使用明确标记“模拟”的 300 mm 距离；
  Shell 通知请求成功、进程退出码 0。未独立确认屏幕气泡显示，不能把 API 成功当作视觉验收。

实际 ToF 读数、USB 事件接收、跨菜单/AI 模式运行尚未进行实机验证。
当前构建没有刷入机器人；首次部署必须先备份设备并核对实际分区、启动槽及资源兼容性，
保留 Wi-Fi、绑定和校准数据。不能以构建或模拟通过代替实机结果。
安装方法、运行参数与固定传感器方向的限制见 [README.md](README.md)。

官方依据：[固件构建说明](https://github.com/m5stack/StackChan/blob/f901cc2969189f3f0e96af1442c0f96e77e1e2c1/firmware/README.md)、
[StackChan 硬件文档](https://docs.m5stack.com/zh_CN/StackChan)、
[ESP-IDF Windows 工具说明](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32s3/get-started/windows-setup-update.html)。
