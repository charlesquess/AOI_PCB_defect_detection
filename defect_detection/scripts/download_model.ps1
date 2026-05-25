# PCB 缺陷检测 — 模型下载脚本
# 用法: .\scripts\download_model.ps1
# 从指定 URL 下载预训练 ONNX 模型到 models/model.onnx

param(
    [string]$Url = "",
    [string]$OutputDir = "$PSScriptRoot\..\models"
)

# 如果未指定 URL，使用默认网盘/发布地址
if (-not $Url) {
    # 如果项目有 Release，从这里下载
    Write-Host "未指定 URL，请从以下方式获取模型:" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "  方式 1: 自行训练" -ForegroundColor Cyan
    Write-Host "    cd yolov5-7.0"
    Write-Host "    python train.py --img 640 --batch 16 --epochs 100 --data ../defect_detection/data/yolo_dataset/dataset.yaml --hyp data/hyps/hyp.pcb.yaml --weights yolov5s.pt"
    Write-Host "    python export.py --weights runs/train/exp/weights/best.pt --include onnx --img 640"
    Write-Host "    python ../defect_detection/scripts/export_onnx.py"
    Write-Host ""
    Write-Host "  方式 2: 从 Release 下载（如已发布）" -ForegroundColor Cyan
    Write-Host "    .\scripts\download_model.ps1 -Url https://github.com/<user>/<repo>/releases/download/v1.0/model.onnx"
    Write-Host ""
    exit 1
}

$OutputDir = Resolve-Path $OutputDir
$OutputPath = Join-Path $OutputDir "model.onnx"

Write-Host "下载模型: $Url" -ForegroundColor Green
Write-Host "保存到:   $OutputPath" -ForegroundColor Green

# 下载
try {
    Invoke-WebRequest -Uri $Url -OutFile $OutputPath -UseBasicParsing
    Write-Host "下载完成: $( (Get-Item $OutputPath).Length / 1KB ) KB" -ForegroundColor Green
} catch {
    Write-Host "下载失败: $_" -ForegroundColor Red
    exit 1
}

# 生成/更新 manifest.json
$manifestPath = Join-Path $OutputDir "manifest.json"
$manifest = @{
    model_version = "1.0.0"
    training_date = (Get-Date -Format "yyyy-MM-dd")
    description   = "YOLOv5s P2 PCB defect detection"
    input_width   = 640
    input_height  = 640
    num_classes   = 6
    class_names   = @("missing_hole", "mouse_bite", "open_circuit", "short", "spur", "spurious_copper")
    mAP           = $null
    framework     = "YOLOv5"
    runtime       = "ONNX Runtime"
}
$manifest | ConvertTo-Json -Depth 3 | Set-Content $manifestPath -Encoding UTF8
Write-Host "manifest.json 已生成" -ForegroundColor Green
Write-Host ""
Write-Host "就绪，启动检测模式: .\build\Release\defect_detection.exe config\config.yaml" -ForegroundColor Cyan
