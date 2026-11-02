[CmdletBinding()]
param(
    [string]$Port,
    [ValidateRange(60, 3600)][int]$PollSeconds = 300,
    [switch]$Once,
    [switch]$Simulate
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$python = Join-Path $projectRoot '.tools\espressif\python_env\idf5.5_py3.12_env\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $python)) { throw 'The project Python environment is missing. See DEVELOPMENT.md.' }
$arguments = @('-m', 'pc.codex_weekly', '--poll-seconds', $PollSeconds.ToString())
if ($Port) { $arguments += @('--port', $Port) }
if ($Once) { $arguments += '--once' }
if ($Simulate) { $arguments += '--simulate' }
Push-Location $projectRoot
try {
    & $python @arguments
    $weeklyExitCode = $LASTEXITCODE
} finally {
    Pop-Location
}
exit $weeklyExitCode
