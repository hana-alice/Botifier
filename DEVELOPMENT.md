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
| 设备 | COM38，USB VID 303A / PID 1001；实际备份确认原厂槽 StackChan 1.6.0 / ESP-IDF v5.5.4，现运行距离提醒构建 |
| C++ 主机测试 | 运动数学、真实 ToF 驱动、本机规则、诊断快照、表情生命周期及宠物动作，6/6 通过；驱动 11 组、规则 30 组、快照 9 组、生命周期 2 组、宠物动作 13 组 |
| 电脑端测试 | 57 项通过；包含新增调试字段的兼容测试；无硬件模拟产生 1 次电脑端提醒 |
| 当前固件构建 | 含采样、本机判断与表情符号；产物大小和 SHA256 见下文与 environment.json |
| 固件刷写 | 已备份整片 16 MiB 并校验；距离固件写入 ota_0，原厂 ota_1、资源与配置区保留并核验 |
| 配网 | 用户确认更换为 AX300 后连接成功；热点排查结束，不再作为开发阻塞 |
| ToF4M | 用户确认已接线；新固件已返回约 80 cm 的有效读数，约 10 Hz |
| 摆放目标 | 用户计划将 ToF 固定在电脑屏幕边框，Grove 线连接机器人；USB 较短，先近处测试 |
| 测距调试入口 | 主菜单 DISTANCE；LIVE 电子宠物脸、TUNE 实时距离与三参数调节、HOME 退出；读数 100 ms 刷新；用户已确认上一版诊断读数灵敏 |

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
本次已读取实际分区表，只有 ota_0/ota_1，无独立 factory 分区；备份中 ota_0 是已标为 ABORTED 的
flappy_bird，ota_1 是有效原厂 1.6.0。距离固件替换 ota_0，保留 ota_1。未找到已启用的、可确认返回原厂主菜单的特殊物理按键组合。
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
测试覆盖运动坐标数学、ToF 寄存器事务与故障路径、本机过近规则及计时回绕，不能证明物理传感器、脸部显示、Wi-Fi、舵机或整机功能通过。

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

## 当前距离提醒

- 已初始化根 Git 仓库并保留原厂源码子仓库历史；两个本地开发分支均为 `feature/distance-reminder`。发布到 `hana-alice/Botifier` 的 `main`（根项目）和 `stackchan-firmware`（固件）；未向 M5Stack 上游推送。完整获取需 `git clone --recurse-submodules`。
- 固件在 HAL 初始化后启动独立采样任务，不随普通菜单卸载；外部 PORT A 使用 I²C0 / SDA2 / SCL1，
  内部 `board_get_i2c_bus()` 的 I²C1 / GPIO12、11 保持原用途。已检查控制器分配，并在红色 PORT A 实际收到有效 ToF 数据。
- ToF 驱动采用 ST 官方 VL53L1X 寄存器配置，Long 模式、50 ms 测量预算、100 ms 周期。
  只把有效状态及 40–4000 mm 范围的读数标记有效；读失败、无目标、超时都不会复用旧读数。
- 用户已明确：过近时由机器人本机表情提醒，电脑信息同步保留供后续开发。因此本机规则不依赖电脑或网络。
  现有脸显示时复用并放大原厂 `AngryDecorator`，增加红框和顶部文字；移远、无效或数据过期就移除，不覆盖 AI 表情或远端眼嘴字段。
- 表情符号由 `StackChan::update()` 在现有 LVGL 锁内、modifier 更新后创建和更新，
  随 attached avatar 销毁，菜单无脸或脸全部隐藏时不显示；采样任务只通过原子状态发布告警。
- 本机规则为三个有效样本中值，默认低于 40 cm 即触发（0 ms 额外等待），48 cm 恢复，超过 1.5 秒无新数据清除。
  DISTANCE 的 TUNE 页可实时修改阈值、恢复余量、等待；采样任务在下一帧应用并重新判断，沿用仍新鲜的中值窗口但清除旧 latch 和计时。
  `Snapshot.config` 表示同一帧实际使用参数；请求参数独立用短锁复制。UI 不访问 I²C、不直接推进规则。
  SAVE 在 LVGL 锁外显式写入 NVS `sc_distance/config` 单个版本化 blob；开机校验版本、尺寸和边界后恢复，缺失或错误用默认。电脑命令仍只改独立的电脑规则。
- `SC_DISTANCE` v1 单行 JSON 通过现有 stdout 发布，并追加可选 `robot_alert` 布尔值。
  旧电脑帧兼容，电脑显示本机告警 on/off/unknown。主控制台 UART0、次级 USB Serial/JTAG，
  已在默认配置中固定次级 USB 输出；USB 输入不作为控制通道。
- 电脑端另有独立的三样本中值、持续过近计时、迟滞、冷却、暂停、设置保存和可选 Windows 中文通知。
  默认阈值 40 cm、持续 5 秒、恢复 48 cm、提醒间隔至少 60 秒；默认静音。
- 完整固件构建通过，二进制 SHA256：
  `527756678afd18f61865d1f25d87d24643b04e4375999c7d8c839587b42b50c3`，3,762,976 字节，分区剩余约 27%。
  最新构建日志：`logs/firmware-20261030-182654.log`。
- C++ 主机测试 6/6 通过，最新日志：`logs/host-tests-20261030-182110-194-test.log`。
  本机规则 30 组覆盖默认 0 ms 触发、在线改参数与重新计时、同值不重置、非法配置不变，以及原 5 秒回归、中值、迟滞、无效/断流恢复和时间回绕。
  快照 9 组覆盖新鲜/过期、副本不变、无效光学诊断、自定义参数与超时和计时回绕。
  新增生命周期 2 组通过真实 Avatar 头验证 100 次 reset 与 100 次替换的派生资源释放；隔离去掉虚析构后运行 0/2，证明捕获该回归。数学依赖使用专用 fake，不据此声称实测 LVGL 内存无泄漏。
  为支持 LIVE/TUNE 反复切换，修复 Avatar 基类缺少虚析构的原有缺陷；其派生 DefaultAvatar 的面板需要被正确释放。
  宠物动作 13 组覆盖可复现节奏、八种自动情绪、64 组转场、告警同一时间戳抢占、仅有效恢复时放松、无效数据不庆祝、100,000 帧有限值/边界与时间回绕。
  电脑端 57 项通过，模拟状态流为 safe → near → 1 次模拟提醒 → 无效等待 → safe。
- 已实际运行 Windows 原生静音通知助手，使用明确标记“模拟”的 300 mm 距离；
  Shell 通知请求成功、进程退出码 0。未独立确认屏幕气泡显示，不能把 API 成功当作视觉验收。

原厂源码有设置页检查更新及 AI 用户工具 `self.upgrade_firmware(url)`，后者可以调用自定义 URL 下载升级。
但没有已确认能直接由本机电脑触发的局域网入口，现机 1.5.1 的工具/平台可调用性未验证；
不能断言首次技术上必须 USB。Wi-Fi 距离同步、无线改本机参数及电脑直接 OTA 入口尚未实现。

当前电子宠物构建已刷入，实际 ToF 读数与 USB `SC_DISTANCE` 数据接收通过；独立读回启动记录 seq13 / VALID，
USB 重启后 HAL 确认 ota_0 / state=2，301/301 连续有效帧。首轮 602/602 连续有效帧，7 次告警开启、6 次恢复，规则与中值独立复算一致。
电脑程序实连、P 暂停/恢复、Q 退出也已通过；生气符号和跨菜单/AI 模式仍待现场验证。
部署前已完成整片备份、分区和资源兼容性核对；刷写后保护区域摘要确认原厂槽、Wi-Fi/绑定所在 NVS、
bootloader、分区表及资源未被刷写改变。备份、修正旧设备版本记录与上机证据见 [DEVICE_DEPLOYMENT.md](DEVICE_DEPLOYMENT.md)。
安装方法、运行参数与固定传感器方向的限制见 [README.md](README.md)。

## 电子宠物表情实现与验证

用户选择“更像电子宠物，开心、委屈、撒娇都夸张一些”。本次仅替换 DISTANCE LIVE 的脸；沿用测距、调参、保存和全局告警路径。

- `apps/app_distance/pet_motion.h` 是无 LVGL/IDF 依赖的动作模型：平静、好奇、开心、撒娇、害羞、委屈、困倦、惊讶、单眼眨、严肃提醒、放松共 11 个状态。普通动作约 280 ms 转场；随机只在安排动作时生成，保留看向目标、停留和回看的节奏，不逐帧抖动。
- `pet_avatar.h/.cpp` 用现有 LVGL 圆角图元与线条实现眼、嘴、腮红和泪滴。开心弹跳幅度 8 px、撒娇侧摆 12 px；实体眼先压扁再变笑弧，嘴由一条闭合路径变形，避免同时显示两套轮廓。
- `app_distance.cpp` 每次 UI 循环读取一致快照，普通五官更新间隔至少 33 ms、读数标签仍 100 ms；告警信号变化绕过 33 ms 节流。Alert 立即抢占闲置动作；只有 Alert→有效 Ready 才放松，无效/过期数据不庆祝。它不决定采样阈值，也不承诺 LCD 零延迟。
- LIVE/TUNE 切换时先释放装饰与五官，再释放 LVGL 父面板；不再为本页面创建独立 Blink/Breath modifier。其它 App 保留原厂脸和原有告警。

参考调研：[理想 OTA 6.4 情绪状态扩展报道](https://finance.sina.com.cn/tech/roll/2024-10-25/doc-inctucpx4877974.shtml)、[小爱车载表情彩蛋报道](https://m.ithome.com/html/823698.htm)、[NIO 官方交互说明](https://www.nio.com/cdn-static/www/user-instructions/en_EU/EL7/index.html)。借鉴情绪与眼神动作的方向；本项目几何、节奏和素材为原创，没有将上述报道当作动画时长测量。小爱闹钟官方视频主要是时间/闹钟场景，未用它声称观察到完整情绪动画。

验证分为三个层次：

1. 主机动作测试 13 组，完整 CTest 6/6，日志 `logs/host-tests-20261030-182110-194-test.log`。
2. 浏览器动作设计在 1440 px、375 px 无横向溢出，按钮至少 40 px 高；真实浏览器检查 11 项状态/阈值/优先级，另有替身状态检查。它与固件不采用同一组数字参数。
3. 用项目实际 LVGL 9.4.0、生产 `pet_avatar.cpp` 和动作模型生成 11 种静态脸及 5 段动画；420 帧脏区刷新与强制整屏重绘逐像素一致，100 次创建/销毁后 screen 子对象 0→0、LVGL 可用内存 8,374,392→8,374,392 字节、分配块 94→94，完整性检查通过。该有限桌面实验不证明无限运行或其它分配器无泄漏；XRGB8888 与实机 RGB565 颜色量化不同，图集中未包含 AppDistance 控件和全局红框。

实际渲染图与源码摘要见 [Face Lab](demos/face-lab/README.md)。初版实际渲染暴露笑眼/嘴重影和委屈眼形不清，已据图修正并重渲染；`.omx/state/pet-face/ralph-progress.json` 保留前后视觉评审。设备更新与测距证据见 [DEVICE_DEPLOYMENT.md](DEVICE_DEPLOYMENT.md)，现场观感和触摸验收须与桌面渲染结果区分。

官方依据：[固件构建说明](https://github.com/m5stack/StackChan/blob/f901cc2969189f3f0e96af1442c0f96e77e1e2c1/firmware/README.md)、
[StackChan 硬件文档](https://docs.m5stack.com/zh_CN/StackChan)、
[ESP-IDF Windows 工具说明](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32s3/get-started/windows-setup-update.html)。
