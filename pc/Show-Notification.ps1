param(
    [Parameter(Mandatory = $true)]
    [ValidateRange(40, 4000)]
    [int]$DistanceMm,
    [switch]$Simulated,
    [switch]$Sound
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

# Native NIIF_NOSOUND is required: NotifyIcon.ShowBalloonTip has no sound option.
# https://learn.microsoft.com/windows/win32/api/shellapi/ns-shellapi-notifyicondataw
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class StackChanBalloon
{
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct NotifyIconData
    {
        public uint cbSize;
        public IntPtr hWnd;
        public uint uID;
        public uint uFlags;
        public uint uCallbackMessage;
        public IntPtr hIcon;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] public string szTip;
        public uint dwState;
        public uint dwStateMask;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)] public string szInfo;
        public uint uTimeoutOrVersion;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)] public string szInfoTitle;
        public uint dwInfoFlags;
        public Guid guidItem;
        public IntPtr hBalloonIcon;
    }

    [DllImport("shell32.dll", CharSet = CharSet.Unicode, EntryPoint = "Shell_NotifyIconW")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool ShellNotifyIcon(uint message, ref NotifyIconData data);

    private static NotifyIconData Create(IntPtr window)
    {
        NotifyIconData data = new NotifyIconData();
        data.cbSize = (uint)Marshal.SizeOf(typeof(NotifyIconData));
        data.hWnd = window;
        data.uID = 1;
        data.szTip = "StackChan 距离提醒";
        data.szInfo = "";
        data.szInfoTitle = "";
        return data;
    }

    public static bool Add(IntPtr window, IntPtr icon)
    {
        NotifyIconData data = Create(window);
        data.uFlags = 0x02 | 0x04; // NIF_ICON | NIF_TIP
        data.hIcon = icon;
        return ShellNotifyIcon(0, ref data); // NIM_ADD
    }

    public static bool Show(IntPtr window, string title, string message, bool sound)
    {
        NotifyIconData data = Create(window);
        data.uFlags = 0x10; // NIF_INFO
        data.szInfoTitle = title;
        data.szInfo = message;
        data.uTimeoutOrVersion = 10000;
        data.dwInfoFlags = 0x01u | (sound ? 0u : 0x10u); // NIIF_INFO | NIIF_NOSOUND
        return ShellNotifyIcon(1, ref data); // NIM_MODIFY
    }

    public static bool Delete(IntPtr window)
    {
        NotifyIconData data = Create(window);
        return ShellNotifyIcon(2, ref data); // NIM_DELETE
    }
}
'@

$window = $null
$context = $null
$timer = $null
$added = $false
try {
    $window = New-Object System.Windows.Forms.NativeWindow
    $window.CreateHandle((New-Object System.Windows.Forms.CreateParams))
    $context = New-Object System.Windows.Forms.ApplicationContext
    $timer = New-Object System.Windows.Forms.Timer
    $timer.Interval = 10000
    $timer.add_Tick({ $timer.Stop(); $context.ExitThread() })

    $added = [StackChanBalloon]::Add($window.Handle, [System.Drawing.SystemIcons]::Information.Handle)
    if (-not $added) { throw 'Windows could not register the temporary notification icon.' }
    $title = if ($Simulated) { '【模拟】StackChan 距离提醒' } else { 'StackChan 距离提醒' }
    $distanceCm = ($DistanceMm / 10.0).ToString('0.#', [System.Globalization.CultureInfo]::InvariantCulture)
    $message = "当前距离约 $distanceCm 厘米，已持续靠近一段时间。可以稍微坐远一些，调整到舒适的位置。"
    if (-not [StackChanBalloon]::Show($window.Handle, $title, $message, [bool]$Sound)) {
        throw 'Windows could not request the distance reminder notification.'
    }
    $timer.Start()
    [System.Windows.Forms.Application]::Run($context)
}
finally {
    if ($null -ne $timer) { $timer.Stop(); $timer.Dispose() }
    if ($added) { [void][StackChanBalloon]::Delete($window.Handle) }
    if ($null -ne $window) { $window.DestroyHandle() }
    if ($null -ne $context) { $context.Dispose() }
}
