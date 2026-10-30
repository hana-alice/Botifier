[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'Enter-DevEnv.ps1')
$logDirectory = Join-Path $projectRoot 'logs'
$null = New-Item -ItemType Directory -Path $logDirectory -Force
$logFile = Join-Path $logDirectory ('firmware-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.log')
Push-Location (Join-Path $projectRoot 'StackChan\firmware')
try {
    python "$env:IDF_PATH\tools\idf.py" build 2>&1 | Tee-Object -FilePath $logFile
    $buildExit = $LASTEXITCODE
} finally {
    Pop-Location
}
Write-Host "Firmware build log: $logFile"
exit $buildExit
