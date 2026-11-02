# 实机部署与验证

日期：2026-10-30。设备为 COM38 的 ESP32-S3 rev 0.2，16 MiB flash；Secure Boot、Flash Encryption 均未启用。用户已确认 ToF4M 接到红色 PORT A，窗口朝前。

## 原设备备份

完整备份保存在本机 `.tools/device-backups/20261030-124710/flash-original.bin`，共 16,777,216 字节。
分段读取通过设备 MD5，拼接后再次与设备整片 MD5 比较一致；分区表与独立 ROM 读取一致。

- SHA256：`9334bf7752c87f2faa0ea29005f067c9197dad25cc55d0ce57463b1d1c9ad3df`
- 设备整片 MD5：`b202384152810e7e18aacc558270eb4a`
- 分析记录：同目录 `inspection.json`、`partition-table.csv`。

备份包含设备配置和凭据，仅保存在 Git 忽略目录；迁移开发机器时应单独妥善保留。

实际分区表与本地构建一致：NVS `0x9000/0x4000`，otadata `0xd000/0x2000`，phy_init `0xf000/0x1000`，ota_0 `0x20000/0x4f0000`，ota_1 `0x510000/0x4f0000`，assets `0xa00000/0x400000`，coredump `0xe00000/0x10000`。

当前备份纠正了旧日志状态：原 ota_1 实际为有效的 **stack-chan 1.6.0 / ESP-IDF v5.5.4**，并非早先启动日志中的 1.5.1。ota_0 为 flappy_bird，启动记录 seq3 / ABORTED；ota_1 为 seq2 / VALID。两个镜像均通过 checksum 和追加 SHA256 校验。

## 资源兼容与部署范围

原资源与构建资源均为 42 项，40 项字节相同。另两项已离线核验：

- `index.json` 只改变数组顺序；表情按名称、额外文件排序后完全相同，字体和模型引用相同。
- `srmodels.bin` 都是 `wn9_histackchan_tts3`，内部三个文件逐字节相同，仅目录排列和偏移变化。加载器 `managed_components/espressif__esp-sr/src/model_path.c` 按文件名称与 offset 加载，不依赖目录序号。

| 模型文件 | 字节数 | 原/新共同 SHA256 |
| --- | ---: | --- |
| `_MODEL_INFO_` | 45 | `96b2615dc51d471796fd5da91330c33733a8ff45ee465692cc630fddbda4572a` |
| `wn9_data` | 289665 | `dbba353b4b5cb923984de753012671aa122cd4373739173956d2071ba9618eb3` |
| `wn9_index` | 1200 | `5810ee84beac8e73b4080a7b765e09900627ff973f2654811dd6a8557f0769b4` |

因此保留原 assets。首次部署镜像为子仓库 `84c6e47ce82469acb3b44b537c64da26a58577b8` 的距离提醒构建，3,741,584 字节，SHA256 `272b23202cc98049d62afe971ba037a67a8bf48422b0f1169b41d1b952bc0505`；本轮调参后的当前版本见下文。

仅写 app 到 ota_0 `0x20000`（擦除范围 `0x20000–0x3b1fff`），校验后再写 `0xd000` 的单个 4096 字节启动扇区，记录 seq5 / NEW / CRC `0xc8210fcd`。保留另一启动扇区、原厂 ota_1、bootloader、分区表、NVS、phy_init、assets。普通 `idf.py flash` 会额外写入这些分区，本次不使用。

部署脚本、原启动扇区及逐步摘要记录保存在上述备份目录及 `.tools/deploy_distance_firmware.py`；不作为可在任意设备上复用的通用刷写入口。

若需要回到保留的原厂槽，本机备份中的 `otadata-slot0-original.bin` 恰为原 `0xd000` 扇区。
核对仍是同一设备后，仅恢复这个扇区即可让另一条 seq2 / VALID 记录重新选择 ota_1，
无需改写应用、资源或 NVS；恢复后的重启与主菜单应再验收。完整恢复整片备份是另一个操作，不与选择原厂槽混用。

## 工具问题留底

原 esptool bulk 读取默认端口超时为 None，出现过无限等待。改用明确超时、有期限的分段读取；512 字节包失败时回退到 256 字节，再严格校验摘要，最终完整备份成功。实际丢包的底层原因尚未证明。

首次部署尝试在第一条写入前因重复 `run_stub()` 被拒绝，没有修改 flash。外部已运行 stub 交给 `esptool.main` 时需正确标记 `sync_stub_detected`，避免再次覆盖 RAM 工具；失败日志 `deployment.log` 保留，修正后的执行另记日志。

## 首次部署的上机结果

两次写入与 MD5 校验成功。启动前再次核对保护区域，与原备份一致：bootloader、分区表、NVS、phy_init、原厂 ota_1、第二 OTA 记录、assets 均未被刷写改变。运行固件可能正常更新设置，不能把这次部署前后的校验延伸为运行时永不写配置。

日志：`.tools/device-backups/20261030-124710/deployment-fixed.log`、同目录 `deployment.json`、`logs/device-distance-first-boot.log`。
HAL 首次报告 `ota confirm check: partition=ota_0, state=1`，随后请求 `marking current app valid`。
之后独立读回整个 otadata 并校验设备 MD5：sector0 为 seq5 / VALID / CRC 正确，sector1 为原 seq2 / VALID，且 sector1 全扇区与备份逐字节相同；记录在 `ota-confirmation.json`、`otadata-after-confirm.bin`。
再经 USB 硬件复位，仍收到有效 `SC_DISTANCE`，20 秒后 HAL 报告 `partition=ota_0, state=2`；日志 `logs/device-distance-reboot.log`。这是 USB 复位重启验证，未实际断开电源测试。

实际接线后 ToF 采样通过。使用现有电脑端 `parse_line` 对真实日志固定窗口离线解析：

| 项目 | 实测 |
| --- | --- |
| 窗口 | seq29–1044，设备 uptime4782–105912 ms |
| 解析与有效性 | 1016/1016 帧成功、有效，raw_status9，error0 |
| 内部缺帧/重复/回退 | 均为 0 |
| 采样间隔 | 中位 100 ms，平均 99.635 ms，范围 80–120 ms |
| 距离 | 中位 800 mm，范围 787–815 mm；未用量尺验证绝对精度 |
| 本机告警 | 此窗口全部 false，尚未覆盖靠近和移远 |

这证明真实 ToF 读取、USB 输出和电脑协议兼容；脸旁符号、跨菜单/AI、传感器拔插和语音唤醒尚未据此验收。

电脑程序也已使用真实 COM38 运行：`python -m distance_reminder --port COM38 --no-notify`。
控制台显示 `state=safe sensor=ranging robot_alert=off` 和实时约 800 mm 距离；按 P 进入 paused 时继续接收距离，再按 P 经 waiting 恢复 safe，按 Q 正常退出码 0 并释放串口。此次关闭桌面通知，不据此声称实际近距通知弹窗通过。

用户随后确认“可以的，有效果”，并反馈感应等待太长、符号不明显。该反馈确认第一版可用，不能代替后续新版视觉验收。

## 第二次调参：更快、更明显

按用户实测反馈，将机器人持续判定从 5 秒缩短到 1 秒，保留三样本中值和 48 cm 恢复迟滞。
在约 10 Hz 采样下，从新出现的近距输入到告警，含滤波预计约 1.1–1.2 秒；精确现场时延仍需日志中有实际近距边沿才能确认。
电脑通知仍使用独立的默认 5 秒，不借本次修改改变其规则。

原 40 px 角落图标改成水平居中、眼睛上方的 80×80 px 红图，并加 8 px 红边框及顶部 “TOO CLOSE”。
提示容器不接收点击和滚动；原 Home 手势、底部语音气泡保持原区域。解除只删除提示图层，不恢复旧 emotion，因此不会覆盖期间收到的新表情。
这是源码坐标核对；任意远程自定义眼嘴位置可能与提示重叠，未做截图对照验收。

固件子仓库提交 `e24db6d`；二进制 3,742,576 字节，SHA256 `f8637727a7990dd1cef68f35bd8a349193a94ea40d8db4dbf23dc880c6dbaeed`。
源码变更为 `distance_sensor/proximity_policy.h`（默认时长）、`stackchan/stackchan.cpp`（独立提示层）、`stackchan/avatar/decorators/angry.cpp` 与同目录 `decorators.h`（复用缩放接口），以及 `tests/proximity_policy_test.cpp`（默认时序与原场景回归）；路径相对 firmware/main，测试相对 firmware。未改变电脑规则或原 AI 表情数据。
最新构建日志 `logs/firmware-20261030-141414.log`；CTest 3/3，规则 17 组，包含默认 999 ms 不触发、1000 ms 触发和移远中值恢复。

上一版可用镜像另存 `20261030-124710/distance-first-working.bin`。本次刷写前再次备份当前前 128 KiB（含运行后的 NVS 和启动记录），保存在 `.tools/device-backups/20261030-141710-faster-warning/`，原完整备份继续保留。
仍只写 ota_0 和启动扇区 0；新记录 seq7 / NEW，保留原厂槽和另一 VALID 记录，逐段校验当前配置及原资源。
部署日志 `logs/device-update-faster-warning.log`；两次写入、当前 NVS/bootloader/分区表/原厂槽/资源的保护摘要均通过。
独立读回新版启动记录为 seq7 / VALID / CRC 正确，sector1 全扇区与原记录相同；同目录 `ota-confirmation.json` 可核对。USB 重启后的日志 `logs/device-faster-warning-reboot.log` 仍有有效测距，HAL 确认 `ota_0, state=2`。

新版采集 `logs/device-faster-warning.log` 共 1203 帧（seq194–1396），全部有效、无序号缺帧，采样间隔中位 100 ms；汇总 `logs/device-faster-warning-summary.json`。
实际记录到 4 次触发和 4 次解除。四次从中值近距成立到 `robot_alert=true` 均为 **1000 ms**，从首次远距读数到两个样本的中值恢复均约 **100 ms**。

| 周期 | 近距判定成立 uptime(ms) | 告警开启 | 告警解除 |
| --- | ---: | ---: | ---: |
| 1 | 38402 | 39402 | 62142 |
| 2 | 96242 | 97242 | 102122 |
| 3 | 103022 | 104022 | 105022 |
| 4 | 106522 | 107522 | 120982 |

这些是传感器与本机规则的真实时延，不包含肉眼观察或 LCD 渲染延迟；新版红框、文字与图标的实际观感尚待用户反馈。

## 第三次部署：DISTANCE 调试入口

用户希望直接查看距离，以排查感觉触发不灵敏的问题。新增内置 Mooncake 页面 **DISTANCE**，紫色图标，追加在既有 App 后以保持旧 appID；右上角固定 **HOME** 按钮返回。源码入口 `apps/app_distance/app_distance.cpp`，只读展示原始距离、中值、近距状态和计时、当前阈值、样本年龄及错误，不改变采样与规则。

采样任务在短临界区发布完整 `Snapshot`，页面每 100 ms 复制一次；UI 不访问 I²C、不推进判定。
超过 1500 ms 没有新发布的数据，快照读取会在副本上清除距离和计时，显示 STALE / --。
原始无效光学读数仍保留用于诊断，并明确标记 INVALID。原有表情告警继续读取既有新鲜度判断。

为让串口日志能和屏幕交叉核对，SC_DISTANCE v1 新增可选 `filtered_mm`、`near_ms`、`proximity_state`，与屏幕来自同一份采样状态；现有 PC 解析器兼容。新增页面、service.h/.cpp、App 注册、主机测试及 CMake 为本次固件范围；电脑端只追加协议兼容测试。

源码提交 `3a64d7c82ed56ae160150a49abd3ee341e3262b5`；固件 3,751,744 字节，SHA256 `19b63305e95315708e83ab29e2ef8369dea49351634f07660f3f1f041deb62e8`。
构建日志 `logs/firmware-distance-debug-20261030-143830.log`；CTest **4/4**，含新增快照 **8 组**（过期边界、副本不变、无效值、自定义超时和时间回绕），原 17 组规则继续通过；PC **57 项**通过。

上一版另存 `20261030-141710-faster-warning/distance-second-working.bin`。当前前 128 KiB 配置与 OTA 备份、刷写和确认记录在 `.tools/device-backups/20261030-144309-distance-debug/`。
仍只写 ota_0 和启动扇区 0；新记录 seq9 / NEW，所有保护区域摘要通过。独立读回 seq9 / VALID / CRC 正确，另一 seq2 / VALID 扇区保持原值。部署日志 `logs/device-update-distance-debug.log`。

刷写后日志 `logs/device-distance-debug.log` 固定采集到 **903 个连续有效帧**（seq317–1219），采样间隔中位 **100 ms**、无缺帧，PC 解析拒绝数 0。
新 `filtered_mm` 与独立三样本中值计算逐帧一致，`proximity_state` 和 `robot_alert` 一致；此窗口原始距离 960–1001 mm，未出现近距边沿，不能当作触发时延测试。
汇总 `logs/device-distance-debug-summary.json`；USB 重启日志 `logs/device-distance-debug-reboot.log` 仍有有效测距，HAL 确认 `ota_0, state=2`。

刷写前旧版 25 秒窗口有 250 个连续有效帧、277–545 mm，未见采样停顿；首行有前次端口关闭留下的旧半帧拼接，已从统计中排除，不据此推断传感器停顿。
用户随后确认“可以的，很灵敏”，说明距离页面可用且读数跟随符合其现场感受；没有单独确认 HOME 触摸返回。用户推测此前表情显示不及时，目前没有 LCD 时延测量来确定该解释。

## 第四次部署：动态表情与实时调参

按用户要求，DISTANCE 默认进入 LIVE：复用 DefaultAvatar，只增加本页面拥有的 Blink/Breath 表情动画；底部固定 HOME/TUNE。切到 TUNE 可直接看 F（中值）/R（原始）cm、状态和有效性，三组 40 px 高加减按钮调整 TRIP cm、CLEAR +cm、WAIT sec，LIVE 返回验证表情。

机器人默认等待从 1000 ms 改为 0 ms，中值低于 400 mm 当帧判定 Alert，恢复仍为 480 mm；保留三样本滤波和 1500 ms 断流清除。这是去掉额外的持续等待，不能据此声称光学采样或 LCD 零延迟。

请求参数和快照在短临界区复制，policy 仍只由独立采样任务操作。每帧先应用参数，再处理新的有效/无效读数；真正修改时清除旧 latch 和计时，但保留仍新鲜的中值窗口，相同参数不重置。
快照记录同一帧实际参数；SC_DISTANCE v1 新增可选 threshold_mm、hysteresis_mm、hold_ms，用于和屏幕交叉核对，原电脑端兼容。

SAVE 请求由 app task 在 LVGL 锁内摘取、锁外写 NVS，锁内显示结果。只有显式 SAVE 才保存 sc_distance/config 单个版本化 blob；开机校验尺寸、版本和范围后恢复，错误或缺键使用默认。保存期间有新编辑会提示 NEWER EDITS，不将后续编辑误标为纯 SAVED。

独立审查发现原 Avatar 基类没有虚析构，而 StackChan 通过 unique_ptr<Avatar> 持有派生脸；反复切页会使派生屏幕面板无法正确释放。因此仅增加 virtual ~Avatar() = default，未扩展其它脸部功能。现有 LVGL Object wrapper 对已由父容器删除的子对象有 isValid/release 防重删路径。
新增实际 Avatar 头的生命周期回归：100 次 reset 与 100 次替换检查派生 RAII、五官、气泡和装饰资源释放；隔离移除虚析构后两组均运行失败，恢复后 2/2。该主机测试不包含真实 LCD 或 LVGL 分配器，不替代现场切页验收。

最终固件 3,755,584 字节，SHA256 `2263d163805563ce04587427b5bcdff970d4c2b17fa2669110c695925bfb4cbe`；构建日志 `logs/firmware-20261030-164246.log`。CTest **5/5**（规则30组、快照9组、生命周期2组，另有原驱动11组与运动），日志 `logs/host-tests-20261030-164441-926-test.log`；电脑端57项与模拟通过，日志 `logs/pc-distance-live-tests.log`。

上一版可用镜像已归档到 `20261030-144309-distance-debug/distance-third-working.bin`。本次配置前缀/启动记录备份和部署记录在 `.tools/device-backups/20261030-164605-distance-live/`；只写 ota_0 与启动扇区0，新记录 seq11 / NEW；原厂槽、当前配置、bootloader、分区表、资源及另一启动扇区保护摘要均通过。部署日志 `logs/device-update-distance-live.log`。

独立读回 OTA 记录为 **seq11 / VALID / CRC 正确**，sector1 原 seq2 / VALID 全扇区不变；记录在同备份目录的 ota-confirmation.json。再次 USB 重启后，日志 `logs/device-distance-live-reboot.log` 返回 **301/301 有效帧**、无序号缺帧，HAL 确认 `ota_0, state=2`。

首轮真实记录 `logs/device-distance-live.log` 为 seq149–1050 共 **902 连续帧**，892 有效、10 无效，PC 拒绝数0，无序号缺帧；采样间隔90–110 ms、平均99.767 ms。每个可独立计算的中值与上报一致，规则边界检查无不一致。
出现两次近距开启：seq279（中值232 mm）和seq669（中值356 mm），均在进入 Alert 当帧 `near_ms=0 / hold_ms=0`，没有额外1秒等待。
seq287 因无效测量清除；seq672 中值483 mm 达到恢复阈值后解除。首行旧半帧与 boot 日志拼接，已排除，未计作协议帧。
汇总 `logs/device-distance-live-summary.json`；这些是采样与判断证据，不包含 LCD 显示时延。此窗口参数始终400/80/0，尚未记录现场加减或 SAVE，因此未声称编辑参数后的保存/重启往返已实测。

固件提交 `2698e9bac05914a22a84185f6b6214d276bf6332`；本次改动文件为 apps/app_distance/app_distance.h/.cpp、distance_sensor/proximity_policy.h 与 service.h/.cpp、stackchan/avatar/avatar/avatar.h，以及 tests/CMakeLists.txt、proximity_policy_test.cpp、distance_snapshot_test.cpp、avatar_lifetime_test.cpp、专用 fakes/avatar_ui/。路径相对 firmware/main，测试路径相对 firmware。
复用原脸、动画、图标、字体和 NVS，无新增依赖；两屏取代单一只读页，继续复用原告警图层。现场 LIVE/TUNE/HOME 触摸、表情观感、编辑后 SAVE/重启恢复及完整 AI 回归仍未独立验收。

## 第五次部署：电子宠物情绪与眼神动作

按用户选择的电子宠物风格，将 DISTANCE LIVE 的单纯眨眼/呼吸替换为 11 状态的宠物脸：平静、好奇、开心、撒娇、害羞、委屈、困倦、惊讶、单眼眨、严肃提醒和放松。开心有笑眼与 8px 弹跳，撒娇有 12px 侧摆和腮红，委屈有垂眼、小弯嘴和泪滴。普通动作以 280ms 平滑转场穿插眼神停留、回看和不等间隔眨眼。

告警信号变化直接抢占动作并绕过 33ms 动画节流；有效距离恢复时短暂放松，无效/过期数据不庆祝。采样、阈值、TUNE/SAVE、NVS 格式和 SC_DISTANCE 均沿用上一版。仅 DISTANCE LIVE 换脸，其它 App 保留原厂脸和既有全局提醒。删除该页面单独持有的 Blink/Breath modifier，合并为一个动作时间表；图元继续复用 LVGL，无新依赖。

实际 LVGL 桌面渲染揭示初版笑眼与嘴部的双轮廓，已改成先压扁再弯曲的笑眼和一条变形嘴线，并增加委屈/严肃的上睑斜切。最终 11 姿态与动画无采样边界裁切，420 帧正常刷新与强制整屏重绘像素一致；100 次创建/销毁后子对象、LVGL 可用内存和分配块数不变，完整性检查通过。图集与限制见 [Face Lab](demos/face-lab/README.md)；这些是实际生产代码的桌面渲染，不是 LCD 实拍，未含完整控件和警告红框。

主机测试 CTest **6/6**，新增动作 **13 组**覆盖 64 组转场、告警即时优先、无效数据恢复、100,000 帧边界/有限值与时间回绕。日志 `logs/host-tests-20261030-182110-194-test.log`。完整 ESP-IDF 构建通过，最终 **3,762,976 字节**，SHA256 `527756678afd18f61865d1f25d87d24643b04e4375999c7d8c839587b42b50c3`，分区余量约 27%；日志 `logs/firmware-20261030-182654.log`。

更新备份目录 `.tools/device-backups/20261030-182853-pet-face/`；前一版镜像已在 `20261030-164605-distance-live/distance-live-working.bin` 校验留存。部署前核对 COM38、MAC、上一版 app SHA 和 seq11/VALID，只写 ota_0 与 `0xd000` 启动扇区0。设备摘要核验当前 NVS、bootloader、分区表、另一启动扇区、原厂 ota_1 和资源不变；部署日志 `logs/device-update-pet-face.log`。最终镜像归档为本次目录内 `distance-pet-working.bin`。

独立读回记录为 **seq13 / VALID / CRC 正确**，原 seq2/VALID 全扇区不变，见同备份目录 `ota-confirmation.json`。正常重启后 HAL 确认 **ota_0 / state=2**，`logs/device-pet-face-reboot.log` 有 **301/301** 连续有效样本（seq178–478），无序号缺帧、协议拒绝、中值或规则不一致。

首轮 `logs/device-pet-face.log` 的 60 秒窗口有 **602/602** 连续有效样本（seq138–739），原始距离 145–591mm，采样间隔 90–110ms、平均 99.734ms；每个可复算中值和判定均一致，协议拒绝与缺帧为 0。7 次进入 Alert，全部 `near_ms=0 / hold_ms=0`；6 次达到恢复距离后解除，窗口结束时处于另一次提醒。首行旧半帧与启动片段拼接已排除。汇总分别为 `logs/device-pet-face-summary.json` 与 `logs/device-pet-face-reboot-summary.json`。

固件本地提交 `e3d1e91ce1258be9ea7d140b0b11e29c5d3f9e3d`。改动为 `apps/app_distance/app_distance.cpp`、新增 `pet_avatar.h/.cpp` 与 `pet_motion.h`，以及 `tests/CMakeLists.txt`、`pet_motion_test.cpp`。本次采样证据不包含 LCD 延迟或用户对表情的最终观感；触摸、编辑后 SAVE/重启往返和完整 AI 回归仍未独立验收。

## 第六次部署：响指候选与摄像头辅助（2026-10-31）

用户连接 USB 后，重新枚举 COM38，核对 MAC、此前电子宠物镜像摘要和 seq13/VALID。初次部署 `e8cc8ec` 的 3,776,496 字节镜像，SHA256 `c9fd38285fd433febdba4fee19f5bd31adab0a8bb7a231a3935ea1a5c2b31cfc`。备份目录 `.tools/device-backups/20261031-132642-snap-sensing/`，部署日志 `logs/device-update-snap.log`；仍仅写 ota_0 与启动扇区0，seq15/NEW，原厂槽、当前配置、bootloader、分区表、另一启动扇区与资源摘要均通过。

**该初版未通过实机验收**：用户报告进入 DISTANCE 就重启，`logs/device-snap.log` 捕获两次相同崩溃。`SC_SNAP` 输出在 `visual_age` 处出现字面 `li`，随后 `LoadProhibited`。旧 ELF 摘要以 `f4010ed2e` 开头，与崩溃记录一致；解码 `logs/device-snap-crash-decoded.log` 指向 `nano-vfprintf_i.c` → `printf` → `snap_service.cpp:134` 的 `emit_telemetry`。

本机 `sdkconfig` 启用 `CONFIG_LIBC_NEWLIB_NANO_FORMAT=y`，ESP-IDF 5.5.4 `components/newlib/Kconfig:110` 明确说明不支持 64 位整数格式。新日志错误使用 `PRIi64`，破坏后续可变参数解析；桌面完整 printf 与固件编译没有暴露这一运行时差异。已将仅需 -1 哨兵与新鲜帧龄的字段改为 `int` / `%d`，保留现有 nano 配置，不扩大为全局 libc 替换。不能将此前 CTest/桌面检查当作实机可用结论。

修复提交 `8b11962020be3c249afaefe360922b90849e0a20`，完整构建日志 `logs/firmware-20261031-133112.log`；最终镜像 **3,776,464 字节**，SHA256 `6e75aae593da35c926d331d5984212852d29117eeaf38ecd7a0b95c59667fbb0`。先核对当前 seq15/VALID 及初版镜像摘要，再写入 ota_0 和启动扇区0，seq17/NEW；部署记录 `.tools/device-backups/20261031-133312-snap-printf-fix/` 与 `logs/device-update-snap-fixed.log`。再次验证当前配置、bootloader、分区表、原厂槽、资源和另一启动扇区不变；最终镜像归档为该目录内 `snap-sensing-deployed.bin`。

先采集 25 秒启动与测距日志，再独立进入 ROM 读回启动记录：**seq17 / VALID / CRC 正确**，原 seq2/VALID 扇区全字节保留。证据为该目录 `ota-confirmation.json`。随后正常重启，`logs/device-snap-fixed.log` 的 HAL 确认 `ota_0 / state=2`。

修复后的 150 秒窗口有 **1506 个连续 SC_DISTANCE 帧**（seq69–1574），776 有效、730 明确无效；没有序号缺帧、协议拒绝、中值或规则不一致，也没有 panic/assert 标记。不能将无效帧算作有效测距。白名单审计汇总为 `logs/device-snap-fixed-audit.json`，审计器的 11 组合成输入检查通过。

**本窗口未观察到修复后 DISTANCE 启动，也没有 SC_SNAP 帧**。因此只确认了新版写入、启动记录、重启与距离通路；修复后的入口回归、MIC/CAM 状态、实际响指响应和误触发率仍等待用户现场操作，未声称感知功能已通过实机验收。初次失败日志与译码记录保留，不用无崩溃的菜单窗口替代进入 App 的验证。

## 第七次部署：响指播放黑底 KURO GAMES 片头

按用户确认的黑底 Logo 版本，从[库洛 PGR 官网](https://pgr.kurogame.net/)公开的 **Aeon Reforged** 宣传片截取原始 0–3.4 秒。保留淡入淡出；该区间静音，没有添加人声或音轨。中心裁切到 4:3 后缩放为 320×240，12 fps、41 张 baseline JPEG；包 176,092 字节，SHA256 `ebb2f97d8e294b1571f711d9b9477269eac9a97e495808915563accf1bc873f3`。来源与转换参数保存在固件 `apps/app_distance/media/source.json`，根目录 `scripts/Prepare-KuroIntro.py` 可用已有 ffmpeg 重建；重复生成逐字节相同，41 帧均独立解码成功。

一次新鲜 Accepted 同步占用播放器，独立任务按时间解码并跳过落后帧，不排队；最多持有两张 RGB565 图。播放期间同步暂停本功能的输入并作废相机旧会话，结束、取消或错误后清空旧候选并恢复预热；累计计数与用户配置保留。完成基线阻断播放前间隙的事件补播。全屏黑底覆盖原控件但保留 HOME，新的距离 Alert 会中断片头。App 关闭先停感知再停播放器，所有阻塞等待在 LVGL 锁外。仅使用现有 JPEG 解码与 app 分区，不修改输出音频路径、不新增依赖。

固件提交 `4ebe86a`；应用 **3,958,176 字节**，SHA256 `2db3b2c8e43727f6c92ed2c935adcd0b0ed3686d346bc5da892eb913056ebd8f`，最小应用分区剩余约 24%。完整构建 `logs/firmware-kuro-intro.log`；主机 CTest **8/8**（新增包解析 6 组），电脑端 **57 项与模拟**通过。静态独立审查未发现锁反转或图像引用失效；底层 checked 输入关闭和 JPEG 解码没有已证明的总时限。

真实生产 View、LVGL 9.4、字体和 `LvglAllocatedImage` 的桌面验证：12 张截图、**36 项行为/生命周期及 304 项边界检查**通过，文字裁切 0，6 次图像释放中仍被 LVGL 借用的次数 0。覆盖 busy 期间连续事件不重启/排队、完成基线、恢复后新事件、空帧快照保持图像、错误/取消、Alert 与 HOME；旧时钟顺序反例按预期失败。证据 `.tools/intro-ui-probe/manifest.json`、同目录 `frames/contact.png`、`logs/intro-render-run.log`。感知和播放器为替身，未纳入全局红色告警装饰；这些结果不代表真实 LCD、触摸、JPEG 解码速度或识别准确率。

重新核对 COM38、MAC、当前 seq17/VALID 与上一版 app SHA 后更新。备份与当前镜像归档 `.tools/device-backups/20261031-141300-kuro-intro/`，文件 `kuro-intro-deployed.bin`；部署日志 `logs/device-update-kuro-intro.log`。只写 ota_0 与 `0xd000` 单个启动扇区，新记录 seq19/NEW；当前 NVS、bootloader、分区表、原厂 ota_1、原资源和另一启动扇区摘要保护校验均通过。

25 秒启动后独立读回 **seq19 / VALID / CRC 正确**，原 seq2/VALID 扇区全字节不变，见同目录 `ota-confirmation.json`；随后 USB 重启再采集 `logs/device-kuro-intro.log`。实际进入 DISTANCE、识别触发与连续响指播放行为的现场结果另据该日志与用户反馈记录，不从菜单窗口推断通过。

用户确认“播一次，结束后恢复；播放中响指无影响”。150 秒窗口记录 5 次 request→playing→finished，均 error0/cancelled0；512 条 SC_SNAP 均可解析，最后恢复 listening/audio_open/ready。SC_DISTANCE 为 seq41–1543 共 1503 条，其中 1501 有效、2 明确无效，无缺帧、中值或规则不一致，没有 panic/assert。原始日志和 `logs/device-kuro-intro-audit.json` 保留。

审计仍为 `attention_required`：每次恢复输入都有一次 SDK 的 `i2s_channel_disable: the channel has not been enabled yet`，共 5 条，不能把本窗口写成“零错误日志”。源码闭环为：checked close 已关闭并核验 RX；恢复时 `esp_codec_dev_open()` 先调用厂商 `audio_codec_data_i2s.c` 的 `set_fmt`，其再次无条件 disable RX 且忽略该返回值，随后重新配置并 enable；checked open 再核验 RX 已启用。实际随后恢复采音，未因这条日志改动 codec、掩盖日志或提前启 RX。这一解释仅限该条重复关闭记录。

### 后续灵敏度反馈与证据

用户随后反馈第一次灵敏，之后往往没反应、间隔一段时间又能触发；宠物表情变化让其难以区分识别和播放。新的 180 秒 `logs/device-kuro-intro-responsiveness.log` 有 876 条 SC_SNAP，候选与接受仅各增加 1、融合拒绝计数不变；该一次接受有完整播放周期，摄像头处理样本增加 1325。同期 1805/1805 个距离样本有效、连续，规则与中值复算一致。这支持当前问题主要出在融合前的声音过滤，而非已接受事件被播放器漏掉。

按 `uptime_ms-event_age_ms` 去重并排除采集前旧记录，得到 34 条以 tonal 结束的短瞬态：edge0.0461–0.773 全部低于固定 0.8，crest2.514–11.332 均通过原 2.5；duration10–60ms。因此这些记录由 edge 条件过滤。没有逐次声源标注或原始 PCM，不能将 34 条全部认定为响指，也不能以重复上报的 event_reason 次数计算识别率。另 6 条 long_sound 是保留观测，不等于 6 次独立声音。SENS 只调幅度门槛，不改变原固定 edge 门槛。

### seq21：短衰减瞬态与明确就绪提示

源码提交 `217f37a` 保留原 broadband 分支，另允许 duration≤30ms / edge≥0.45 / crest≥3.0 的短高峰瞬态，原幅度、保护与摄像头规则保持。8 个连续衰减合成夹具（含跨块）在旧规则全拒、新规则全过，4 个边界负控仍拒；原 12 组保留，检测器 14 组、CTest8/8，日志 `logs/host-tests-20261031-142121-166-test.log`。短敲击和衰减铃声也可能通过，合成测试不代表现场识别率。

LIVE 顶部增加 READY、WARMING、SOUND FILTERED 和画面/距离状态，日常宠物动画不作为识别反馈。首次桌面验证抓到 Alert 首帧标签未隐藏，已改为每帧跟随告警显隐、文字仍100ms刷新。真实 View 最终 57 行为与761边界检查通过、0裁切，6次释放无借用违规；证据 `.tools/intro-ui-probe/responsive-manifest.json` 与 `responsive-frames/contact.png`，原证据存入 `baseline-intro/`。

完整构建 `logs/firmware-kuro-snap-response.log`；镜像 **3,958,976 字节**，SHA256 `e22df798f9002fab7fcbf8277fbaf8c750a2e69d3a59408a55944a84bd0a29ac`。核对上一版 app 和 seq19/VALID 后，仅写 ota_0 与启动扇区0，部署日志 `logs/device-update-snap-response.log`；备份与归档 `.tools/device-backups/20261031-142411-snap-response/snap-response-deployed.bin`。保护区域摘要校验通过，独立读回 **seq21/VALID、CRC正确**，原 seq2/VALID 全扇区保留。重启后以 `logs/device-snap-response.log` 采集现场复测，未用上一版单次成功代替修正版灵敏度验收。

**seq21 仍未通过识别验收。** 用户按 READY 测试三次仅成功 1–2 次，之后明确报告敲键盘持续触发。`logs/device-keyboard-false-positive.log` 中 accepted 从15增至25，camera_samples 从926增至1997；配置始终 SENS3、CAM ON。原采集文件名 `device-snap-sens5.log` 不代表实际调到5，不能写成 SENS5 的测试结果。`logs/device-snap-response.log` 中一次播放取消与有效 ToF Alert 同时发生，属于距离提醒抢占，不能据此解释所有漏识别。

现有摄像头门槛仅判断亮度局部运动，没有手检测；此前声音规则放宽并不能解决键盘误触发。已复位回主菜单停止反复播放，记录 `logs/device-snap-reset-menu.log`。用户随后明确授权新增乐鑫官方 ESP-DL 与 hand_detect，继续实现“镜头前举手＋短声音”；模型只输出手框，不识别响指姿态，现场准确率须重新验收。

### seq23：官方手检测与一次举手授权

固件提交 `374a7a4e6e27893b195a8518e9d60ff02ddbc8d4`，新增精确锁定的 hand_detect0.2.0 和 ESP-DL3.3.0，其它依赖版本保持。生产相机路径改为真实 RGB888 手框检测，区域内的大手稳定后才授权一次声音互动；持续有手不能重复授权，接受或1.8秒到期后要求移开再举手。取帧前丢弃排队旧帧，取帧加推理统一受500ms新鲜度预算约束。模型输出手框，不识别响指姿态；几何区域也不等于键盘分类器。

默认 O3 构建超槽935,328字节，未刷入；仅将新增 ESP-DL 组件改为大小优化，并关闭其不用的像素转换，未改 managed 源码或分区布局。最终构建 `logs/hand-model-build-size2.log` 通过，应用 **5,032,096 字节**、SHA256 `23926da2e211574131b0e4422da21944df221659e2c7f2b2a62a575ad53acec9`，应用槽余145,248字节（约3%）。主机CTest10/10、实际View86行为/1367边界检查通过；测试输入和硬件识别分开记录，见 `DEVELOPMENT.md`。

更新前核对 COM38、MAC7c:4f:ad:af:61:f0、seq21/VALID和旧镜像摘要；只写 ota_0 和启动扇区0。`logs/device-update-hand-detection.log` 记录当前NVS、bootloader、分区表、原厂槽、资源与另一启动扇区的保护摘要通过。备份目录 `.tools/device-backups/20261031-154932-hand-detection/`，归档 `hand-detection-deployed.bin`、匹配 ELF 和 map。

启动25秒采集 `logs/device-hand-boot.log` 后，独立 ROM 读回 **seq23/VALID、CRC正确**，原seq2/VALID扇区逐字节保留，证据同目录 `ota-confirmation.json` 及 `logs/device-hand-ota-verify.log`。随后复位回菜单，开始 `logs/device-hand-detection.log` 现场采集；模型加载、推理耗时、举手响应和正常键盘负样本仍须结合该窗口与用户操作确认，不能从菜单启动或主机合成测试推断通过。

该180秒窗口实际记录 **1804/1804 有效 SC_DISTANCE**（seq670–2473），没有序号缺帧、中值或规则复算不一致，未见 panic/assert 或 SDK 错误标记。**SC_SNAP 为0，未观察到模型激活**；`logs/device-hand-detection-audit.json` 正确标记 `not_observed`。这只能补充测距与启动证据，不能据此宣称手检测、推理耗时、响指成功率或键盘负样本已通过。现场测试问题已发出，等待用户实际打开 DISTANCE 并操作。

### seq23 实机故障：SNAP CAMERA ERROR

用户进入后报告 CAMERA ERROR。20秒采集 `logs/device-hand-camera-error.log` 包含98条手检测快照，均 `camera_error=263`（`ESP_ERR_TIMEOUT`）、audio listening/error0、camera_samples0。它们是轮询快照，不能当作98次独立推理。当前源码中该超时出现在取帧和推理返回后的统一500ms预算检查；具体哪段耗时仍未知。旧实现仅在成功发布时更新 `hand_inference_ms`，因此失败帧留下的0ms不能解释为真实推理速度。

诊断提交 `1250d3a` 保留时效门槛，改为在拒绝帧中保留取帧和推理耗时，每5秒最多打印一次包含任务栈余量的32位整数诊断。构建 `logs/hand-timing-build.log` 通过；镜像5,032,480字节，SHA256 `37317e86b0adff3477f3a664a9694571166efca169b43d659739b7a6f1d1e206`。这是定位更新，尚未解决超时，不将编译通过写为故障修复。

本次归档 `.tools/device-backups/20261031-160657-hand-timing/hand-timing-deployed.bin` 与匹配ELF；部署日志 `logs/device-update-hand-timing.log` 验证当前设置和原厂保护区域不变。25秒启动采集后独立读回 **seq25/VALID、CRC正确**，原seq2/VALID全扇区保留，见同目录 `ota-confirmation.json`。首次150秒 `logs/device-hand-timing.log` 仍无 SC_SNAP，审计 `not_observed`；没有获得分段耗时。现有远程接口没有打开内置App的命令，warm reboot索引仅选中菜单图标，仍需要用户点击DISTANCE才能继续实测。

### seq25 实际激活后的超时基线

用户进入 DISTANCE 后仍显示 CAMERA ERROR。35秒 `logs/device-hand-timing-active.log` 中172个SC_SNAP轮询全部为camera_error263，camera_samples保持0；39组连续变化的耗时值中，取帧87–286ms、中位143ms，推理577–1277ms、中位670ms，合计715–1462ms。轮询会重复同一结果，39组变化值也不等于39次独立推理。6条限频拒帧日志的实测总耗时789–1200ms，底层error全部为0，确认推理成功后被500ms新鲜度门槛拒绝。分析见 `logs/hand-timing-baseline.json`，不能据此计算识别准确率。

### seq27：手检测 CPU 调度单变量对照

提交 `82185aea25c3671fbd56e7798abf0bcafacde0eb` 只将 priority1 的 snap-camera 从CPU1移到CPU0。源码 `hal.cpp:200` 与 `stackchan_display.cc:123` 表明CPU1上已有priority3的表情和LVGL任务；模型默认SINGLE_CORE，摄像头接口没有CPU1专属约束。CPU0上的priority3音频仍可抢占推理。调度竞争是待实测的性能假设，模型参数、内存分配、识别门槛与500ms有效期均保持，不把编译通过写成修复成功。

`logs/hand-camera-core0-build.log` 编译链接与槽检查通过，主机CTest10/10通过（`logs/hand-camera-core0-host.log`）。镜像5,032,480字节，SHA256 `8a998727043ca7c3dc493d32e986d34e3eb17b6656f838bff4823734b05be268`，应用槽余144,864字节。归档 `.tools/device-backups/20261031-162745-hand-camera-core0/` 含镜像、匹配ELF和map。

部署 `logs/device-update-hand-camera-core0.log` 验证旧镜像身份并只更新ota_0与启动扇区0，当前NVS、原厂槽、资源、bootloader、分区表和原启动扇区1的保护摘要全部通过。25秒启动采集后独立读回 **seq27/VALID、CRC正确**，原seq2/VALID扇区逐字节保留，见归档 `ota-confirmation.json`。已开始 `logs/device-hand-camera-core0.log`，等待进入DISTANCE后的实际耗时与手检测状态；超时改善、键盘反例与举手正例尚未确认。

用户随后报告空镜头仍为ERROR。完成的seq27采集共482个SC_SNAP轮询，其中10个是初始化零耗时快照，其余472个全部camera_error263，没有Ready帧。排除初始化后，97组连续变化的耗时值中，检测流程672–1046ms、中位806ms，合计848–1212ms、中位968ms；17条限频拒帧日志全部底层error0。见 `logs/hand-core0-timing-summary.json`。换核没有解决超时；没有CPU占用对照证据，不能将先前关于CPU1表情竞争的假设写成根因。

### seq29：局部优化仍超时，随后切换 NFC

`ac43a385fd7663813d805e965b3f004df2c06dfb` 将两个卷积实现文件恢复为O3，其余72个ESP-DL C++单元仍为Os，另增加32位格式的pre/model/post分段日志。镜像5,039,248字节，SHA256 `b17cd70a1038497e1d1784ec571098f2722c7c9219dd3741677e6fda0b56af4d`；归档 `.tools/device-backups/20261031-163805-hand-hot-conv/`，独立读回seq29/VALID、CRC和原扇区保护通过。

用户实测空镜头仍为ERROR。`logs/device-hand-hot-conv.log` 首批分段样本为pre25–27ms/model825–842ms/post1–2ms；完成采集中的28条限频拒帧日志均底层error0，合计884–1136ms，见 `logs/hand-hot-conv-timing-summary.json`。不能将局部优化写为有效提速。

后续完整O3配合7算子注册裁剪在本地构建为4,856,224字节、SHA256 `d1b8566df6bc5bd2d31217073269ee14192d051a2575e89e752316f5a42bc63b`，静态检查见 `logs/hand-graph-o3-review.json`，**未刷入或实测**。用户改用随附NFC卡并明确允许删除handdetect；该实验的镜像、ELF和构建源片段归档 `.tools/hand-graph-o3-research/`，没有继续部署。

### NFC 替代实现与验证范围

用户明确授权引入四个M5Stack官方库并移除手检测。四库固定Git提交，项目侧处理IDF5.5.4组件名称大小写映射；60个保留组件的版本/hash/依赖无漂移，managed源码与官方提交一致。见 `logs/nfc-dependency-verification.json`。删除生产手模型、响指服务、hand_detect和ESP-DL依赖，片头、距离提醒与宠物表情保留。

读卡使用机身ST25R3916、共享I2C总线0x50，仅执行NFC-A UID选择（4/7/10字节）和易失RF状态切换，不写卡片存储。任一传输/选择错误不算移卡；候选无卡须RF关场/开场各实际等待至少10ms后再次确认无响应。服务在播放/告警/无效距离时关闭触发许可，结束后须至少两次有效无卡且跨度达到300ms才能重新触发。错误后约1秒重新初始化，不把错误当作有效移卡。

主机CTest10/10通过，NFC门控11组包含1000次生命周期；实际生产View的LVGL探针875断言（88行为/787几何）、0裁切、6次图像释放/0借用中释放、heap完整性通过。证据 `logs/nfc-host.log`、`.tools/intro-ui-probe/nfc-manifest.json`，视觉判定96/pass已归档 `.omx/state/nfc-ui/ralph-progress.json`。硬件读卡、持卡去重和移卡重触发仍待实机；主机输入为模拟数据。

### NFC 构建就绪，USB 中断导致尚未部署

源码提交 `d3014aab3c78646aeb95e5db550a31343a10262e`。完整固件构建2473/2473步骤通过，日志 `logs/nfc-firmware-build.log`；镜像 **4,025,936字节**、SHA256 `f337d8c73581b0272f9f9649cd133b8d1070fcb48069a4fada954b9cabe87e30`，应用槽保持5,177,344字节，剩余1,151,408字节（约22%）。候选镜像、匹配ELF/map及身份记录归档 `.tools/firmware-artifacts/20261031-nfc-trigger/`，文件名 `nfc-trigger-candidate.bin`；明确标记 `Flashed=false`。

17:16尝试更新时COM38已不在Windows设备枚举中；`logs/device-update-nfc-trigger.log` 显示工具在 `port.open()` 即报找不到端口，尚未读取或写入设备。空的备份目标 `.tools/device-backups/20261031-171610-nfc-trigger/` 不是成功部署记录。最后已核验设备状态仍为 **seq29/VALID**，没有seq31更新或NFC硬件验收结果。

接回USB数据线后，重新核验设备身份与旧镜像，使用新的备份目录完成受保护更新，再读回OTA状态。现场验收包括进入DISTANCE后的初始化、随附卡片读取、持卡不重复、移卡重触发、播放期间刷卡无影响及ToF告警抢占。日志审计工具 `.tools/audit_nfc_log.py` 的12组合成自检通过，但这不替代物理刷卡验证。

### 2026-11-02：NFC 固件部署为 seq31/VALID

用户接回USB后，Windows重新枚举COM38、VID303A/PID1001、序列号7C:4F:AD:AF:61:F0；更新器再通过ROM核验MAC、旧seq29/VALID、旧app摘要和原厂保护区域。继续使用此前已构建的 `d3014aa` 镜像，4,025,936字节、SHA256 `f337d8c73581b0272f9f9649cd133b8d1070fcb48069a4fada954b9cabe87e30`，未重新构建或修改固件代码。

备份与部署归档 `.tools/device-backups/20261102-093531-nfc-trigger/` 包含更新前启动记录及配置、`nfc-trigger-deployed.bin`、匹配ELF和map。`logs/device-update-nfc-trigger-20261102.log`（另存为 `logs/device-update-nfc.log`）记录三阶段保护校验通过：只写ota_0应用和0xd000启动扇区0，当前NVS、bootloader、分区表、原厂ota_1、资源和启动扇区1保留。

更新后采集25秒启动日志 `logs/device-nfc-boot.log`，随后独立进入ROM读回两启动扇区：**seq31/VALID、CRC正确**，原seq2/VALID扇区逐字节不变，见归档内 `ota-confirmation.json` 和 `logs/device-nfc-ota.log`。验证完成后复位并开始180秒 `logs/device-nfc.log` 采集；启动日志已有持续有效距离读数。NFC初始化、随附卡片兼容性、持卡去重、移卡重触发和播放期间忽略仍须以实际进入DISTANCE后的现场结果为准。

用户进入DISTANCE后确认“播一次，持卡不重复，移开可再播”。完成的180秒采集有97个SC_NFC轮询快照，实际观测到1种UID，accepted从0增至3；3组accepted→playing→finished均error0/cancelled0。NFC初始化与本次随附卡片读出已有实机证据，持卡去重和移卡重触发另有用户现场确认；轮询快照数不等于独立刷卡次数，不据此计算识别率。

同窗口SC_DISTANCE为seq178–1985，**1808/1808有效**，无缺帧、中值或规则复算不一致，未见panic/assert/watchdog。审计 `logs/device-nfc-audit.json` 仍标记 **attention_required**：读卡errors累计增加9次，其中3个轮询保留error=-1（ESP_FAIL），另有一次无法确认RF关场的警告，最终读卡状态恢复ready/error0。当前返回码不能区分具体失败步骤，错误根因未定位；不能将这次基本功能验收写成“全程零错误”或长期稳定性通过。播放中刷卡忽略、ToF/HOME抢占的独立现场试验与精确响应延迟仍未单独标注验收。
