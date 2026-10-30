# StackChan 两个免刷写演示

1. **表情灯光秀**：中性、开心、眨眼，配合变色灯光、左右各 8° 与小幅点头。
2. **专注提醒**：电脑倒计时 10 秒，结束后机器人亮暖色灯、改变表情并点头。
   这是电脑计时触发，不是 ToF 距离检测。

在机器人主菜单打开 **DANCE**，暂时退出手机 App 的蓝牙控制页面，保持机器人在 DANCE 内。
然后双击 `1-Showcase.cmd` 或 `2-Timer.cmd`。只扫描匹配 StackChan 的蓝牙广播；
找到多个设备时会停止，需明确指定地址。无需配网改动或刷写固件。

```powershell
# 两个 demo 连续运行
.\Run-Demo.ps1 -Demo Both -Seconds 10

# 只扫描，不连接、不发送命令
.\Run-Demo.ps1 -Demo Scan

# 只输出演示命令，不访问蓝牙、不等待倒计时
.\Run-Demo.ps1 -Demo Both -Seconds 3 -DryRun

# 按扫描结果选择特定设备；不要把 Wi-Fi MAC 当成蓝牙 MAC
.\Run-Demo.ps1 -Demo Showcase -Address '扫描得到的12位地址'
```

首次动作的绝对目标为 yaw 0°、pitch 45°，随后 yaw 在 ±8° 内、pitch 在 41°～49° 内变化。
最后回到上述中位、恢复中性表情并关闭演示灯光；不读取或保存运行前姿态。
Ctrl+C 会请求取消并尝试回中位；蓝牙断开时无法保证恢复成功。

编译使用已经安装的 VS2026、Windows SDK 和 C++/WinRT，不安装额外依赖。
`build/stackchan-demos/Release/stackchan_demo.exe` 是程序；运行日志在项目 `logs/demo-*.log`。

## 验证边界

- Windows 主机编译已通过。
- `Verify-Demos.py` 检查实际程序 dry-run 的 JSON、字段范围、结束姿态及非法参数，完全不访问蓝牙。
- 尚未验证机器人现有 1.5.1 固件上的连接、表情和动作效果；只有实际运行后才能补充此结论。
- `GATT_OK` 表示蓝牙写操作获确认，不代表已观测到画面变化或电机到位。
- 应用中心下载的独立 App 可能不提供 DANCE 服务，需先回到原厂主菜单。

协议依据：本地官方源码 `app_dance.cpp`、`hal/utils/bleprph/bleprph.h`、
`stackchan/json/json_helper.cpp` 和 `avatar/skins/default/eyes.cpp`。
眼睛权重为 0～100，角度为 0.1°；不使用 PWM `rotate` 字段。
每条完整 JSON 通过 GATT WriteWithResponse 发送，最大 512 字节。
Microsoft [GATT 写入接口](https://learn.microsoft.com/en-us/uwp/api/windows.devices.bluetooth.genericattributeprofile.gattcharacteristic.writevaluewithresultasync)
和 [ATT/MTU 处理说明](https://learn.microsoft.com/en-us/uwp/api/windows.devices.bluetooth.genericattributeprofile.gattsession.maxpdusize)。
