# Dot-source this script to keep the environment in the current shell:
# . .\scripts\Enter-DevEnv.ps1
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$idfPath = Join-Path $projectRoot '.tools\esp-idf-v5.5.4'
$idfToolsPath = Join-Path $projectRoot '.tools\espressif'
$idfPythonEnvPath = Join-Path $idfToolsPath 'python_env\idf5.5_py3.12_env'
$exportScript = Join-Path $idfPath 'export.ps1'

if (-not (Test-Path -LiteralPath $exportScript -PathType Leaf)) {
    throw "ESP-IDF 5.5.4 is missing at '$idfPath'. Finish the project toolchain setup first."
}
if (-not (Test-Path -LiteralPath $idfToolsPath -PathType Container)) {
    throw "ESP-IDF tools are missing at '$idfToolsPath'. Finish the project toolchain setup first."
}
$pythonLauncher = Get-Command py -CommandType Application -ErrorAction SilentlyContinue
if (-not $pythonLauncher) {
    throw 'Python Launcher (py) is unavailable. This project requires the existing Python 3.12 installation.'
}
$pythonPath = & $pythonLauncher.Source -3.12 -c 'import sys; print(sys.executable)'
if ($LASTEXITCODE -ne 0 -or -not $pythonPath -or -not (Test-Path -LiteralPath $pythonPath -PathType Leaf)) {
    throw 'Python 3.12 is unavailable through py -3.12. Finish the project toolchain setup first.'
}
$idfPython = Join-Path $idfPythonEnvPath 'Scripts\python.exe'
if (-not (Test-Path -LiteralPath $idfPython -PathType Leaf)) {
    throw "ESP-IDF Python environment is not ready at '$idfPythonEnvPath'. Wait for toolchain setup to finish."
}

$env:IDF_TOOLS_PATH = $idfToolsPath
$env:IDF_PATH = $idfPath
$env:IDF_PYTHON_ENV_PATH = $idfPythonEnvPath
$env:PATH = (Split-Path -Parent $pythonPath) + [IO.Path]::PathSeparator + $env:PATH
. $exportScript
if ($LASTEXITCODE -ne 0 -or -not (Get-Command idf.py -ErrorAction SilentlyContinue)) {
    throw 'ESP-IDF environment activation failed. Check the export output above.'
}

Write-Host "ESP-IDF environment ready for $projectRoot"
