# 下载 ONNX Runtime GPU 版并覆盖安装
# 要求: CUDA 12.x + cuDNN 8.x

$ErrorActionPreference = "Stop"
$url = "https://github.com/microsoft/onnxruntime/releases/download/v1.23.2/onnxruntime-win-x64-gpu-1.23.2.zip"
$outDir = "C:\Users\29814\Desktop\vision_projects\defect_detection\3rdparty"
$zipFile = "$outDir\ort_gpu.zip"

Write-Host "下载 ONNX Runtime GPU 版 (310MB)..." -ForegroundColor Cyan
Write-Host "预计 5-15 分钟..." -ForegroundColor Yellow

[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
Invoke-WebRequest -Uri $url -OutFile $zipFile -UseBasicParsing

Write-Host "解压中..." -ForegroundColor Yellow
$tempDir = "$env:TEMP\ort_gpu"
Remove-Item -Recurse -Force $tempDir -ErrorAction SilentlyContinue
Expand-Archive -Path $zipFile -DestinationPath $tempDir -Force

$src = Get-ChildItem $tempDir -Directory | Select-Object -First 1 -ExpandProperty FullName

# 备份 CPU 版
$ortDir = "$outDir\onnxruntime"
if (Test-Path "$ortDir") {
    Remove-Item -Recurse -Force "$ortDir.bak" -ErrorAction SilentlyContinue
    Rename-Item -Path $ortDir -NewName "onnxruntime.bak"
}

# 复制 GPU 版
Copy-Item -Path "$src\include" -Destination "$outDir\onnxruntime\include" -Recurse -Force
Copy-Item -Path "$src\lib" -Destination "$outDir\onnxruntime\lib" -Recurse -Force

Remove-Item -Recurse -Force $tempDir -ErrorAction SilentlyContinue
Remove-Item $zipFile -Force

Write-Host "ONNX Runtime GPU 版安装完成!" -ForegroundColor Green
Write-Host "之后在 config.yaml 中设置 use_gpu: true" -ForegroundColor Cyan
