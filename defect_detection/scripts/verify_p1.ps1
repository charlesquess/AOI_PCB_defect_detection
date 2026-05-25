param()

$passed = 0
$failed = 0
$warned = 0

function Check {
    param($desc, $cond)
    if (& $cond) { $script:passed++; Write-Host "  [PASS] $desc" -ForegroundColor Green }
    else { $script:failed++; Write-Host "  [FAIL] $desc" -ForegroundColor Red }
}
function Warn {
    param($desc)
    $script:warned++; Write-Host "  [WARN] $desc" -ForegroundColor Yellow
}

$root = Split-Path -Parent $PSScriptRoot

Write-Host "============================================" -ForegroundColor Cyan
Write-Host "  P1 Acceptance Script (P1-1 ~ P1-8)" -ForegroundColor Cyan
Write-Host "============================================" -ForegroundColor Cyan
Write-Host ""

# P1-1
Write-Host "P1-1 One-click Demo" -ForegroundColor Yellow
Check "run_demo.py exists"       { Test-Path "$root/scripts/run_demo.py" }
Check "batch_infer.py exists"    { Test-Path "$root/scripts/batch_infer.py" }
Check "make_simulation_video exists" { Test-Path "$root/scripts/make_simulation_video.py" }
Check "DEMO.md exists"           { Test-Path "$root/docs/DEMO.md" }
Check "simulation video exists"  { Test-Path "$root/data/pipeline_simulation.mp4" }

# P1-2
Write-Host "P1-2 Batch Inference & Report" -ForegroundColor Yellow
Check "batch_infer.py runs (--help)" { & python $root/scripts/batch_infer.py --help 2>$null; $LASTEXITCODE -eq 0 }
Warn "evaluate.py report.html (requires training run)"

# P1-3
Write-Host "P1-3 Model Version Binding" -ForegroundColor Yellow
Check "manifest.json exists" { Test-Path "$root/models/manifest.json" }
Check "manifest has version" {
    $j = Get-Content "$root/models/manifest.json" -Raw | ConvertFrom-Json
    ($j.model_version -ne $null -and $j.model_version -ne "")
}
Check "main.cpp MIN_VERSION check" { (Get-Content "$root/src/main.cpp" -Raw) -match "MIN_VERSION" }
Check "main_qt.cpp MIN_VERSION check" { (Get-Content "$root/src/main_qt.cpp" -Raw) -match "MIN_VERSION" }

# P1-4
Write-Host "P1-4 Automated Tests & CI" -ForegroundColor Yellow
Check "run_tests.py exists" { Test-Path "$root/scripts/run_tests.py" }
Check "run_ci.bat exists"   { Test-Path "$root/scripts/run_ci.bat" }
Check "tests pass (56/56)"  {
    $r = & $root/scripts/run_tests.py 2>&1
    $LASTEXITCODE -eq 0
}

# P1-5
Write-Host "P1-5 Qt UI as Default" -ForegroundColor Yellow
Check "setup_qt.ps1 exists"  { Test-Path "$root/setup_qt.ps1" }
Check "Qt exe exists"        { Test-Path "$root/build_qt/Release/defect_detection_qt.exe" }
Check "debug tab has video path" { (Get-Content "$root/src/ui/qt_main_window.cpp" -Raw) -match "edit_video_path_" }
Check "debug tab has serial help" { (Get-Content "$root/src/ui/qt_main_window.cpp" -Raw) -match "com0com" }
Check "save to config.yaml button exists" { (Get-Content "$root/src/ui/qt_main_window.cpp" -Raw) -match "onSaveConfig" }
Check "config.cpp save() exists" { (Get-Content "$root/src/utils/config.cpp" -Raw) -match "bool save\(" }

# P1-6
Write-Host "P1-6 Defect Traceability" -ForegroundColor Yellow
Check "save_defect draws boxes" { (Get-Content "$root/src/main.cpp" -Raw) -match "cv::rectangle.*annotated" }
Check "save_defect writes pcb_count" { (Get-Content "$root/src/main.cpp" -Raw) -match "pcb_count" }
Check "save_defect writes datetime" { (Get-Content "$root/src/main.cpp" -Raw) -match "datetime" }
Check "DayStats struct exists" { (Get-Content "$root/include/statistics/statistician.hpp" -Raw) -match "struct DayStats" }
Check "export_daily_csv exists" { (Get-Content "$root/include/statistics/statistician.hpp" -Raw) -match "export_daily_csv" }

# P1-7
Write-Host "P1-7 PLC Protocol" -ForegroundColor Yellow
Check "SerialCommState enum exists" { (Get-Content "$root/include/utils/types.hpp" -Raw) -match "enum class SerialCommState" }
Check "serial_state in SharedResultBox" { (Get-Content "$root/include/utils/types.hpp" -Raw) -match "serial_state" }
Check "main.cpp state machine" { (Get-Content "$root/src/main.cpp" -Raw) -match "WaitingTrig" }
Check "Qt UI serial state display" { (Get-Content "$root/src/ui/qt_main_window.cpp" -Raw) -match "lbl_serial_state_" }
Check "PLC simulator has STAT/RST" { (Get-Content "$root/scripts/plc_simulator.py" -Raw) -match "STAT" -and (Get-Content "$root/scripts/plc_simulator.py" -Raw) -match "RST" }
Check "SERIAL_PROTOCOL.md has state diagram" { (Get-Content "$root/docs/SERIAL_PROTOCOL.md" -Raw) -match "Idle" }

# P1-8
Write-Host "P1-8 Deployment Package" -ForegroundColor Yellow
Check "package_deploy.bat exists"   { Test-Path "$root/scripts/package_deploy.bat" }
Check "DEPENDENCIES.md exists"      { Test-Path "$root/docs/DEPENDENCIES.md" }
Check ".gitignore covers build_qt"  { (Get-Content "$root/.gitignore" -Raw) -match "build_qt/" }
Check ".gitignore covers logs"      { (Get-Content "$root/.gitignore" -Raw) -match "logs/" }
Check "package_deploy.bat builds deploy dir" {
    $null = & cmd.exe /c "$root\scripts\package_deploy.bat" 2>&1
    $ok = Test-Path "$root/deploy/bin/defect_detection.exe"
    Remove-Item -Recurse -Force "$root/deploy" -ErrorAction SilentlyContinue
    $ok
}

# Summary
Write-Host ""
Write-Host "============================================" -ForegroundColor Cyan
Write-Host "  Result: $passed PASS / $failed FAIL / $warned WARN" -ForegroundColor Cyan
Write-Host "============================================" -ForegroundColor Cyan

if ($failed -eq 0) {
    Write-Host "  P1 ALL PASS. Ready for sign-off." -ForegroundColor Green
} else {
    Write-Host "  Some checks failed, please fix and retry." -ForegroundColor Red
}
exit $failed
