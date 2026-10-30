[CmdletBinding()]
param(
    [ValidateSet('Showcase', 'Timer', 'Both', 'Scan')]
    [string]$Demo = 'Showcase',
    [ValidateRange(3, 3600)]
    [int]$Seconds = 10,
    [string]$Address,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$executable = Join-Path $projectRoot 'build\stackchan-demos\Release\stackchan_demo.exe'
& (Join-Path $PSScriptRoot 'Build-Demos.ps1')
$logDirectory = Join-Path $projectRoot 'logs'
New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
$log = Join-Path $logDirectory ('demo-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.log')
$arguments = @('--demo', $Demo.ToLowerInvariant(), '--seconds', $Seconds.ToString())
if ($Address) { $arguments += @('--address', $Address) }
if ($DryRun) { $arguments += '--dry-run' }
& $executable @arguments 2>&1 | Tee-Object -FilePath $log
$demoExitCode = $LASTEXITCODE
Write-Host "Log: $log"
exit $demoExitCode
