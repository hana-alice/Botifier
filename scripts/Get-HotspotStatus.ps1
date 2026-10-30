[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSEdition -eq 'Core') {
    & "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -File $PSCommandPath
    exit $LASTEXITCODE
}

Add-Type -AssemblyName System.Runtime.WindowsRuntime
$null = [Windows.Networking.Connectivity.NetworkInformation,Windows.Networking.Connectivity,ContentType=WindowsRuntime]
$null = [Windows.Networking.NetworkOperators.NetworkOperatorTetheringManager,Windows.Networking.NetworkOperators,ContentType=WindowsRuntime]
$profile = [Windows.Networking.Connectivity.NetworkInformation]::GetInternetConnectionProfile()
if (-not $profile) { throw 'No internet connection profile is available.' }
$manager = [Windows.Networking.NetworkOperators.NetworkOperatorTetheringManager]::CreateFromConnectionProfile($profile)
$configuration = $null
$configurationError = $null
try {
    $configuration = $manager.GetCurrentAccessPointConfiguration()
} catch {
    $configurationError = $_.Exception.Message
}

# Query only. A disconnected adapter may return ERROR_INVALID_STATE (5023).
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class StackChanWlanProbe {
    [DllImport("wlanapi.dll")] static extern uint WlanOpenHandle(uint version, IntPtr reserved, out uint negotiated, out IntPtr handle);
    [DllImport("wlanapi.dll")] static extern uint WlanCloseHandle(IntPtr handle, IntPtr reserved);
    [DllImport("wlanapi.dll")] static extern uint WlanQueryInterface(IntPtr handle, ref Guid guid, uint opcode, IntPtr reserved, out uint size, out IntPtr data, out uint valueType);
    [DllImport("wlanapi.dll")] static extern void WlanFreeMemory(IntPtr data);
    public static uint[] Channel(Guid guid) {
        uint version, size, valueType;
        IntPtr handle, data = IntPtr.Zero;
        uint error = WlanOpenHandle(2, IntPtr.Zero, out version, out handle);
        if (error != 0) return new uint[] {error, 0};
        try {
            error = WlanQueryInterface(handle, ref guid, 8, IntPtr.Zero, out size, out data, out valueType);
            return new uint[] {error, error == 0 && size >= 4 ? (uint)Marshal.ReadInt32(data) : 0};
        } finally {
            if (data != IntPtr.Zero) WlanFreeMemory(data);
            WlanCloseHandle(handle, IntPtr.Zero);
        }
    }
}
'@

$adapters = @(Get-NetAdapter -IncludeHidden | Where-Object {
    $_.InterfaceDescription -match 'Wi-Fi|Wireless|802\.11' -and $_.Status -ne 'Not Present'
} | ForEach-Object {
    $channel = [StackChanWlanProbe]::Channel([guid]$_.InterfaceGuid)
    [pscustomobject]@{
        Name = $_.Name
        Description = $_.InterfaceDescription
        Status = $_.Status.ToString()
        ChannelQueryError = $channel[0]
        Channel = if ($channel[0] -eq 0) { $channel[1] } else { $null }
    }
})

# Select fields explicitly: never print or persist the hotspot passphrase.
[pscustomobject]@{
    CapturedAt = (Get-Date).ToString('o')
    InternetProfile = $profile.ProfileName
    State = $manager.TetheringOperationalState.ToString()
    ClientCount = $manager.ClientCount
    SSID = if ($configuration) { $configuration.Ssid } else { $null }
    ConfiguredBand = if ($configuration) { $configuration.Band.ToString() } else { $null }
    Authentication = if ($configuration) { $configuration.AuthenticationKind.ToString() } else { $null }
    ConfigurationQueryError = $configurationError
    Clients = @($manager.GetTetheringClients() | ForEach-Object {
        [pscustomobject]@{
            MacAddress = $_.MacAddress
            HostNames = @($_.HostNames | ForEach-Object { $_.DisplayName })
        }
    })
    Adapters = $adapters
} | ConvertTo-Json -Depth 5
