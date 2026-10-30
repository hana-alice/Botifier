[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$buildDirectory = Join-Path $projectRoot 'build\stackchan-demos'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsPath = & $vswhere -latest -version '[18.0,19.0)' -products '*' `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $vsPath) { throw 'Visual Studio C++ tools were not found.' }
$cmake = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (-not (Test-Path -LiteralPath $cmake)) { throw 'The existing Visual Studio CMake was not found.' }

& $cmake -S $PSScriptRoot -B $buildDirectory -G 'Visual Studio 18 2026' -A x64
if ($LASTEXITCODE -ne 0) { throw 'Demo configuration failed.' }
& $cmake --build $buildDirectory --config Release --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'Demo build failed.' }
Write-Host "Built: $buildDirectory\Release\stackchan_demo.exe"
