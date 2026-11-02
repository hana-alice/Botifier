# StackChan 开发准备

更新：2026-10-31。已完整阅读 `STACKCHAN_HANDOFF.txt`，沿用 StackChan + ToF4M 方案。

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
| 设备 | COM38，USB VID 303A / PID 1001，同一 MAC；用户于 2026-10-31 接回 USB，最新启动记录 seq17/VALID |
| C++ 主机测试 | 7/7 通过；驱动 11 组、规则 30 组、快照 9 组、生命周期 2 组、宠物动作 15 组、响指候选与运动融合 12 组；后两者使用合成输入 |
| 电脑端测试 | 57 项通过；包含新增调试字段的兼容测试；无硬件模拟产生 1 次电脑端提醒 |
| 当前固件构建 | 新增响指候选与摄像头运动辅助，修复 nano printf 的 64 位格式崩溃后已刷入；当前与历史产物分开记录于 environment.json |
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

`shorekeeper_art.cpp`从应用槽嵌入614,400字节原始六帧160×160 BGRA，静态descriptor直接引用flash，尺寸/索引先校验；不新增线程、解码器或逐帧RGB分配。动画时钟与护眼计时独立，切换模式恢复黑底和原宠物布局；PNG原图与生成提示、pack/hash/透明度数据均保留在media目录。转换脚本只用标准库和已有FFmpeg，不新增Python依赖。

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
