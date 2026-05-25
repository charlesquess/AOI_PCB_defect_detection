# Qt6 安装脚本 — 使用 aqtinstall 下载 Qt 6.6.2 for MSVC 2019
# 用法: powershell -ExecutionPolicy Bypass -File setup_qt.ps1

$ErrorActionPreference = "Stop"
$QtDir = "C:\Qt\6.6.2"

Write-Host "=== 安装 Qt 6.6.2 (MSVC 2019 64-bit) ===" -ForegroundColor Cyan

# 1. 安装 aqtinstall (Python Qt 安装工具)
Write-Host "[1/3] 安装 aqtinstall..." -ForegroundColor Yellow
$env:Path = "C:\Users\29814\.conda\envs\yolov5;C:\Users\29814\.conda\envs\yolov5\Scripts;$env:Path"
pip install aqtinstall -q

# 2. 下载并安装 Qt 6.6.2 (仅 desktop 组件, ~300MB)
Write-Host "[2/3] 下载 Qt 6.6.2 (win64_msvc2019_64)..." -ForegroundColor Yellow
Write-Host "  这可能需要 5-15 分钟，取决于网络速度" -ForegroundColor White
aqt install-qt --outputdir C:\Qt windows desktop 6.6.2 win64_msvc2019_64 -m qtbase qtwidgets

# 3. 验证
Write-Host "[3/3] 验证安装..." -ForegroundColor Yellow
$cmake_conf = "C:\Qt\6.6.2\msvc2019_64\lib\cmake\Qt6\Qt6Config.cmake"
if (Test-Path $cmake_conf) {
    Write-Host "  Qt 6.6.2 安装成功!" -ForegroundColor Green
    Write-Host "  CMake 编译: cmake -B build -G `"Visual Studio 16 2019`" -A x64 -DOpenCV_DIR=C:/Opencv2/opencv/build -DCMAKE_PREFIX_PATH=C:/Qt/6.6.2/msvc2019_64"
} else {
    Write-Host "  安装可能不完整，请检查 $QtDir" -ForegroundColor Red
}

Write-Host ""
Write-Host "安装完成后编译 Qt 版:" -ForegroundColor Cyan
Write-Host "  cmake -B build_qt -G `"Visual Studio 16 2019`" -A x64 -DOpenCV_DIR=C:/Opencv2/opencv/build -DCMAKE_PREFIX_PATH=C:/Qt/6.6.2/msvc2019_64"
Write-Host "  cmake --build build_qt --config Release"
Write-Host "  .\build_qt\Release\defect_detection_qt.exe config\config.yaml"
