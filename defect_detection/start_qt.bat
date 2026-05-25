@echo off
chcp 65001 >nul
cd /d "%~dp0"
start "" "build_qt\Release\defect_detection_qt.exe" "config\config.yaml"
