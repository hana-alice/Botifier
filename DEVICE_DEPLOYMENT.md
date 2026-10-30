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

因此保留原 assets。已部署镜像为子仓库 `84c6e47ce82469acb3b44b537c64da26a58577b8` 的距离提醒构建，3,741,584 字节，SHA256 `272b23202cc98049d62afe971ba037a67a8bf48422b0f1169b41d1b952bc0505`。

仅写 app 到 ota_0 `0x20000`（擦除范围 `0x20000–0x3b1fff`），校验后再写 `0xd000` 的单个 4096 字节启动扇区，记录 seq5 / NEW / CRC `0xc8210fcd`。保留另一启动扇区、原厂 ota_1、bootloader、分区表、NVS、phy_init、assets。普通 `idf.py flash` 会额外写入这些分区，本次不使用。

部署脚本、原启动扇区及逐步摘要记录保存在上述备份目录及 `.tools/deploy_distance_firmware.py`；不作为可在任意设备上复用的通用刷写入口。

若需要回到保留的原厂槽，本机备份中的 `otadata-slot0-original.bin` 恰为原 `0xd000` 扇区。
核对仍是同一设备后，仅恢复这个扇区即可让另一条 seq2 / VALID 记录重新选择 ota_1，
无需改写应用、资源或 NVS；恢复后的重启与主菜单应再验收。完整恢复整片备份是另一个操作，不与选择原厂槽混用。

## 工具问题留底

原 esptool bulk 读取默认端口超时为 None，出现过无限等待。改用明确超时、有期限的分段读取；512 字节包失败时回退到 256 字节，再严格校验摘要，最终完整备份成功。实际丢包的底层原因尚未证明。

首次部署尝试在第一条写入前因重复 `run_stub()` 被拒绝，没有修改 flash。外部已运行 stub 交给 `esptool.main` 时需正确标记 `sync_stub_detected`，避免再次覆盖 RAM 工具；失败日志 `deployment.log` 保留，修正后的执行另记日志。

## 上机结果

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

现场下一步：打开原厂 DANCE（不是应用市场下载项），手掌放在 ToF 前约 20–30 cm 持续至少 7 秒，再移到 50 cm 外；观察脸旁生气符号出现和解除，并同步核对 `robot_alert`。本次采集窗口没有出现近距输入，视觉验收仍待用户操作。
