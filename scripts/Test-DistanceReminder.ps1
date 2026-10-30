[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$python = Join-Path $projectRoot '.tools\espressif\python_env\idf5.5_py3.12_env\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $python)) { throw 'The project Python environment is missing.' }
Push-Location $projectRoot
try {
    & $python -m unittest discover -s pc/tests -v
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $python -m pc.distance_reminder --simulate --no-notify
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} finally {
    Pop-Location
}
Write-Host 'Distance reminder host checks passed. No robot connection or Windows notification was opened.'
