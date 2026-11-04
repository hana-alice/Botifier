[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$DeviceHost,
    [Parameter(Mandatory)][string]$Image,
    [Parameter(Mandatory)][string]$ExpectedMac,
    [Parameter(Mandatory)][string]$Report,
    [string]$KeyFile,
    [ValidateRange(1, 1800)][int]$TimeoutSeconds = 180
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$python = Join-Path $projectRoot '.tools\espressif\python_env\idf5.5_py3.12_env\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $python -PathType Leaf)) { $python = 'python' }
$arguments = @((Join-Path $PSScriptRoot 'Update-FirmwareWifi.py'), '--host', $DeviceHost,
    '--image', $Image, '--expected-mac', $ExpectedMac, '--report', $Report,
    '--timeout', $TimeoutSeconds.ToString())
if ($KeyFile) { $arguments += @('--key-file', $KeyFile) }
& $python @arguments
exit $LASTEXITCODE
