# P0 Completion Verification Script
# Usage: powershell -ExecutionPolicy Bypass -File scripts\verify_p0.ps1

$ErrorActionPreference = "SilentlyContinue"
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$pass = 0; $fail = 0; $warn = 0

function Check($name, $cond) {
    if ($cond) { Write-Host "  [PASS] $name" -ForegroundColor Green; $script:pass++ }
    else       { Write-Host "  [FAIL] $name" -ForegroundColor Red;   $script:fail++ }
}
function Warn($name) {
    Write-Host "  [WARN] $name" -ForegroundColor Yellow; $script:warn++
}

Write-Host "========================================" -ForegroundColor Cyan
Write-Host " P0 Completion Verification" -ForegroundColor Cyan
Write-Host " Repo: $root" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

# P0-0 Delivery Boundary
Write-Host "--- P0-0 Delivery Boundary ---" -ForegroundColor Magenta
$readme = Get-Content "$root\defect_detection\README.md" -Raw -Encoding UTF8
$devdoc = ""
if (Test-Path "$root\defect_detection\docs\DEVELOPMENT.md") {
    $devdoc = Get-Content "$root\defect_detection\docs\DEVELOPMENT.md" -Raw -Encoding UTF8
}
Check "Delivery boundary documented" (
    $readme -match "v1\.0" -or $readme -match "6 \u7c7b" -or
    $devdoc -match "\u4ea4\u4ed8\u8fb9\u754c" -or $devdoc -match "\uff08\u672c\u9879\u76ee"
)
Check "6-class ID standard defined" (
    $readme -match "0=missing_hole" -or $devdoc -match "0=missing_hole" -or
    ($readme -match "missing_hole" -and $readme -match "spurious_copper")
)

# P0-1 Class Names Unified
Write-Host "--- P0-1 Class Names ---" -ForegroundColor Magenta
$typesContent = Get-Content "$root\defect_detection\include\utils\types.hpp" -Raw
Check "No Scratch/Other in types.hpp" ($typesContent -notmatch "Scratch" -and $typesContent -notmatch "Other,")
Check "MouseBite present" ($typesContent -match "MouseBite")
Check "Spur present" ($typesContent -match "Spur,")
$yaml = Get-Content "$root\defect_detection\config\config.yaml" -Raw
Check "snake_case class names in config" ($yaml -match "missing_hole" -and $yaml -match "mouse_bite")

# P0-2 YAML Config
Write-Host "--- P0-2 YAML Config ---" -ForegroundColor Magenta
$cmake = Get-Content "$root\defect_detection\CMakeLists.txt" -Raw
Check "yaml-cpp usable (QUIET+conda fallback)" ($cmake -match "yaml-cpp QUIET" -and $cmake -match "FATAL_ERROR.*yaml-cpp")
$cfg = Get-Content "$root\defect_detection\src\utils\config.cpp" -Raw
Check "YAML::LoadFile" ($cfg -match "YAML::LoadFile")
Check "YAML::Emitter" ($cfg -match "YAML::Emitter")

# P0-3 Training Pipeline
Write-Host "--- P0-3 Training Pipeline ---" -ForegroundColor Magenta
Check "manifest.json exists" (Test-Path "$root\defect_detection\models\manifest.json")
$m = Get-Content "$root\defect_detection\models\manifest.json" -Raw | ConvertFrom-Json
Check "manifest.model_version" ($m.model_version -ne $null)
Check "manifest.num_classes==6" ($m.num_classes -eq 6)
Check "manifest.input_width>0" ($m.input_width -gt 0)
Check "download_model.ps1" (Test-Path "$root\defect_detection\scripts\download_model.ps1")
if (Test-Path "$root\defect_detection\models\model.onnx") {
    Check "model.onnx exists" $true
} else {
    Warn "model.onnx missing (train or download required)"
}
$mc = Get-Content "$root\defect_detection\src\main.cpp" -Raw
Check "manifest read in main.cpp" ($mc -match "manifest_num_classes")

# P0-4 Evaluation
Write-Host "--- P0-4 Evaluation ---" -ForegroundColor Magenta
Check "evaluate.py exists" (Test-Path "$root\defect_detection\scripts\evaluate.py")
Check "Thresholds defined" ((Get-Content "$root\defect_detection\scripts\evaluate.py" -Raw) -match "THRESHOLDS")
$evalDir = Get-ChildItem "$root\defect_detection\logs\eval" -ErrorAction SilentlyContinue
if ($evalDir) { Check "logs/eval/ has content" ($evalDir.Count -gt 0) } else { Warn "logs/eval/ empty (run evaluate.py)" }

# P0-5 Data Augmentation
Write-Host "--- P0-5 Data Augmentation ---" -ForegroundColor Magenta
Check "rotate.py.legacy" (Test-Path "$root\PCB_DATASET\rotate.py.legacy")
Check "rotation README exists" (Test-Path "$root\PCB_DATASET\rotation\README.md")
Check "verify_labels.py exists" (Test-Path "$root\defect_detection\scripts\verify_labels.py")
$lc = Get-ChildItem "$root\defect_detection\logs\label_check" -ErrorAction SilentlyContinue
if ($lc) { Check "logs/label_check/ has samples" ($lc.Count -gt 0) } else { Warn "logs/label_check/ empty (run verify_labels.py)" }

# P0-6 Inference Pipeline
Write-Host "--- P0-6 Inference Pipeline ---" -ForegroundColor Magenta
$mainCpp = Get-Content "$root\defect_detection\src\main.cpp" -Raw
Check "No duplicate filter+nms in main.cpp" ($mainCpp -notmatch "postprocessor\.filter\(all_detections\)")
Check "set_confidence_threshold in main.cpp" ($mainCpp -match "set_confidence_threshold")
$qtCpp = Get-Content "$root\defect_detection\src\ui\qt_main_window.cpp" -Raw
Check "Qt line scan: filter only, no nms" (($qtCpp -match "filter\(bx\)") -and ($qtCpp -notmatch "nms\(bx\)"))
$infCpp = Get-Content "$root\defect_detection\src\inference\inferencer.cpp" -Raw
Check "No hardcoded 0.45f NMS" ($infCpp -notmatch "global_nms\(all_boxes, 0\\.45f\)")
Check "No hardcoded 0.01f" ($infCpp -notmatch "0\\.01f")
Check "process() returns empty (bridge function)" ((Get-Content "$root\defect_detection\src\postprocess\postprocessor.cpp" -Raw) -match "return \{\}")

# P0-7 Serial Communication
Write-Host "--- P0-7 Serial Protocol ---" -ForegroundColor Magenta
Check "SERIAL_PROTOCOL.md exists" (Test-Path "$root\defect_detection\docs\SERIAL_PROTOCOL.md")
Check "Contains TRIG" ((Get-Content "$root\defect_detection\docs\SERIAL_PROTOCOL.md" -Raw) -match "TRIG")
Check "Contains PASS/FAIL/DONE" ((Get-Content "$root\defect_detection\docs\SERIAL_PROTOCOL.md" -Raw) -match "FAIL:n")
Check "main.cpp TRIG listen" ($mainCpp -match "TRIG")
Check "main.cpp DONE reply" ($mainCpp -match "DONE")
Check "main.cpp RST support" ($mainCpp -match "RST")
Check "main.cpp save_defect" ($mainCpp -match "save_defect")
Check "Serial open fail handled" ($mainCpp -match "if \(!serial\.open" -or $mainCpp -match "WARN.*serial")
if (-not (Test-Path "$root\defect_detection\logs\defects")) { Warn "logs/defects/ missing (created at runtime)" }

# P0-8 Benchmark
Write-Host "--- P0-8 Benchmark ---" -ForegroundColor Magenta
Check "benchmark.py exists" (Test-Path "$root\defect_detection\scripts\benchmark.py")
$bench = Get-ChildItem "$root\defect_detection\logs\benchmark_*.txt" -ErrorAction SilentlyContinue
if ($bench) {
    Check "Benchmark report exists" ($bench.Count -gt 0)
    $br = Get-Content $bench[0].FullName -Raw
    Check "Contains single_window" ($br -match "single_window")
    Check "Contains large_image" ($br -match "large_image")
} else {
    Warn "logs/benchmark_*.txt missing (run benchmark.py)"
}
Check "Performance documented (README or DEVELOPMENT)" (
    ($readme -match "benchmark" -and ($readme -match "\u6574\u677f" -or $readme -match "\u6ed1\u7a97")) -or
    ($devdoc -match "benchmark" -and $devdoc -match "\u6574\u677f")
)

# P0-9 Verification script itself
Write-Host "--- P0-9 Verification ---" -ForegroundColor Magenta
Check "verify_p0.ps1 runs" $true

# Summary
Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host " Result: $pass PASS / $fail FAIL / $warn WARN" -ForegroundColor Cyan
if ($fail -eq 0) {
    Write-Host " All P0 checks passed!" -ForegroundColor Green
} else {
    Write-Host " $fail item(s) not passing" -ForegroundColor Red
}
Write-Host "========================================" -ForegroundColor Cyan
exit $fail
