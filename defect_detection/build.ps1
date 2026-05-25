# defect_detection 一键编译脚本
# 用法: .\build.ps1
# 或者: powershell -ExecutionPolicy Bypass -File build.ps1

param(
    [string]$Config = "Release",         # 编译配置: Release / Debug
    [string]$OpenCVDir = "C:\Opencv2\opencv\build"  # OpenCV 安装路径
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDir    = Join-Path $ProjectRoot "build"

# ── 1. 检查 VS 2019 BuildTools 环境 ──────────────────────────
$vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
$opencv_bin = "$OpenCVDir\x64\vc16\bin"
if (-not (Test-Path $vcvars)) {
    Write-Error "找不到 vcvars64.bat，请确认 VS 2019 BuildTools 已安装。"
    exit 1
}

# ── 2. 检查 cmake ───────────────────────────────────────────
$cmake = "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if (-not (Test-Path $cmake)) {
    Write-Error "找不到 cmake"
    exit 1
}

Write-Host "=== 编译: defect_detection ($Config) ===" -ForegroundColor Cyan
Write-Host "  项目目录 : $ProjectRoot"
Write-Host "  构建目录 : $BuildDir"
Write-Host "  OpenCV   : $OpenCVDir"
Write-Host ""

# ── 3. CMake 配置 ──────────────────────────────────────────
& cmd.exe /c "`"$vcvars`" && `"$cmake`" -B `"$BuildDir`" -S `"$ProjectRoot`" -G `"Visual Studio 16 2019`" -A x64 -DOpenCV_DIR=`"$OpenCVDir`" -DCMAKE_BUILD_TYPE=$Config"
if ($LASTEXITCODE -ne 0) {
    Write-Error "CMake 配置失败"
    exit 1
}

# ── 4. 编译 ────────────────────────────────────────────────
& cmd.exe /c "`"$vcvars`" && `"$cmake`" --build `"$BuildDir`" --config $Config"
if ($LASTEXITCODE -ne 0) {
    Write-Error "编译失败"
    exit 1
}

Write-Host ""
Write-Host "=== 编译成功 ===" -ForegroundColor Green
Write-Host "  输出: $BuildDir\$Config\defect_detection.exe"
if (Test-Path "$BuildDir\$Config\defect_detection_qt.exe") {
    Write-Host "  输出: $BuildDir\$Config\defect_detection_qt.exe (Qt 版)" -ForegroundColor Cyan
}

# 自动复制 DLL 到 exe 目录
$dll_sources = @(
    @{Path="$opencv_bin"; Pattern="opencv_world*.dll"; Label="OpenCV"},
    @{Path="$ProjectRoot\3rdparty\onnxruntime\lib"; Pattern="*.dll"; Label="ONNX Runtime"}
)
foreach ($src in $dll_sources) {
    $dlls = Get-ChildItem -Path $src.Path -Filter $src.Pattern -ErrorAction SilentlyContinue
    foreach ($dll in $dlls) {
        Copy-Item -Path $dll.FullName -Destination "$BuildDir\$Config\" -Force
        Write-Host "  已复制 $($src.Label) DLL: $($dll.Name)" -ForegroundColor Yellow
    }
}

# Qt DLL (仅在 Qt 编译时复制)
$qtBin = "$ProjectRoot\..\..\Qt\6.6.2\msvc2019_64\bin"
if (Test-Path "$BuildDir\$Config\defect_detection_qt.exe" -and (Test-Path $qtBin)) {
    $qtDlls = @("Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll")
    foreach ($dll in $qtDlls) {
        Copy-Item "$qtBin\$dll" "$BuildDir\$Config\" -Force -ErrorAction SilentlyContinue
        Write-Host "  已复制 Qt DLL: $dll" -ForegroundColor Yellow
    }
    # Qt 平台插件
    $plugins = "$qtBin\..\plugins\platforms"
    if (Test-Path "$plugins\qwindows.dll") {
        New-Item -ItemType Directory -Path "$BuildDir\$Config\platforms" -Force | Out-Null
        Copy-Item "$plugins\qwindows.dll" "$BuildDir\$Config\platforms\" -Force
        Write-Host "  已复制 Qt 平台插件: qwindows.dll" -ForegroundColor Yellow
    }
    Write-Host "  运行 Qt 版: cd $ProjectRoot && .\build\$Config\defect_detection_qt.exe config\config.yaml"
}
