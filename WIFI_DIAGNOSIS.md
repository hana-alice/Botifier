# 电脑热点配网排查

更新：2026-10-30。**用户确认更换为 AX300 后连接成功，当前配网阻塞已解除，本轮排查结束。**

## 最终处理结果

用户最新反馈：“换了个ax300 好了”。成功结果来自用户反馈，本次未重新操作机器人或采集连接日志。
后续开发使用当前已连通的网络，不再继续修改原 AX900 网卡或热点设置。
AX300 的具体 SKU、芯片、驱动和实际发射频段未核验，不作补充推断。

更换网卡后成功支持原 AX900 热点路径存在兼容性问题的判断，
但不能据此区分硬件、驱动或热点配置的具体原因，也不能证明原热点实际发射在 5 GHz。
以下保留原 AX900 的排查、驱动测试及回退记录，不表示当前仍连接失败。

## 已确认事实

- 原 AX900 测试期间用户报告：StackChan 能连接 iPhone 17 Pro Max 开启“最大兼容性”的个人热点，不能连接该网卡提供的电脑热点。
- 用户截图：电脑热点名称 `<HOTSPOT_SSID>`；编辑窗口仅有名称和密码，没有频段选项。
  用户确认 App 手填 SSID 完全相同，无首尾空格。本文及准备文件不保存密码。
- 用户重试后报告 `wifi reason 201`。IDF 5.5.4 的
  `components/esp_wifi/include/esp_wifi_types_generic.h:166` 定义
  `WIFI_REASON_NO_AP_FOUND = 201`。这是未找到目标 AP；该错误码不能证明密码错误，也不能证明密码正确。
  此码来自用户的本次反馈，串口采集窗口没有捕获到相应失败事件，不应描述成已采集到的设备日志。
- 原 AX900 网卡：`UGREEN Wi-Fi 6 USB Adapter`，USB `VID_368B&PID_8D88`，当时接口 `WLAN 2`。
- 排查开始时的驱动：AIC `6.40.60.210`，2024-09-12；
  `C:\Windows\INF\oem104.inf` 明确包含该硬件 ID。
  用户批准测试后已更新为 `14.46.40.664`，2026-02-02，绑定 `oem16.inf`。
- 电脑主连接为有线以太网；热点使用 Microsoft Wi-Fi Direct Virtual Adapter #4，
  本地地址 `<ROBOT_HOST>/24`。测试时 API 回报连接客户端数为 0。
- 初始 WinRT 热点配置为 `Auto`、`Wpa2`；Native Wi-Fi
  `WlanQueryInterface(wlan_intf_opcode_channel_number)` 对热点虚拟接口返回信道 44，属于 5 GHz 信道。
  这是驱动接口回报，尚无第二台独立扫描设备确认实际无线发射频段。
- 后续独立核对 Native Wi-Fi 查询：同一 Wi-Fi Direct 热点接口同时回报
  `PHYType=5`（802.11b）和 `channel=44`，这两项不相容；当前操作模式为 `0x20`（Wi-Fi Direct GO）。
  新的实时连接质量查询返回 `ERROR_NOT_SUPPORTED=50`，旧 Hosted Network 查询为 Idle，
  均不能补足现代移动热点的实际发射频率证据。**不能把信道 44 当作空口实测结果。**

## 已做实验及结果

| 操作 | 配置/接口结果 | 实际连接结果 |
| --- | --- | --- |
| 原热点 | Auto / WPA2，驱动信道 44 | 用户连接失败 |
| WinRT 持久配置请求 2.4 GHz，停止并重启热点 | API 成功，读回 TwoPointFourGigahertz；驱动仍信道 44 | 用户重试仍失败 |
| 重启 WLAN 2 网卡 | Windows 返回 Access is denied | 没有执行成功 |
| 用户拔插网卡 | 网卡 PnP 状态 OK；一度只剩蓝牙共享选项，配置查询 E_FAIL | 重新打开设置后 Wi-Fi 选项恢复 |
| 新建配置恢复原 Auto，保留用户截图中的名称/密码 | 配置查询恢复正常，驱动仍信道 44 | 没有据此宣称修复 |
| Win11 会话级配置请求 2.4 GHz / WPA2 | StartTetheringAsync 成功，读回 2.4；驱动仍信道 44 | 用户本次确认失败，reason 201 |
| 用户批准后安装新 Wi-Fi 驱动，再启动 2.4 GHz / WPA2 会话 | 绑定 14.46.40.664 / oem16.inf，设备错误码 0；热点 On；PHY 11b / 信道 44 矛盾仍存在 | 按用户要求未操作机器人；更新后的连接结果未知 |
| 用户更换为 AX300 | 本次未额外查询或修改配置 | 用户确认连接成功；当前配网阻塞解除 |

会话级配置根据 Microsoft 文档不修改持久配置；持久配置曾恢复为 Auto。
当前会话的配置值不能作为实际发射频段已改变的证明。

证据文件：

- `logs/hotspot-before.json`
- `logs/hotspot-after-settled.json`
- `logs/hotspot-after-replug-settled.json`
- `logs/hotspot-restored-auto.json`
- `logs/hotspot-session-2g4.log`
- `logs/hotspot-after-session-2g4.json`
- `logs/wlan-events.json`、`logs/wlan-after-replug.json`

## 原 AX900 的判断与限制

可确认失败表现是机器人找不到电脑的目标热点，问题尚未进入已证实的 DHCP/云端阶段。
结合正确 SSID、机器人仅支持 2.4 GHz、电脑请求 2.4 GHz 后仍失败，
网卡的热点兼容性值得继续排查。但驱动的 PHY/信道回报自身矛盾，
**“真实发射在 5 GHz”“驱动忽略 2.4 GHz”均未证实**。
需要独立接收端扫描或更新驱动后的对照结果，才能进一步定位。

更正留底：排查早期将“驱动回报信道 44”直接说成“原因已定位”，结论过早；
也曾要求用户在热点编辑窗口选择 2.4 GHz，而用户截图证明该窗口没有此选项。
本轮又查出 PHY/信道矛盾，进一步降低该信道值的证据强度。
后续应直接核对接口/截图，以设置读回成功或驱动单项回报代替无线行为验证会重犯同一问题。

## 驱动包与下一步

用户补充外壳标识为 `AX900-wifi6`；它是系列/速率标识，不能单凭它区分全部硬件变体。
本轮已直接解开安装器并按 `USB\VID_368B&PID_8D88`、INF、SYS 与目录签名核对，未运行任何安装器。

| 官网包 | Windows x64 INF 版本 | 匹配本机硬件 ID | 结论 |
| --- | --- | --- | --- |
| [绿联 CM762-35264 V1.4](https://www.lulian.cn/download/135.html) | 2024-05-08 / 6.40.60.192 | 否 | 版本更旧，排除 |
| [绿联 CM763-35265 V1.4](https://www.lulian.cn/download/154.html) | 2024-09-12 / 6.40.60.210 | 是 | SYS 与当前安装文件 SHA256 完全相同，重装不会更新版本 |
| [腾达 U11 Pro V1.0.0.25](https://www.tenda.com.cn/website/service/download/787881267925061?proPath=U11Pro) | 2026-02-02 / 14.46.40.664 | 是，设备名直接写 UGREEN Wi-Fi 6 USB Adapter | 用户批准后已安装，热点启动成功，API 报告的矛盾未改善 |

绿联页面日期和外层包版本不等于实际 INF 版本。两个绿联包分别存于
`.tools/ugreen-driver/`、`.tools/ugreen-cm763-driver/`。
腾达官方 API 返回的包已保存在 `.tools/tenda-driver/`，来源及哈希、签名核验见其 `audit.json`。
新 Wi-Fi CAT 的 Microsoft Windows Hardware Compatibility Publisher 签名有效，
SYS 嵌入签名有效；`signtool verify /kp /c` 对 SYS 和 INF 的目录成员验证通过。
这些证明文件来源/完整性及硬件声明，**不证明修复热点，也不保证闭源驱动的运行期兼容性**。

旧/新 INF 的五项高级属性相同，没有新增可选的频段/信道控件。
`IEEE11abgMode` 各枚举都保留 802.11a，不能将其解释为强制 2.4 GHz。
新 INF 增加无 UI 的 `DisplayMaxSpeed=0`、`ScanOptimize=1`，语义和热点影响未验证；
不写入猜测的 CountryCode/ChannelPlan 等注册表参数。

新 Wi-Fi INF 自身只复制 `aicusbwifi.sys`、安装对应内核服务，使用系统 `netvwifibus.inf`，
没有声明依赖 AicWifiService 或 aicloadfw；可作为单独 Wi-Fi PnP 包提交。
现有 AicWifiService 位于 Tenda 目录，不能仅凭路径断言旧驱动装错，也没有为此停止/替换服务。

旧驱动 INF/CAT/SYS 已从 DriverStore 复制至 `.tools/wifi-driver-backup/6.40.60.210/`，
逐文件 SHA256 与源文件核对一致，清单见 `backup.json`。
用户已明确选择“测试新驱动”。经 Windows UAC 提权后，仅提交已核验的 Wi-Fi INF：

```powershell
pnputil /add-driver "F:\stackchan\.tools\tenda-driver\extracted\app\win10_x64\aicusbwifi.Inf" /install
```

该命令实际返回 0，新包发布为 `oem16.inf`。随后通过三项独立主机接口确认已生效：

- PnP `DEVPKEY_Device_DriverVersion=14.46.40.664`、`DriverInfPath=oem16.inf`、`ProblemCode=0`。
- `Get-NetAdapter` 报告版本 `14.46.40.664`、日期 2026-02-02。
- 系统 `aicusbwifi.sys` SHA256 与新包一致：`7FC5AFB9D73380DA898DC3B42CD81A16986F6BEE7F5D6E3FB90F9F77B3A84951`。

安装结束的即时 `Win32_PnPSignedDriver` 查询仍短时回报旧版本，导致一次性安装脚本的原始
`install-result.json` 将后置验证记为 False；该文件保留原状。
后续直接 PnP 属性、NetAdapter 和 SYS 哈希已交叉纠正此判断，见
`.tools/tenda-driver/install-verification.json`。脚本已改为查询直接 PnP 属性，未重复安装。

PnPUtil 还将同包匹配的历史、当前不在场的 `USB\VID_2604&PID_0014\20220127` 标记为重装；
SetupAPI 明确记录为 non-present。没有更新或重启有线网卡。

驱动切换关闭了热点，已用原有凭据重新启动 2.4 GHz / WPA2 会话，API 返回 Success/On，
客户端数 0。有线网卡仍 Up / 2.5 Gbps。
更新前后 Native WLAN 的 PHY/信道/操作模式及错误码逐项一致，
因此**没有证据称此次驱动更新修复了频段报告或机器人连接问题**。
用户要求暂不动机器人，本轮没有串口访问、配网或连接重试。

更新证据：`logs/wifi-driver-after-update.json`、`logs/hotspot-new-driver-session-2g4.json`、
`logs/hotspot-new-driver-running.json`，以及 `.tools/tenda-driver/wlan-reporting-after-update.json`。
若将来继续调查原 AX900 的真实发射频段，需要另一台设备独立扫描目标热点；上述电脑回报不够。
当前已通过更换 AX300 解决配网阻塞，不将此调查作为后续开发的前置条件。

若需回退，在设备管理器为这张 UGREEN 网卡选回 6.40.60.210；原驱动包保持在 DriverStore，另有本地备份。
不要删除旧包，也不要凭未知的新 OEM 编号执行驱动删除命令。

当前普通权限会话已经实测无法重启网卡，管理员角色检查也为 False；
驱动安装需要 Windows 管理员授权，不能把未执行的操作写成已完成。
本轮在用户批准后更新了 USB Wi-Fi 驱动并重启热点，没有接触机器人或更改有线上网配置。

可重复运行 `scripts/Get-HotspotStatus.ps1`，同时记录配置值、信道查询结果和错误。

官方依据：[StackChan 2.4 GHz 规格](https://docs.m5stack.com/zh_CN/StackChan)、
[Native Wi-Fi 信道查询](https://learn.microsoft.com/en-us/windows/win32/api/wlanapi/ne-wlanapi-wlan_intf_opcode)、
[Windows 会话级热点配置](https://learn.microsoft.com/en-us/uwp/api/windows.networking.networkoperators.networkoperatortetheringsessionaccesspointconfiguration)。
