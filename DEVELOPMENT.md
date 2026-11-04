# StackChan 开发准备

更新：2026-11-02。已完整阅读 `STACKCHAN_HANDOFF.txt`，沿用 StackChan + ToF4M 方案。最新全屏菜单与12秒护眼动作说明见本文末尾；历史试验按时间保留。

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
| 设备 | COM38，USB VID 303A / PID 1001，同一MAC；最新seq53/VALID独立读回，12拍真实位置反馈通过，已恢复默认20分钟 |
| C++ 主机测试 | 15/15通过；包括Policy25组、护眼控制器20组、真实Servo/Motion/UITK定时运动11组；硬件写回和时钟为替身 |
| 电脑端测试 | 历史77/77通过；本次PC代码未改，不冒充本次重跑 |
| 当前固件构建 | BOTIFIER全屏手势菜单、12秒四组动作、护眼专用350ms定时目标；无新依赖，当前与历史产物分开记录于 environment.json |
| 固件刷写 | 已备份整片 16 MiB 并校验；距离固件写入 ota_0，原厂 ota_1、资源与配置区保留并核验 |
| 配网 | 用户确认更换为 AX300 后连接成功；热点排查结束，不再作为开发阻塞 |
| ToF4M | 用户确认已接线；新固件已返回约 80 cm 的有效读数，约 10 Hz |
| 摆放目标 | 用户计划将 ToF 固定在电脑屏幕边框，Grove 线连接机器人；USB 较短，先近处测试 |
| 测距调试入口 | 主菜单 BOTIFIER；顶部下滑/底部上滑打开菜单，DISTANCE 实时调参，HOME退出；读数100ms刷新；用户已确认诊断读数灵敏 |

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
  已在默认配置中固定次级 USB 输出；新增空闲节目使用该接口的 `/dev/secondary` 非阻塞输入接收 `SC_IDLE`，原测距协议和主 UART 控制台保持原样。
- 电脑端另有独立的三样本中值、持续过近计时、迟滞、冷却、暂停、设置保存和可选 Windows 中文通知。
  默认阈值 40 cm、持续 5 秒、恢复 48 cm、提醒间隔至少 60 秒；默认静音。
- 上一版已部署电子宠物固件构建通过，二进制 SHA256：
  `527756678afd18f61865d1f25d87d24643b04e4375999c7d8c839587b42b50c3`，3,762,976 字节，分区剩余约 27%。
  该版构建日志：`logs/firmware-20261030-182654.log`。当前构建路径已由新功能产物替换，已部署文件仍归档于 `.tools/device-backups/20261030-182853-pet-face/distance-pet-working.bin`。
- 该次 C++ 主机测试 6/6 通过，历史日志：`logs/host-tests-20261030-182110-194-test.log`；当前 7/7 结果见下文。
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

实际渲染图与源码摘要见 [Face Lab](demos/face-lab/README.md)，记录已部署电子宠物版本的历史源码，不代表后续响指改动已部署。初版实际渲染暴露笑眼/嘴重影和委屈眼形不清，已据图修正并重渲染；`.omx/state/pet-face/ralph-progress.json` 保留前后视觉评审。设备更新与测距证据见 [DEVICE_DEPLOYMENT.md](DEVICE_DEPLOYMENT.md)，现场观感和触摸验收须与桌面渲染结果区分。

## 响指候选与摄像头辅助

用户表示手通常在镜头前方，2026-10-30 先完成代码、未操作硬件；次日连接 USB 后继续部署。上文 seq13/VALID、602 帧和 301 帧测距结果均属于上一版，不能用于验证本次感知功能。

- `snap_detector.h`：纯 C++17、固定大小状态；24 kHz、10 ms 音频块，约 500 ms 预热，自适应噪声、起音、时长、波峰和相邻差分能量筛选。PCM 通道 0 是麦克风，通道 1 是播放参考，不按两个独立麦克风处理。播放及其后 200 ms、明显削波、长音和窄带音被抑制。这些规则不等于真实响指分类准确率。
- seq21 及之前的图像路径仅取 32×24 亮度网格，以亮度平移补偿和局部变化筛选运动，拒绝大范围变化；它不判断手或响指姿态。原规则需要帧龄不超过 500 ms、候选前 600 ms 至后 350 ms 内的运动。用户实测键盘持续误触发后，生产服务已改用下述真实手检测，亮度运动不再充当后备证据。
- `snap_service.cpp`：独立音频/相机任务、短锁快照和 epoch 隔离；DISTANCE 打开后监听，关闭时等待 checked 输入关闭确认后才交还麦克风。采样、I²C、相机等待和输入关闭均不在 LVGL 锁内执行；ToF 不可用/过期/滤波未就绪、过近或播放时拒绝互动。参考 RMS 200 是尚未校准的门槛，不是声学回声消除。
- `stackchan_camera` 复用原有 V4L2 缓冲，兼容 YUYV 与本项目驱动使用的 YUV422P；新网格接口不编码 JPEG、不改 `frame_` 或原厂拍照设置。SDK `VIDIOC_DQBUF` 可无限等待，因此相机独立执行，退出只作废其会话，不等待相机线程。硬件故障可能让该线程及相机锁持续等待，后续拍照也可能受阻；不能声称有硬超时。
- `cores3_audio_codec` 新增 checked 输入开关，检查底层 ADC、增益和 RX 关闭结果，失败清理可重试，读错误不再伪报成功；输出与原有 AI 开关入口沿用。底层关闭也没有可证明的总时限，故障时退出可能等待，需实机验证。既有 `input_gain_=60` 最终受 ES7210 档位限制，不表示物理 60 dB。
- 复用 DISTANCE 三行调参控件新增 SNAP 页；调参只更新感知配置、不会写距离 SAVE。默认 LISTEN/CAM ON、SENS 3，RAM 保存。最初的惊讶→开心反应已由下述 KURO GAMES 片头替代，距离 Alert 随时抢占。`SC_SNAP` 最多 5 Hz，保留最近瞬态特征便于排查；不保存或上传原始音频/图像。

片头集成前的验证记录：

1. `logs/host-tests-snap-final.log`：CTest 7/7；宠物动作 15 组（新增互动、抢占与回绕），检测器 12 组合成 PCM/网格测试，覆盖播放、参考通道隔离、削波、连续声、采样间断、全局亮度变化、候选时窗、冷却与计时回绕。
2. `logs/pc-tests-snap-final.log`：电脑端 57 项通过，无硬件模拟产生 1 次提醒。
3. `.tools/snap-ui-probe/manifest.json`：完整生产 View、真实 LVGL 9.4.0、控件与字体的桌面渲染，15 张截图，24 项行为与 408 项对象边界检查通过，0 裁切；极值计数压缩为 9999+，底行自然宽度 269/304 px。新增后台事件时序回归：快照将时钟从 100 推至 103 ms 时，修复版产生 Surprise、相同计数不重播；隔离副本恢复旧读取顺序则事件被吞掉，日志 `logs/snap-render-old-order.log`，反例按预期退出 42。音频/相机/ToF 为替身输入，全局红框层未纳入，不能当作实机触摸和感知验收。
4. 完整 ESP-IDF 构建与当前二进制摘要见 `environment.json`，最新物理部署见 `DEVICE_DEPLOYMENT.md`。初次进入 DISTANCE 的真实崩溃暴露了桌面 libc 与设备 nano printf 的差异：后者不支持 `PRIi64`，已将新日志中仅需 -1 哨兵的 `visual_age` 改为 `int` / `%d`，保留原 libc 配置。修复版 seq17/VALID 已独立读回；真实响指/拍手/键盘/音乐准确率、资源占用和硬件采样时序仍未完整验证。

## 响指触发黑底片头

`intro_clip.h` 校验只读 `KINTRO1` 包，`intro_player.cpp` 在独立任务中按时间选择 JPEG 帧，复用原有 `jpeg_dec::decode_to_lvgl()`；落后时跳帧，不积压播放请求。当前资源来自[库洛 PGR 官网](https://pgr.kurogame.net/)公开宣传片的原始黑底 Logo，320×240、12 fps、41 帧、3.4 秒、176,092 字节。该区间静音；不增加音频控制或新依赖。来源、SHA256、格式与可重复生成步骤见 `main/apps/app_distance/media/README.md` 和根目录 `scripts/Prepare-KuroIntro.py`。

资源只嵌入 app；不替换设备 assets 分区。播放器至多保留两张解码图，View 持有当前图，确保 LVGL 借用的 descriptor 与像素在切换或删除前有效；解码在显示锁外。画面暂时没有新帧时保留上一帧，不闪回加载状态。

第一次有效事件在启动任务前同步设置 busy。`pause_for_playback()` 等待输入关闭确认，作废旧音频/相机会话；播放期间不接收新候选，计数和用户配置保留。完成、取消和错误均恢复监听并重新预热，完成基线屏蔽暂停前已经到达的旧事件。App 关闭先 stop 感知，再 stop 播放器，恢复过程不会重新打开已关闭 App 的输入。新距离 Alert 中断播放，HOME 可直接退出；等待均不持 LVGL 锁。底层输入关闭和 JPEG 解码仍没有已证明的总时限。

主机测试 CTest **8/8**，新增包解析 6 组涵盖真实 baseline JPEG、截断、非法表项/边界/尺寸/时长和非 baseline 格式；日志 `logs/host-tests-intro.log`。全部 41 帧另经 ffmpeg 完整解码，重复转换所得包逐字节相同。电脑端 **57 项及无硬件模拟**通过，日志 `logs/pc-tests-kuro-intro.log`。ESP-IDF 5.5.4 完整构建通过，应用 **3,958,176 字节**，分区余量约 24%，日志 `logs/firmware-kuro-intro.log`。桌面播放界面与实际部署结果另记 `DEVICE_DEPLOYMENT.md`；主机测试不代表真实响指准确率或 LCD 播放帧率。

### 依据实机短瞬态修正形状过滤

seq19 的后续 180 秒窗口中，用户反馈 5 次仅 0–1 次触发；876 条 SC_SNAP 只有一次新候选/接受，且该接受完整播放。保留的 34 个 tonal 事件全部因 edge<0.8 被拒，crest 均通过原 2.5；SENS 不影响这个固定门槛。事件没有逐次用户标签，不能把所有事件认定为响指或据此计算准确率。

保留原 broadband 分支，另允许 `duration<=30ms && edge_ratio>=0.45 && crest>=3.0` 的短高峰衰减瞬态；原起音、峰值/RMS、长音、削波、播放与摄像头规则保持。8 个连续指数衰减合成夹具（含跨块）在旧规则全拒、新规则全过；低 edge、两个 40ms 衰减声及低峰度短音 4 个负控仍拒，原 12 组保留，检测器合计 **14 组**。CTest **8/8**，日志 `logs/host-tests-20261031-142121-166-test.log`。这是对观测特征的兼容，不是训练分类器；短敲击或衰减铃声也可能通过。

LIVE 顶部明确显示 READY、WARMING、SOUND FILTERED 与画面/距离未就绪状态，避免把闲置表情误认为接受事件。反馈文字仍每 100ms 刷新；标签显隐跟随每帧告警信号，保护全局提醒标题。真实 View 新增状态验证曾捕获首个 Alert 帧仍留 READY 的缺陷，修复后 **57 行为/761 边界**检查通过、0 裁切，6 次图像释放中 0 次仍被借用；最大文字 206/304px。证据 `.tools/intro-ui-probe/responsive-manifest.json` 和 `responsive-frames/contact.png`，初版片头证据保存在 `baseline-intro/`。桌面感知/播放器为替身，现场灵敏度须另测。

修正源码提交 `217f37a`，构建 `logs/firmware-kuro-snap-response.log`，二进制 **3,958,976 字节**、SHA256 `e22df798f9002fab7fcbf8277fbaf8c750a2e69d3a59408a55944a84bd0a29ac`。部署与现场复测见 `DEVICE_DEPLOYMENT.md`。

### 官方手检测与一次举手授权

首次完整链接生成6,112,672字节镜像，超出原有0x4f0000应用槽935,328字节，见 `logs/hand-model-build-resume.log`，该镜像未刷入。尺寸表 `logs/hand-model-size-baseline.log` 显示 ESP-DL 的 flash code 占1,645,905字节。项目据此仅对该组件追加 `-Os`，并通过官方Kconfig只启用 RGB888→RGB888（包括int8量化）转换；其余14项像素转换关闭。设置在顶层 `project()` 完成之后应用，生成命令的最后一个优化项为 `-Os`，证据 `logs/hand-model-size-config-review.json`。

尺寸修正后完整构建通过，日志 `logs/hand-model-build-size2.log`。应用5,032,096字节，SHA256 `23926da2e211574131b0e4422da21944df221659e2c7f2b2a62a575ad53acec9`，原槽剩余145,248字节（约3%）；最终 ESP-DL flash code 为571,335字节，见 `logs/hand-model-size-final.log`。固件提交 `374a7a4e6e27893b195a8518e9d60ff02ddbc8d4`。未采用私有算子注册表覆盖；实际推理速度仍待设备采样。

用户明确授权新增依赖后，精确锁定 `espressif/hand_detect==0.2.0` 与 `espressif/esp-dl==3.3.0`，保留所有既有组件版本。选取这组兼容版本是为了沿用 JPEG0.6.1 和 DSP1.7.0；没有升级其它音视频依赖。S3 模型 `espdet_pico_224_224_hand.espdl` 为497,184字节，SHA256 `ada47f76f8393cbb5572c26862edea17905e8f352669b29033a996ffbcba020a`，只嵌入 app rodata，不新增模型分区。模型只返回手框，不返回响指姿态或手指关键点。

`hand_model.cpp` 在独立相机任务中同步单核推理，RGB888 输入由已有 GC0308 的 YUV422 转换而来。`TrySampleRgbFrame()` 要求单 mmap buffer，在同一相机锁内先归还排队旧帧、再等待新帧，复制后释放锁；推理不占相机或 LVGL 锁。ESP Video 没有填有效采集时间戳，因此以调用前时钟为保守下界，取帧加推理总耗时超过500ms即拒绝。相机任务目标周期200ms、优先级1、栈16KiB；这些配置不等于实测5Hz。底层 DQBUF 仍可无限等待，退出仅隔离 epoch，不等待该线程。

`hand_gate.h` 要求置信度至少0.5、手框宽至少画面1/6、高至少1/4，中心在横向20–80%、纵向12.5–66.7%的区域。有效无手/区域外样本至少两帧跨250ms后，进入 SHOW HAND；区域内至少两帧跨100ms后进入 READY，授权固定1.8秒，持续有手不会延长。接受一次短声音立即消费授权，之后必须移出再举手；无效、晚到或过期画面拒绝。授权时间由保守采样时钟计算，尚未测量物理举手持续时长。

LIVE 显示 MOVE HAND OUT／SHOW HAND／HOLD HAND／READY，SNAP 页显示手置信度、阶段和推理毫秒数；串口追加手框、区域、授权和相机错误。`camera_required` 默认开启，只有用户显式关闭后才声音独判。任意符合区域的手配合通过启发式的短声音仍可能触发；区域规则不是键盘分类器，误检/漏检造成的新举手窗口仍需实测。

新增 RGB 转换5组、手门控17组合成测试，完整主机CTest10/10通过，日志 `logs/host-tests-20261031-145605-806-test.log`。实际生产View与HandGate桌面验证86行为/1367边界检查通过、0裁切、6次图像释放无借用违规，证据 `.tools/intro-ui-probe/hand-manifest.json`；摄像头识别输入为替身。静态调用链审计 `logs/hand-model-format-audit.json` 未发现当前路径中的64位printf；禁止调用含 `%lld` 的 SDK `profile_memory/profile_module/profile`，保留 nano libc。模型预检与异常处理不能保证厂商内部全部 OOM/assert 路径可恢复，完整链接、RAM、延迟及键盘负样本须以实机证据为准。

官方依据：[固件构建说明](https://github.com/m5stack/StackChan/blob/f901cc2969189f3f0e96af1442c0f96e77e1e2c1/firmware/README.md)、
[StackChan 硬件文档](https://docs.m5stack.com/zh_CN/StackChan)、
[ESP-IDF Windows 工具说明](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32s3/get-started/windows-setup-update.html)。

## 空闲节目与 Codex 周额度（2026-11-02）

`apps/common/idle_program` 复用现有 FreeRTOS、NVS、USB Serial/JTAG 与 LVGL，不增加组件。`Policy` 接收单调毫秒时钟；服务每40ms推进，仅BOTIFIER（原DISTANCE）LIVE发布可展示上下文并在同一LVGL锁内渲染，超过500ms未更新即阻止显示。主菜单与AI没有栏目渲染入口；退出应用禁用上下文并清除图层。最新优先级为NFC片头/EyeRest并列且互斥→距离Alert→普通信息，设置页暂停节目。普通触摸在有效EyeRest期间不取消动画，显式HOME/TUNE/OFF仍可退出。节目图层以稳定的`lv_layer_top()`为父，更换屏幕不会保留悬空父指针；退出只删自有控件与宠物，不重设共享avatar。活跃动画最多约30Hz，抢占不受节流限制。App取测距快照后采样任务可能继续更新，因此在`GetStackChan().update()`后再以`alert_active()`与快照Alert取并集，阻止普通栏目并隐藏顶部状态；有效EyeRest保留。

`try_begin_foreground_animation()`与服务tick共用state_mutex：已有有效EyeRest则拒绝且不改其时间，否则先占foreground_busy再由App调用片头start，同帧以真实播放结果覆盖context。刷卡事件在claim之前消费，失败或动画中被拒绝的事件不补播。距离Alert只阻挡普通栏目，不刷新idle活动时间，因此到期EyeRest能在持续Alert中开始并完整计时；NFC播中到期只留单due。EyeRest期间LIVE控制层用不透明蓝紫底遮住下层红底的底部区域，结束恢复透明，保留按钮。

当前EyeRest分为Intro→Motion→Rest，只有真实clip完成（或启动失败）才以generation匹配的`finish_eye_intro()`进入Motion；固定4,500ms动作结束后，以实际tick时间开始完整rest_duration_ms。worker延迟不会吞掉REST20。各阶段均递增generation、保留EyeRest最高优先级；普通触摸/NFC/Alert不能重置阶段。遥测追加eye_phase/phase_elapsed_ms，原Program和v1字段保留。

`intro_player::ClipId`区分Kuro与Shorekeeper，共用原JPEG worker、两个weak RGB image slot与KINTRO1校验。App只把Kuro busy作为外部foreground，自己的Shorekeeper不取消EyeRest；完成确认要求!busy、completed_count变化且不同于start前baseline。同步stop后立即消费完成/RF边界并重新读scanned，防止旧完成误确认新一轮或旧卡重放。stop等待在LVGL锁之外。38帧预渲染3D pack占324,546字节；旧600KiB静态BGRA链接与无调用方art代码已删除，历史原图/提示与pack保留。

`RestHeadMotion`在Intro仅预占协作锁，不发动作，已有idle移动可自然结束；Motion开始时才读取实际姿态。按500ms节拍只发当前目标，左右相对±8°、俯仰±4°，与实时硬限取交集，speed300；最后回起始位置。无法满足窗口、他人持锁或旧运动未结束则跳过实体动作；失锁不夺，取消只停止自身已发起动作。REST期间持锁抑制普通转头，退出后释放。既有bool锁没有owner token，未扩展全局运动架构；实际角度/方向/节奏须分别实测。View独立pet按同一节拍变表情，不伪造全局距离状态。

普通Countdown用暖色钟环与居中MM:SS，Weekly用冷蓝底、>_标识与10格额度条；等待/过期/失败与0/100%保留。仅用既有LVGL/字体。转换只用标准库与已有FFmpeg，不新增依赖。

护眼默认20分钟/20秒，完成短暂显示RestDone；中断不记完成，到期仅保留一次。用户追加要求信息主导，常规栏目按EyeCountdown12秒→Pet3秒→Weekly12秒→Pet3秒交替（正常约80%信息），到期休息可抢占普通栏目。EyeCountdown只读eye_remaining_ms，不把12秒卡片时长当工作计时。NVS v2保存新节奏，v1只迁移原开关/护眼间隔/休息时长。当前精简字库缺少护眼中文用字，经实际glyph查询与画面确认，采用已有英文文案，避免引入完整大字库。

`SC_IDLE` v1输入为至多512字节ASCII单行，未知/重复字段、错误类型、非整数/溢出、超长帧被拒绝，且不回显输入。secondary VFS使用现有basic RX立即读取，每轮最多512字节，不安装新USB驱动或切换UART控制台。固定IDF5.5.4中basic模式不能使用`O_NONBLOCK`，具体实验证据见下文。`SC_IDLE_STATE`约1Hz发布只读节目与额度状态。可用以下临时参数缩短验收周期；`save:false`不写NVS：

```text
SC_IDLE {"v":1,"type":"config","eye_interval_ms":20000,"rest_duration_ms":20000,"save":false}
```

本机三行调参仅暴露PROGRAMS/EYE min/REST sec，SAVE在LVGL与状态锁外写`sc_idle/config`版本化blob。调试协议可修改Config已知计时字段；测试完应恢复默认或重启，避免留下短周期。

PC `pc.codex_weekly`通过现有Codex native app-server只读握手与`account/rateLimits/read`，严格选择`codex`桶的10080分钟窗口，不假设primary/secondary位置。独立子进程stdio、总超时、仅清理自身子进程；助手不直接读取凭据文件，不调用thread/turn、额度重置或购买方法。查询失败立即发布unavailable；只向设备发送percent/reset/observed/sequence/ttl，原始API响应及设备普通日志不存储、不打印。额度按真实查询时间标记，过期不再显示旧百分比。API资料：[官方App Server文档](https://learn.chatgpt.com/docs/app-server)。

串口单owner持续drain；默认每300秒查询、每15秒重发。序号阻止同运行周期重放延长有效期；PC重发缩短TTL，防止无RTC设备重启后重新给旧数据15分钟。该保守策略可能提前失效，不会延长缓存有效期。设备端以TTL与reset先到者为准，不把查询失败或无数据显示成100%。USB断开后护眼保持独立；本轮未实现Wi-Fi quota投递或系统开机自启。

### USB接收实机联调纠正

首轮使用O_NONBLOCK的输入实现存在缺陷：主机首次配置写超时，设备读数持续但未接收额度。此前仅根据“非阻塞”分支就认为接收可工作的审查结论错误。回到固定SDK源码闭环发现：`usb_serial_jtag_vfs.c`在O_NONBLOCK时跳过唯一FIFO预读，随后以`usb_serial_jtag_get_read_bytes_available()`计算fetch_size；该函数在未安装driver时恒返回0。因此basic模式的新字节无法被读取，并非stdout被O_NONBLOCK关闭。

独立实验从SDK自动提取7个真实函数、只替换硬件FIFO/无driver状态和锁，在主机编译运行：O_NONBLOCK下待收16字节、读取0、FIFO尝试0；O_RDONLY完整收16字节，空FIFO一次尝试立即返回EWOULDBLOCK；512字节预算读512并留下88。证据`logs/usb-secondary-vfs-source-probe.json`、`.log`，可复现`.tools/usb-secondary-vfs-probe/run.py`。该实验不替代USB硬件验证。

修正保留VFS锁、peek与换行处理，basic模式以O_RDONLY启用立即FIFO预读，短read继续、每40ms总计最多512字节。若未来安装USB driver则停止本输入路径，避免进入blocking read；不混用另一条LL输入、不改变TX或主UART。seq39实际USB采集已收到真实75%周额度并进入Weekly、EyeRest；Alert抢占时完成计数保持0，具体当前版本与后续倒计时界面验收见部署记录。

## BOTIFIER全屏菜单与四组慢拍（2026-11-02）

本次只修改BOTIFIER及其护眼动作，不改变原厂主菜单或AI应用。移除常驻底部HOME/TUNE和顶部状态，将导航与状态集中到独立`lv_layer_sys()`抽屉；不修改共享sys层属性。顶部28px内向下、底部28px内向上，向内滑至少42px且纵向明显大于横向，松手打开；最长4秒。菜单可从视频、宠物、栏目和设置页呼出，单纯开关菜单不改播放或护眼计时。显式HOME或切换设置由下一轮App逻辑在LVGL锁外停止worker。销毁先注销本应用indev回调，再释放抽屉与图片。

实际LVGL事件链表明，indev拦截RELEASED会跳过按钮自身的release；默认PRESS_LOCK按钮也不会因滑出而收到PRESS_LOST。因此有效手势先向原对象发送PRESS_LOST清除按下状态，再拦release及独立的SHORT_CLICKED/CLICKED，防止误触SAVE/HOME。真实指针回放覆盖SAVE底边和菜单底排；仅删除PRESS_LOST的隔离负控精确复现按下状态残留。

`eye_sequence.h`统一定义12拍、每拍1000ms：左4°、左8°、回中；右4°、右8°、回中；上2°、上4°、回中；下2°、下4°、回中。每次回中独占一秒，画面表情/组号/拍点使用同一表。实际姿态±8°/±4°窗口与动态硬限取交集，以可用窗口中点作为本轮neutral，并按半窗宽缩放目标；避免初始姿态紧贴下限导致一个方向完全不动，也不扩大原实际姿态的外包络。顺序为实际3.8秒视频完成→12秒动作→独立完整REST20秒。NFC与整段EyeRest互斥、距离采样持续等既有规则保留。

此前把主机控制器发出目标当作实体动作准备充分是不够的：生产`ScsServo::init()`调用`Servo::init()`，后者明确关扭矩；自动释放逻辑也会在静止时关扭矩。旧护眼路径从未开启。新控制器每个新拍在moveWithSpeed前开启双轴扭矩，重复tick、Intro预占、他人占用或丢锁不发开启命令；不全局关闭自动释放。硬件上的因果与实际位移仍须用反馈验证，不能仅凭目标日志宣布已转动。

seq51实机只测到1raw左右抖动和0上下位移，证明仅补扭矩不充分。独立编译真实UITK后验证：固定20ms软件步长在每秒只有少量写入时走不完一拍，再从真实姿态重新开始会损失剩余进度。护眼专用`moveWithDuration(...,350)`改为立即发完整目标和硬件时长，舵机自行插值；timed期间不让旧spring覆盖，但继续原堵转检查。普通move/stop取消timed，原厂应用继续使用原spring与固定dt；没有全局提速或关闭堵转保护。

`SC_HEAD` v1只在阶段/拍号/控制状态变化时输出已有事务缓存，读取`diagnostics()`不产生总线I/O。字段包括真实raw_position、position_reads、torque_before/after、独立位置/扭矩写ACK、写包duration_ms、协议错误与设备状态。SCS ACK=1不保证设备status=0，二者独立保留；无返回的raw=-1不替换成软件角度。记录发生在本帧StackChan.update前，不能把同一行target和旧write_ack硬配成同一事务，应依据计数递增和后续位置判断。扭矩寄存器40为单字节，修正原ReadWord为readByte，避免混入寄存器41。

守岸人pack改为实际320×240满幅裁剪，去掉原56px留黑；38帧/10fps/3.8秒、422,532字节，SHA256`5a67997f01f8be973bd1f3d9b0b05785c89bb78b227ed679751bcc5f364155ab`。现有FFmpeg两次重建逐字节一致，所有38帧无整行黑边；复用原JPEG worker，无新依赖。真实LVGL、主机测试、实体反馈与LCD实拍分别记录，当前设备部署结果见[DEVICE_DEPLOYMENT.md](DEVICE_DEPLOYMENT.md)。

## 护眼摇头的连续过渡（2026-11-02）

用户指出护眼摇头时表情与动作不连贯。此前seq53只证明实体位移与阶段时长，不能据此宣布动作流畅。实际生产View/Pet/LVGL按33ms采样显示：脸部容器在每秒换拍时直接跳到目标，990→1023ms跳4px、1980→2013ms跳8px；抵消容器平移后脸部区域像素差为0，证据`.tools/motion-flow-probe/baseline/manifest.json`。因此仅将Motion位置改为350ms smoothstep连续过渡、其余650ms保持；脸部与硬件共用`kMotionMoveMs`，保留原280ms情绪混合、12拍和完整REST20。

串口两种`readSCS`仅在没有成功读到字节时让出CPU，避免缓冲数据逐字节触发额外调度；超时、部分读取、CRC/ID/status与flush语义不变。原源码提取实验63组比较通过，证据`logs/scserial-read-source-probe.json`；正式测试先在旧实现准确失败，修改后CTest17/17通过，包含串口5组和内容计时8组。固定SDK为100Hz，`pdMS_TO_TICKS(1)`实际为0，表示重新调度，不能解释成每字节固定延迟10ms；实验注入的调度成本不能当实机收益。

新增被动`SC_FRAME`仅在LIVE的EyeRest Motion统计内容提交间隔、三处显示锁等待、前后head、StackChan与render耗时，每约2秒和阶段结束输出，输出在显示锁外。View内部仍可能节流，因此该计数不是实际绘制次数或LCD帧率。seq55保留旧画面用于基线采样，但两次175秒窗口均未进入LIVE、未发送测试配置，没有得到可比较的实机Motion数据。

修改后实际LVGL同样91帧/33ms采样的相邻容器最大位移8→1px，两处旧边界跳变消失，视觉95/pass；完整App回归8,105项断言、五项隔离负控、0裁切/缺字、两组各100轮生命周期增量0。176张静态帧中160张非Motion图逐字节一致。证据`.tools/motion-flow-probe/motion56-result.json`与`after/visual-verdict.json`；motion56是桌面迭代名称，没有部署OTA seq56。此证据仅覆盖渲染路径，实机调度与机械流畅度须分开验证。

## 两次往返与预备眼神（2026-11-02）

用户进一步确认“左→中→再左→中，短暂停；右、上、下各重复两次”，并要求动画曲线。参考[Apple ELEGNT](https://machinelearning.apple.com/research/elegnt-expressive-functional-movement)的表达性动作设计、[Takayama等人的机器人动画研究](https://wendyju.com/publications/151r-takayama.pdf)的预备与结果反应、[Disney动画原则](https://www.disneyanimation.com/process/animation/)的节奏与缓入缓出。下列数值为本项目设计选择，论文并未证明它们是本设备最优参数。

`eye_sequence.h`用16个带`start_ms/move_ms`的目标替代12个等长拍，删除等长常量。每组3秒，在120/720/1400/2100ms下发方向/中/方向/中，移动360/300/420/340ms，最后回中留560ms；两次偏移分别左右6°/8°、上下3°/4°，仍在原实际姿态±8°/±4°与动态硬限交集内缩放。首120ms仅眼神预备。共享索引保证View与控制器用同一时间表；迟到只发当前目标、不补播，回退不重发。取消、扭矩、协作锁、堵转检查、完整REST20与优先级保持。

眼神在每个机头目标前120ms开始并用smoothstep移向目标；脸部容器按该目标的独立移动时长使用同一类曲线。每组暂停2560–2780ms做一次闭眼再睁开。硬件仍使用已有定时目标包，不把屏幕曲线宣称为实测舵机加速度曲线。`EyeCue`默认中立，只在Ready叠加，Alert不受影响；普通调用使用默认参数。

首轮完整真实LVGL影片发现第三组Surprise在第二摆前到期，第四组末尾随机变Sad，视觉88/revise，失败证据保留`movie-v1/`。修复为仅Motion调用`Controller::update(...,hold_mood=true)`，保持该组情绪并抑制随机换脸，原280ms混合与细小动作继续；不每目标重播表情。第二轮300×40ms帧及60个精确采样点中，实际Controller情绪全部匹配四组，0越界/文字裁切，视觉94/pass，证据`.tools/expressive-motion-probe/movie-v2/`。主机CTest17/17通过（宠物18组、Policy27组、机头21组、生产Servo/Motion/UITK12组）。实机与完整App回归结果见部署记录。

## 修正左右基准与Motion显示开销（2026-11-02）

用户指出seq59实体机头四次均在同一侧。此前只以总跨度与目标包判定“physical_motion_verified”不充分，已明确撤回其方向通过含义。新启动记录给出yaw校准raw458；该轮起点raw499约为逻辑+12.8°，旧控制器以这个实际姿态为中点，全部左右目标仍在逻辑+4.8°到+20.8°，后续新鲜反馈raw477–518也全在校准正面的一侧。根代理独立原始帧审计`logs/device-expressive59-direction-failure-audit.json`保留，原自动报告不覆盖。

`RestHeadMotion`左右改围绕校准yaw=0，与动态限位及±8°相交；交集不能跨零时跳过，不再制造偏置neutral。首目标用700ms缓慢归正，初始偏转超过30°则跳过实体动作，俯仰仍保留原实际姿态±4°可行窗口，避免调用会越过pitch下限的双轴goHome。原四组3秒动作顺延700ms，总12.7秒、17个目标。正常开始后迟到仍只发当前目标，不追赶旧拍；首次tick必须不晚于820−700=120ms，超过则释自己的reservation并跳过，不能省略或截断归正。120/121ms、过期首次tick、旧运动和他人锁均有生产代码回归。

seq59实际内容提交仅45次/11,928ms≈3.77次每秒，44个间隔中43个≥100ms，最大354ms；显示锁等待共9,733ms，最大272ms。源码确认LVGL持全局锁执行刷新，Motion整脸scale232产生软件transform中间层；单320×20缓冲等待flush也在锁内。不能仅凭上述记录将全部等待归因于SPI或缩放。Motion改native256缩放，直接使用目标像素位置；每帧仅设最终字号/位置，删除先设普通208再改回256的重复布局。全局LVGL、SPI、缓冲和普通栏目不改。前后性能比较属于组合修改，不能当单因素实验或LCD帧率测量。

实际LVGL保留318帧/40ms（末帧20ms）的12.7秒影片与64个精确点，17个拍点、预备画面和四组情绪均通过94/pass；同帧布局优化前后影片/关键帧及181个App场景逐像素一致。完整App回归与最终实体两侧方向合同见`DEVICE_DEPLOYMENT.md`；绝不以屏幕smoothstep宣称物理舵机cubic加速度已测得。

## 机器人独立查询Codex周额度（2026-11-03）

用户明确要求电脑关闭后机器人仍能更新额度。此前用USB传输是实现选择，不能将其描述为硬件或账号数据必须依赖这台电脑。新的`apps/common/codex_weekly`在Mooncake/AI生命周期之前启动一条低优先后台任务，复用已固定IDF的Wi-Fi、SNTP、HTTPS证书bundle和HTTP server，不新增第三方依赖。机器人通过设备码流程独立登录，正常运行不调用电脑程序，也不读取或复制电脑的auth文件；USB只用于这次安装与验证。

协议依据官方`openai/codex`固定提交`e839b828fb4a2374b6b9ea9b0530f5a02ced0b95`：[`device_code_auth.rs`](https://github.com/openai/codex/blob/e839b828fb4a2374b6b9ea9b0530f5a02ced0b95/codex-rs/login/src/device_code_auth.rs)、[`oauth/client.rs`](https://github.com/openai/codex/blob/e839b828fb4a2374b6b9ea9b0530f5a02ced0b95/codex-rs/login/src/oauth/client.rs)、[`backend-client/client.rs`](https://github.com/openai/codex/blob/e839b828fb4a2374b6b9ea9b0530f5a02ced0b95/codex-rs/backend-client/src/client.rs)。设备码申请/轮询为JSON，授权码交换为表单编码，刷新为JSON；额度GET为`chatgpt.com/backend-api/wham/usage`，被动读取不启用Luna Reserve。这里复用的是官方实现细节，未发现对底层REST路由的稳定公开兼容承诺，后续上游变更须按固定来源重新核对。

仅从顶层`rate_limit.primary_window/secondary_window`选择唯一604800秒窗口；不固定认为secondary就是周，不取其它模型桶。严格解析32KiB以内JSON及字段类型；opaque access可用，未知access到期时间以0表示，不能强制假定所有access都是JWT。ID token只解码TLS可信响应的账号路由元数据，不把本地JWT解码当签名验证。首次登录必须含access；刷新响应的token字段缺失则继承旧值，显式空/错类型拒绝，且旋转refresh必须先保存再接受，防止丢失后续登录。

凭据仅refresh+账号路由放入`sc_codex`单个原子NVS blob，access/ID token留在RAM。原NVS分区只有16KiB，空间不足明确显示STORAGE并停止继续使用轮换结果；不擦除原设置为其腾空间。UI退出立即失效当前额度，实际NVS擦除由worker完成；擦除commit前突然掉电的窗口不在持久化退出保证内。HTTP禁止重定向并验证证书/主机名，响应受32KiB上限；10秒传输等待及body截止不等同包含DNS/慢响应头的整笔请求硬时限。

`CODEX`设置页的SIGN IN/REFRESH/FORGET只入队，网络和NVS不在LVGL锁内执行。真实App/LVGL新页748断言、原回归8598断言/五项隔离负控、207截图与三组100次生命周期测试通过，视觉95/pass。原181帧156张完全相同，其余25张只有新增CODEX抽屉按钮区域变化。桌面服务fake不构成真实登录、TLS或NVS掉电验证。

局域网诊断只有`GET /api/status`，端口8765、`X-Botifier-Key`鉴权；KEY在设备页显示，未初始化时拒绝。JSON与`SC_CODEX_STATE`日志逐字段选择，不包含token、账号标识、设备登录码或诊断KEY。HTTP服务不运行任意命令、不修改配置。原USB额度帧被忽略，避免残留电脑助手覆盖机器人所登录的账号；USB调试参数协议保留。

后台网络使用现有保存SSID及驱动重连，不抢占唯一WifiManager callback。StackChan板级StartNetwork在上游安装回调后，若station已经连接则补发正常Connected路径，消除AI重复启动网络后错误保留60秒配网定时器的回归；不修改上游子模块。真实设备部署、登录与独立查询结果另记于`DEVICE_DEPLOYMENT.md`。

首个独立版seq65已实机启动并读回VALID，但用户两次SIGN IN均失败。第二次只读采集到`getaddrinfo`错误202（SDK `EAI_FAIL`），继而连接失败、HTTP状态0；不能把Wi-Fi连接成功当作外部域名可用。电脑上的独立UDP实验中，热点网关192.168.137.1:53立即拒绝，1.1.1.1/8.8.8.8返回DNS成功；这不证明设备收到的具体DNS RCODE。

修复只补空BACKUP=1.1.1.1、空FALLBACK=8.8.8.8，保留MAIN及已有服务器。起初仅补FALLBACK的候选被源码复核否决：lwIP `dns.c:1136,1385–1401`在非零RCODE分支仅检查紧邻槽，BACKUP空时不会尝试FALLBACK；超时分支才跳过空槽。NOERROR但无可用记录仍不会切换服务器，此限制保留。当前SDK的DNS槽为全局共享，不能声称它仅作用于额度任务。响应数据预算在`HTTP_EVENT_ON_CONNECTED`后重新开始10秒，避免DNS重试/TLS握手耗时误耗响应预算；仍不是整笔请求硬超时。`SC_CODEX_HTTP`仅固定端点枚举及数字错误码，`SC_CODEX_DNS`仅三个服务器地址，不记录请求/响应内容或凭据。以上修复的实际效果须以新设备记录为准。

授权后的额度超时另有发送端问题。seq67重启实测刷新凭据HTTP200，但usage返回`ESP_ERR_HTTP_EAGAIN/0x7007`，约13秒未收到响应头。SDK默认TX仅512字节；`http_header_generate_string`无法拆分单个过长Authorization头，返回零已写长度，`esp_http_client_request_send`却停止发送并继续等待响应，最终请求头可能连终止空行都未发出。修复仅为usage按`access.size()+account.size()+512`设置TX大小，当前上界8960字节；OAuth POST仍默认512，RX/TLS/超时/响应上限不改。真实未修改的SDK生成器与字符串工具、从生产源码提取的TX赋值组成10例实验：假access1800/account36时旧路径只发送83/1967字节、缺终止，生产预算2348完整；8192/256时旧路径83/8579，生产8960完整。合成实验使用发送捕获模型，不能单独当实机网络结果；证据`.tools/http-header-budget-probe/`。

seq69实际保留登录，开机与独立OTA读回重启后均自动refresh200、usage200，真实额度62%，两次observed_at为1793678208、1793678263；后一次usage实际4436ms。idle额度同步51/51匹配。这里没有读取电脑凭据或重启PC同步助手。完整部署、定时刷新与测试边界见部署记录。

诊断还有一次明确纠错：初版探针把SDK合法的无HTTP响应状态`-1`当非法帧，错误漏采，曾据此推测后台没有输出；该判断被Wi-Fi诊断及修正后实测推翻。现在只额外接受`-1`、仍拒`-2`，成功判据仍严格HTTP200。探针30项合成测试通过，纠错留底`.tools/native-weekly-probe-tests/status-minus-one-correction.json`。调试复用已有设备备份中经过CRC校验的`sc_codex/debug_key`，只提取这一诊断值、不输出KEY；通过鉴权的Wi-Fi GET已实际读取62%状态，未提取或导出账号凭据，也未调用电脑额度接口。


## 本地 Wi-Fi 固件更新（2026-11-03）

复用现有8765端口与设备KEY，保留只读 `/api/status`，新增鉴权 `GET/POST /api/firmware`。GET只返回工程、MAC、ELF、运行/启动槽及OTA状态；POST接收原始app.bin，不下载任意URL、不执行远端命令。没有新增依赖或显示页面，额度登录/查询仍在原独立任务。

上传以MAC及当前ELF为条件，要求当前VALID、boot与running一致，只写inactive应用槽。前288字节核对芯片和工程，再通过4KiB堆缓冲写入并计算整包SHA256；SDK结束校验通过后才设boot。所有失败关闭HTTP会话，不清空NVS或修改启动槽；开始擦写会按SDK规则失效备用槽记录，不能声称所有otadata字节完全不变。begin部分分配失败要abort，end无论成功失败都消费句柄，不能重复abort。提交后即使响应发送失败也重启，客户端独立确认目标slot/ELF/VALID，不靠同版本号或发送字节数报告成功。

服务器每次recv等待5秒，收包/写入/最终验证间检查120秒总时限；SDK单次阻塞调用不能被该软时限中断，不声称绝对墙钟硬截止。客户端默认总确认180秒，限制固定局域网IPv4和8765，不跟重定向，密钥仅文件/环境读取且报告不含密钥。实际HTTPD解析需看到首个body回调才分派有body请求，拒绝路径不排空body：若对端提前塞入剩余payload，关闭会话可能以TCP RST体现；live preflight使用最小1字节启动解析，不能把最初探针的RST或header-only408当成功拒绝记录。

真实handler在host使用隔离HTTPD/鉴权/OTA/SHA平台fake，11组覆盖拒绝、部分分配清理、断流/超时/摘要/写入/结束错误以及成功提交顺序。它不证明硬件SHA或flash行为，最终以实机结果为准。完整CTest20/20与PC95/95通过。新增源文件首次构建需reconfigure扫描现有GLOB；完整成功日志为 `logs/wifi-ota-build71-reconfigure.log`，最初未重配的undefined-reference构建未用于部署。

初版两次无线上传未通过。第二次只发送131072字节就断线；只读串口捕获当前旧ota_0的headtouch栈底哨兵失败并主动abort，不能将最初“回到原槽”判断成已证实的新槽启动回滚。证据 `logs/device-wifi-ota-retry02-boot.log`、两次失败报告和 `logs/wifi-ota-stack-experiment.json`。SDK正常flash guard会暂停两核调度再关cache；仅凭PSRAM栈和canary失败，无法断言是cache访问或调用深度耗尽。

单变量修正版只将headtouch同样6144字节栈从SPIRAM改成INTERNAL|8BIT，不改size/周期/优先级/手势，也不改其它IMU任务。71与73源清单比对仅此文件不同。启动日志明确出现 `[HAL-HeadTouch] start update task`，排除静默分配失败造成任务消失；具体canary损坏机制仍待验证。后续实机通过范围见部署记录，不能以一次成功声称长期/断电稳定。

## BOTIFIER 头顶触摸与电量菜单（2026-11-03）

`app_distance/touch_reaction.h` 将一次 Press 编排为约3秒的 Notice→Respond→Wait→Settle：眼神先行120ms，实体双轴按100ms节流采样quintic曲线；首次从实测姿态接起，7秒内再次轻触采用亲昵变体并交替侧向，400ms内输入去抖。整段保留最初anchor，目标限制在校准yaw±8°、最初pitch±4°与实时限位的交集，不累积偏移。迟帧不补播历史节点；退出或高优先级出现只停止并释放自己取得的锁。现有协作bool锁仍不能识别帧间瞬间换主，没有扩大为全局锁重构。

App只订阅头顶Press，同次接触后续Swipe/Release不重触发。HAL回调只投递带代际atomic邮箱，LVGL/App线程统一操作表情与实体；忙时直接丢弃，不排队。用户后续明确TOO CLOSE只高于闲时栏目，因此Touch不再受距离Alert准入/取消限制，互动期间反复将既有不透明头像面板置于警告上方，结束后降回原层显露当前警告；不改采样、真实Alert或节目Context。每帧重申层级覆盖中途新建警告，并在第二次邮箱检查后绘制，覆盖同帧新Press/Alert。NFC、完整护眼仍按既有规则抢占；Touch不取得受保护的foreground animation slot，因此不延迟到期护眼。外置ToF不可用时仍能摸头。定向EyeCue期间暂停独立随机扫视/眨眼，情绪自身动画继续运行；其他默认表情路径保持原参数。

`Modifiable` 的局部interaction scope仅在BOTIFIER生命周期抑制旧HeadPet/IMU消费，清理旧HeadPet延迟回位，IMU只归还自己取得的锁。切换前用旧avatar完成清理，关闭后恢复先前scope，防止旧恢复路径覆盖新动作或空avatar访问。未改headtouch 50ms采样、任务栈、全局运动dt、原AI动作范围或省电设置。

菜单右侧补充 `BAT 75%`／充电时绿色 `75% CHG`，≤15%红色，读取失败 `BAT --`。新增 `Hal::getBatteryStatus` 通过既有Board缓存一次取得valid/level/charging/discharging，旧百分比helper的100%fallback没有改动，但新界面不使用这个fallback。鉴权只读 `/api/status` 新增power和reset_reason，供后续区分电量和复位；没有远程改参数或动作注入入口。数值是PMIC缓存读数，不是电池电压或健康度诊断。

最终Host CTest22/22：触摸控制器13组、legacy隔离15组、Pet19组等；`logs/host-tests-20261103-161042-393-test.log`。完整真实App+LVGL（硬件替身）触摸/电量1897项、原App/电量9334项通过，0截字/缺字/借用释放错误；100轮生命周期无控件/回调/锁残留。WAITING曾63/60px裁切，改64px后通过，最长充电文本71/88px；`.tools/touch-pet-probe/results.json`。图集经visual-verdict核验96/pass，`.omx/state/touch-pet/ralph-progress.json`。最终IDF5.5.4构建 `logs/firmware-20261103-161627.log` 通过；尝试绕过环境激活直接ninja时因PATH缺ccache失败，使用完整开发环境重建成功，不把那次失败计入通过。

研究参考：Reachy Mini统一动作控制与保留当前姿态（https://github.com/pollen-robotics/reachy_mini_conversation_app/blob/main/src/reachy_mini_conversation_app/moves.py），Disney互动注意力（https://la.disneyresearch.com/publication/realistic-and-interactive-robot-gaze/）。只借鉴编排思路，没有移植其多自由度轨迹或新增依赖。实际触摸准确率、实体流畅度与电池供电故障原因仍以设备证据为准。

优先级修正版保留旧版负控失败 `run-touch-priority-negative.log`（过近拒绝新触摸），随后完整App/LVGL验证 **3514项**通过：持续Alert中启动、中途Alert、20/100ms刷新边界的同帧Press+Alert、结束前恢复安全距离；逐帧核对真实红色像素、命令继续、Alert/Context未被伪造，结束按当前距离恢复。原App回归 **9334项**和Host **22/22**通过，日志为 `.tools/touch-pet-probe/run-{touch,full}-priority.log`、`logs/host-tests-20261103-164052-845-test.log`。三处生产文件为 `app_distance.cpp`、`pet_avatar.h/.cpp`，未增加全局屏蔽状态或传感器暂停逻辑。新图层的实际LVGL截图经visual-verdict **96/pass**，记录 `.omx/state/touch-priority/ralph-progress.json`；实机验收另记。

## 持续过近的低优先级摇头提醒（2026-11-03）

`alert_head_motion.h` 增加单独的低优先级控制器：第一次从实测yaw平滑回到校准正面，之后左约6°→回中→右约6°→回中，每段600ms，中间保持正面1200ms后持续循环。俯仰保持起始实测值，目标受yaw±8°与实际限位交集约束；100ms采样与前瞻，每次yaw变化最多1.8°，停顿不重复写舵机，迟帧不补播历史目标。保留现有红底/白字，未添加音频、设置项或依赖。

App仅在LIVE、有效Alert、菜单关闭且无摸头/NFC/完整护眼时启用。恢复距离、读数失效或退出会停止并释放自己的锁；限位异常/收紧或观察到失锁则中止本轮，不强行归正或重抢外部锁。抢占发生在高优先级动作取得锁之前；互动结束仍Alert时从当前姿态重新开始，不恢复旧目标。持续提醒不更新idle activity，因此不推迟到期护眼。

`Motion::stop()` 给硬件发送短暂保持命令，而SCS `ReadMove`可能仍为真；触摸接管时只对已证明属于自身AlertHead的刚停止动作放行这个短暂状态。`TouchReaction::trigger` 默认行为不变，外部修改锁始终拒绝；不把任意外部运动解释成可抢占。相应控制器测试由13增至14组，加入拥有权交接回归。

最终Host **23/23**通过（新AlertHead10组，Touch14组），日志 `logs/host-tests-20261103-172658-804-test.log`。真实App/LVGL硬件替身验证 **5892+11707项**通过，持续26秒177个目标/15次左右方向转换，红色提醒逐帧不变；安全/失效停止、摸头同帧交接、NFC、护眼三阶段及显式菜单退出通过，30轮Alert生命周期内存一致。证据 `.tools/touch-pet-probe/alert-motion-results.json`，硬件替身不证明实体位移/流畅度。初始护眼夹具在600ms回中结束前误期待有位移，已纠正并留说明；另保留旧锁断言失败完整日志，修正为区分归还护眼锁与恢复Alert锁，没有删除抢占检查。

首次嵌入式构建 `logs/firmware-20261103-172410.log` 因Xtensa的uint32_t与600u模板类型不同失败；只增加显式 `std::min<std::uint32_t>`，最终完整IDF构建 `logs/firmware-20261103-172709.log` 成功。没有以MSVC成功代替嵌入式构建。共享bool锁仍无法识别两次观察间完整发生的清锁/换主，此限制未扩大为全局锁重构。

## 分类提交与复验（2026-11-04）

固件按依赖分为 `b45dfbf`（独立账号与局域网更新）、`5753f9f`（摸头、电量与互动优先级）、`0347a09`（持续过近动作）。共享文件从已验证的75、77、78源码清单分阶段暂存，不改当前工作区；最终57份源文件与已部署78构建清单逐字节一致。提交保留既有测试夹具的空文件尾行，没有为整理历史而重写验证版本。

本日重新运行Host **23/23**、PC **95/95**及离线测距模拟，日志 `logs/classified-commit-host-20261104.log`、`logs/classified-commit-pc-20261104.log`。环境记录增加当前提交/树与构建清单的对应关系，设备实测时间及待验收项保留；本轮未操作机器人或将主机通过误写成实体动作验收。设备备份、KEY、日志、二进制及私有探针仍在忽略目录。
