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
