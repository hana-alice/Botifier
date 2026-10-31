# StackChan 距离提醒

机器人独立采集 ToF4M 距离，平时显示动态表情，过近时显示大号红色生气符号、红边框和 “TOO CLOSE”，移远后解除。
日常表情提醒不依赖电脑、USB 数据线或网络；电脑端保留读数同步与可选 Windows 通知。
沿用 StackChan + Unit ToF4M（U172 / VL53L1X），不更换原厂软件路线。

当前设备已部署，实际 ToF 与 USB 数据接收通过；原厂固件槽和配置区保留。
备份与实测记录见 [DEVICE_DEPLOYMENT.md](DEVICE_DEPLOYMENT.md)。

当前设备为 **seq25/VALID 耗时诊断版**：seq23 实机出现 `SNAP CAMERA ERROR / 263`，手检测流程超出500ms预算，尚未修复。新版会记录失败帧的取帧与推理耗时，等待进入 DISTANCE 后采集。官方手检测已替换亮度运动门槛；seq21 的漏触发、键盘误触发与原版播放确认保留为历史证据，见 [DEVICE_DEPLOYMENT.md](DEVICE_DEPLOYMENT.md)。

## 接线与摆放

关机后，用附带 Grove 线连接 ToF4M 与机器人**机头左侧、USB-C 下方的红色 PORT A**。
顶部蓝色 PORT C 是 UART，黑色 PORT B 是 GPIO，本功能不接这两个口。
PORT A：黑 GND、红 5V、黄 SDA/GPIO2、白 SCL/GPIO1。

ToF4M 白壳凹槽中的两个光学窗口朝向被测目标，窗口保持无遮挡。
按当前摆放方案，将模块固定在电脑屏幕边框，朝向平时坐的位置，连接线留出机器人转头所需的余量。
若以后改装在会转动的头部，当前距离判断不会识别转头，需另做姿态门控。
读数是传感器到目标表面的距离，不直接等于眼睛到屏幕的距离，安装后需调整阈值。

[机器人接口图](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1205/K151_stack_chan_main_pictures_02.webp) ·
[ToF4M 正面与引脚](https://docs.m5stack.com/zh_CN/unit/Unit_ToF4M)

## 数据与提醒路径

```mermaid
flowchart LR
  ToF[ToF4M] -->|I²C 0 / SDA2 SCL1| FW[独立采样与本机判断]
  FW --> Face[红色大符号、边框与文字提醒]
  FW -.->|可选 USB SC_DISTANCE JSON| PC[电脑读数与本机告警状态]
  PC -.-> Notify[可选 Windows 提醒]
```

- 采样任务跨菜单与 AI 模式持续运行，独占外部 I²C 控制器 0；内部控制器 1 保持原用途。
- 固件对三个有效新样本取中值。默认中值低于 **40 cm** 就显示提醒，等待时间 **0 秒**；恢复到 **48 cm** 解除。采样约 10 Hz，仍有采样与中值滤波延迟，不能把“0 秒等待”理解为 LCD 零延迟。持续过近时保持显示，不周期播放提示音。
- 提醒将原厂生气图放大到 80×80 px、改为红色，并增加 8 px 红边框和顶部 24 px 字体提示。显示线程在 LVGL 锁内管理，避开底部语音气泡和 Home 手势区；原表情、眨眼和对话继续运行，解除时移除整层提示，不回放旧的表情或眼嘴控制指令。
- 在原厂主菜单打开 **DISTANCE**，默认显示电子宠物脸：眨眼、回看，以及开心、撒娇、委屈等情绪动作；DANCE、AVATAR 与 AI 模式显示原厂脸时也接入同一提醒。主菜单、没有脸的界面或脸完全隐藏的来电界面不显示符号。
- 无效测量、传感器断连和采样间隔超过 1.5 秒都会清除本机滤波、累计和提醒；恢复后重新采满三个有效样本并累计。未接传感器不会阻止原厂主程序启动。
- `SC_DISTANCE` v1 保留原始距离、有效性、序号、设备时间和错误状态，并追加 `robot_alert` 布尔值。电脑端同时显示原始读数和本机告警状态，旧协议仍兼容；`robot_alert` 表示本机规则满足，实际符号还取决于当前是否显示脸。
- 电脑端可另行开启 Windows 通知；其规则与参数独立运行，默认提醒间隔至少 **60 秒**。关闭电脑程序不会停止机器人本机提醒。

## 机器人表情与实时调参

在原厂主菜单左右滑到紫色 **DISTANCE** 并点击进入；这是内置功能，无需应用市场、电脑程序或网络。

- **LIVE**：默认电子宠物表情，带实时距离与判定状态；过近时立即切换严肃眼神并叠加红色提醒。底部 **HOME** 返回菜单，**TUNE** 打开调参。
- **TUNE**：顶部 **F** 为中值、**R** 为原始距离（cm），后接有效性与状态。按三行 **− / +** 修改参数，下一帧测距应用；按 **LIVE** 返回看效果。读数每 100 ms 刷新。
- **SAVE**：显式保存当前参数；成功或错误会显示在页面上。未保存参数可以跨 LIVE/TUNE 和菜单切换保留，但重启后恢复已保存值。

| 参数 | 默认 | 操作 |
| --- | --- | --- |
| 距离阈值 | 40 cm | 每次调 1 cm；中值低于它才触发 |
| 恢复余量 | 8 cm | 每次调 1 cm；恢复距离 = 阈值 + 余量，减少边界反复闪烁 |
| 触发等待 | 0.0 秒 | 每次调 0.1 秒；0 表示中值越线后不再额外等待 |

例如 40 cm / 8 cm：中值低于 40 cm 触发，达到 48 cm 解除；进入提醒后，40–48 cm 区间保持提醒。
调参后会重置本次触发计时，在下一帧有效数据上重新判断；不会把之前积累的时间算到新设置上。
无效或超过 1.5 秒未更新时显示不可用并清除提醒，不将旧距离当作实时距离。
调参页不显示脸；LIVE 与其他脸部界面使用同一份本机判定。

电子宠物会在平静停留之间穿插好奇、开心、撒娇、害羞、委屈、困倦、惊讶和单眼眨；开心有笑眼与弹跳，撒娇有侧摆与腮红，委屈有垂眼、小弯嘴和泪滴。动作使用约 280 ms 平滑转场，普通表情最多约 30 Hz 更新；告警信号变化绕过动画节流。恢复安全后短暂放松再回到日常动作；读数无效时回到安静表情，不播放庆祝。

动作只改变屏幕五官。真实距离仍由原有采样和阈值判断，TUNE/SAVE 用法相同。浏览器[动作设计与实际固件渲染预览](demos/face-lab/README.md)可以单独查看；实际 LVGL 图集包含开心、撒娇、委屈与告警切换动画，属于桌面渲染，不是机器人 LCD 实拍。

## 响指播放 KURO GAMES 片头

新固件在 **DISTANCE** 内本地监听短促声音，默认要求官方手检测模型确认镜头中央出现较大的手，并获得一次互动授权；候选通过后，全屏播放 **3.4 秒黑底 KURO GAMES Logo**，结束后返回原页面。素材取自[库洛 PGR 官网](https://pgr.kurogame.net/)公开宣传片，保留原始淡入淡出；该片头区间静音。来源、转换方法与文件摘要见 [media/README.md](StackChan/firmware/main/apps/app_distance/media/README.md)。

播放期间暂停本功能的麦克风与相机采样，忽略后续响指，不排队、不重播或延长；结束后清空旧候选并重新预热监听。画面上的 **HOME** 可中止播放并返回主菜单。过近或测距不可用时阻断触发，新的过近提醒会中断片头并优先显示。退出 DISTANCE 后交还麦克风供原厂功能使用。

进入 **TUNE → SNAP** 确认 LISTEN/CAM assist 为 ON、SENS 为 3，再按 **LIVE**；保持测距安全，先把手移出镜头约1秒，再举到镜头中央，等绿色 **SNAP READY** 后打一次响指。每次播完或授权到期，都移开再举手。SNAP 顶部 **DISTANCE** 返回距离调参。

LIVE 顶部 **MOVE HAND OUT → SHOW HAND → HOLD HAND → READY** 分别表示移开手、展示手、稳定检测和已授权；READY窗口为1.8秒。**WARMING** 表示音频预热，**SOUND FILTERED** 表示声音被规则过滤，**CAMERA WAIT / CAMERA ERROR / NEEDS DISTANCE** 表示输入未就绪。日常表情会自行变化，不代表响指被接受；成功接受会直接切到片头。

| 控件/读数 | 含义 |
| --- | --- |
| SENS 1..5 | 默认 3；数值越大，声音门槛越低，也可能更容易误触发 |
| CAM assist | 默认 ON，要求新鲜手检测和一次举手授权；OFF 可单独测试声音候选 |
| LISTEN | 开关本地监听；OFF 停止本功能采音与相机采样 |
| P / N | 当前声音峰值 / 自适应噪声估计，均为 PCM 数值 |
| H% / clear、show、hold、armed / ms | 手框置信度 / 手检测阶段 / 最近一次推理耗时 |
| OK / NO | 通过 / 被拒绝的候选；屏幕大计数显示为 9999+ |

这些选项仅保存在内存，不通过距离页 SAVE 写入；重启恢复默认。开监听、修改设置或播放结束后重新预热；连续会话内成功互动间隔至少 1 秒。当前声级、最近瞬态的时长/形状、候选判定与计数通过最多 5 Hz 的 `SC_SNAP` JSON 调试日志输出，`playback_paused` 表示片头暂停监听；原有 `SC_DISTANCE` 和电脑距离同步继续使用。

当前使用短声音启发式与官方手框检测，**不识别响指姿态**。区域内的手配合拍手、敲击或键盘短声音仍可能触发一次；真实误触发率须实测。机器人播放声音的参考通道用于保守抑制，阈值仍待实测。摄像头异常或画面过期时，CAM ON 会拒绝候选。新服务不保存或上传原始音频和图像。

2026-10-30 先完成代码与模拟检查；用户于次日连接 USB 后更新了固件。仍需要响指、拍手、打字、背景音乐等正负样本来校准灵敏度和误触发率。

## USB、Wi-Fi 与参数

机器人只要正常供电、ToF 连着 PORT A，即可在部署此固件后独立提醒。
USB 仅用于当前的首次开发测试、日志和可选电脑同步；固定在最终位置时可以不连接电脑 USB。

机器人阈值、恢复余量和等待时间可在 **DISTANCE** 内实时调整；下一帧新测距应用，修改后重新开始本次判定，不沿用旧计时。按 **SAVE** 才写入本机设置，重启后恢复已保存参数；未保存的修改仅保留到重启。
下面的电脑命令只调整电脑端规则，**不会修改机器人规则**。机器人无线改参、Wi-Fi 距离同步和电脑直接触发的局域网 OTA 入口尚未实现。

原厂源码已有固件 OTA 和 AI 用户工具 `self.upgrade_firmware(url)`，但触屏更新页没有输入任意本地固件 URL 的入口。
现机旧版本和当前平台能否调用该工具尚未验证，因此不声称首次技术上必须 USB；现阶段先在近处通过 USB 备份、部署和测通，是可控的开发路径。
OTA 机制见 [ESP-IDF 文档](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32s3/api-reference/system/ota.html)。

## Windows 使用

本机设备现已运行含距离采样的固件，可双击根目录
`Start-DistanceReminder.cmd`，或从项目目录运行：

```powershell
.\scripts\Start-DistanceReminder.ps1

# 串口存在多个设备时指定端口；串口号以当前系统为准
.\scripts\Start-DistanceReminder.ps1 -Port COM38

# 只查看同步信息，使用机器人表情提醒
.\scripts\Start-DistanceReminder.ps1 -NoNotify

# 只修改并保存电脑端参数；这些是可调示例，不是医学建议
.\scripts\Start-DistanceReminder.ps1 -ThresholdCm 40 -HysteresisCm 8 -HoldSeconds 5 -CooldownSeconds 60

# 开/关电脑提示音
.\scripts\Start-DistanceReminder.ps1 -Sound
.\scripts\Start-DistanceReminder.ps1 -NoSound

# 完全不连接机器人，用模拟读数演示提醒
.\scripts\Start-DistanceReminder.ps1 -Simulate
```

控制台显示连接、读数、`robot_alert=on/off/unknown` 与电脑端状态。
按 **P** 只暂停/继续电脑端判断，按 **Q** 退出电脑程序，均不暂停机器人本机提醒。
设置保存在本地 `pc/reminder-settings.json`（Git 忽略）；模拟模式不会保存配置或访问串口。
普通固件日志不会被打印或存入提醒日志。系统勿扰/专注设置可能隐藏通知气泡，控制台仍有 `ALERT`。

串口按已有采集方式设置 DTR/RTS，但不同驱动下打开端口仍可能引发设备复位。
自动选择仅在唯一匹配的 ESP32-S3 USB 设备存在时进行，多台时使用 `-Port`。

## 开发与验证

```powershell
# 主机状态机/协议测试与无通知模拟，不操作机器人
.\scripts\Test-DistanceReminder.ps1

# C++ 测距、宠物动作、合成音频和运动融合测试，不访问硬件
.\scripts\Test-Host.ps1

# 构建完整 ESP-IDF 固件，不刷写
.\scripts\Build-Firmware.ps1
```

固件入口：`StackChan/firmware/main/distance_sensor/`。
电脑入口：`pc/distance_reminder/`；通知使用 Windows 自带 PowerShell/WinForms。
工具链与已有依赖均沿用 [DEVELOPMENT.md](DEVELOPMENT.md)，没有新增 Python 包或 Arduino 框架依赖。
ToF 寄存器驱动依据 ST 官方代码改编，来源与许可保留在其目录。

构建成功、模拟通过与真实传感器/Windows通知验证是不同结果。
本机首次部署的备份与分区核验已完成；现有构建脚本仍只编译，不自动刷写。
原厂在线更新可能覆盖本地新增功能，升级前需保留本地修改。

## 源码与 Git

根仓库记录电脑端、脚本、文档及固件子仓库指针，`StackChan/` 保留原厂 Git 历史。
两个仓库的本地开发分支均为 `feature/distance-reminder`。发布仓库为 [hana-alice/Botifier](https://github.com/hana-alice/Botifier)：`main` 保存根项目，`stackchan-firmware` 保存带本项目修改的固件历史；没有向 M5Stack 上游推送。
工具、构建产物、日志和本地设置不提交。迁移本地项目时要同时保留根仓库与固件子仓库的提交。

完整获取源码：

```powershell
git clone --recurse-submodules https://github.com/hana-alice/Botifier.git
# 已克隆根仓库时补取固件
git submodule update --init --recursive
```

子模块固定到根仓库记录的固件提交；仅克隆根仓库不会展开 `StackChan/`。本机后续发布时，先在固件仓库执行 `git push botifier HEAD:stackchan-firmware`，再在根仓库执行 `git push origin HEAD:main`。

原始交接：[STACKCHAN_HANDOFF.txt](STACKCHAN_HANDOFF.txt) ·
环境记录：[environment.json](environment.json) ·
网络排查：[WIFI_DIAGNOSIS.md](WIFI_DIAGNOSIS.md) ·
蓝牙演示：[demos/stackchan-ble/README.md](demos/stackchan-ble/README.md)
