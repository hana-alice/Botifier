[CmdletBinding()]
param(
    [string]$Port,
    [double]$ThresholdCm,
    [double]$HysteresisCm,
    [double]$HoldSeconds,
    [double]$CooldownSeconds,
    [switch]$Sound,
    [switch]$NoSound,
    [switch]$Simulate,
    [switch]$NoNotify
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$python = Join-Path $projectRoot '.tools\espressif\python_env\idf5.5_py3.12_env\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $python)) { throw 'The project Python environment is missing. See DEVELOPMENT.md.' }
if ($Sound -and $NoSound) { throw 'Choose Sound or NoSound.' }
$arguments = @('-m', 'pc.distance_reminder')
if ($Port) { $arguments += @('--port', $Port) }
$numericOptions = @{
    ThresholdCm = '--threshold-cm'
    HysteresisCm = '--hysteresis-cm'
    HoldSeconds = '--hold-seconds'
    CooldownSeconds = '--cooldown-seconds'
}
foreach ($name in $numericOptions.Keys) {
    if ($PSBoundParameters.ContainsKey($name)) {
        $value = $PSBoundParameters[$name].ToString([Globalization.CultureInfo]::InvariantCulture)
        $arguments += @($numericOptions[$name], $value)
    }
}
if ($Sound) { $arguments += '--sound' }
if ($NoSound) { $arguments += '--no-sound' }
if ($Simulate) { $arguments += '--simulate' }
if ($NoNotify) { $arguments += '--no-notify' }
Push-Location $projectRoot
try {
    & $python @arguments
    $reminderExitCode = $LASTEXITCODE
} finally {
    Pop-Location
}
exit $reminderExitCode
