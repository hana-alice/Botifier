[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$testSource = Join-Path $projectRoot 'StackChan\firmware\tests'
$buildDirectory = Join-Path $projectRoot 'build\host-tests'
$logDirectory = Join-Path $projectRoot 'logs'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$cmakePath = $null

if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
    $vsInstallPath = & $vswhere -latest -version '[18.0,19.0)' -products '*' `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -eq 0 -and $vsInstallPath) {
        $cmakePath = Join-Path $vsInstallPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    }
}
if (-not $cmakePath -or -not (Test-Path -LiteralPath $cmakePath -PathType Leaf)) {
    $cmakePath = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
}
if (-not (Test-Path -LiteralPath $cmakePath -PathType Leaf)) {
    throw 'Visual Studio 2026 bundled CMake is unavailable. Finish the project host toolchain setup first.'
}
$ctestPath = Join-Path (Split-Path -Parent $cmakePath) 'ctest.exe'
if (-not (Test-Path -LiteralPath $ctestPath -PathType Leaf)) {
    throw "CTest is missing beside '$cmakePath'."
}
if (-not (Test-Path -LiteralPath (Join-Path $testSource 'CMakeLists.txt') -PathType Leaf)) {
    throw "The upstream host tests are missing at '$testSource'."
}

$null = New-Item -ItemType Directory -Path $logDirectory -Force
$logPrefix = Join-Path $logDirectory ('host-tests-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
Write-Host "Using $cmakePath"

& $cmakePath -S $testSource -B $buildDirectory -G 'Visual Studio 18 2026' -A x64 `
    '-DCMAKE_CXX_STANDARD=20' '-DCMAKE_CXX_FLAGS=/D_USE_MATH_DEFINES /EHsc' 2>&1 |
    Tee-Object -FilePath "$logPrefix-configure.log"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $cmakePath --build $buildDirectory --config Release --parallel 2 2>&1 |
    Tee-Object -FilePath "$logPrefix-build.log"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $ctestPath --test-dir $buildDirectory -C Release --output-on-failure 2>&1 |
    Tee-Object -FilePath "$logPrefix-test.log"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Host tests passed. Logs: $logPrefix-*.log"
exit 0
