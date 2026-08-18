param(
    [string]$QtPath = "C:\Qt\6.11.1\msvc2022_64",
    [string]$VcpkgPath = "C:\vcpkg",
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDirectory = Join-Path $ProjectRoot "build"
$ToolchainFile = Join-Path $VcpkgPath "scripts\buildsystems\vcpkg.cmake"

if (-not (Test-Path $QtPath)) {
    throw "Qt not found: $QtPath"
}

if (-not (Test-Path $ToolchainFile)) {
    throw "vcpkg toolchain not found: $ToolchainFile"
}

cmake `
    -S $ProjectRoot `
    -B $BuildDirectory `
    -G Ninja `
    "-DCMAKE_BUILD_TYPE=$Configuration" `
    "-DCMAKE_PREFIX_PATH=$QtPath" `
    "-DCMAKE_TOOLCHAIN_FILE=$ToolchainFile"

cmake --build $BuildDirectory

Write-Host ""
Write-Host "Build completed: $BuildDirectory\QtVideoGuard.exe"
