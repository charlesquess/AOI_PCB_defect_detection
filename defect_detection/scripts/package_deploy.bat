@echo off
setlocal enabledelayedexpansion

REM PCB Defect Detection - Deployment Package Builder
REM Usage: scripts\package_deploy.bat

set "PROJECT_DIR=%~dp0.."
set "BUILD_DIR=%PROJECT_DIR%\build"
set "DEPLOY_DIR=%PROJECT_DIR%\deploy"

echo ========================================
echo   PCB Defect Detection - Package Builder
echo ========================================
echo.

if not exist "%BUILD_DIR%\Release\defect_detection.exe" (
    echo ERROR: Build not found. Run scripts\run_ci.bat first.
    exit /b 1
)

if exist "%DEPLOY_DIR%" rmdir /s /q "%DEPLOY_DIR%"

mkdir "%DEPLOY_DIR%\bin" "%DEPLOY_DIR%\config" "%DEPLOY_DIR%\models" "%DEPLOY_DIR%\docs" 2>nul

echo [1/4] Copying executables and DLLs...
copy "%BUILD_DIR%\Release\defect_detection.exe" "%DEPLOY_DIR%\bin\" >nul
if exist "%BUILD_DIR%\Release\defect_detection_qt.exe" copy "%BUILD_DIR%\Release\defect_detection_qt.exe" "%DEPLOY_DIR%\bin\" >nul

for %%d in (onnxruntime.dll onnxruntime_providers_shared.dll opencv_world4130.dll) do (
    if exist "%BUILD_DIR%\Release\%%d" copy "%BUILD_DIR%\Release\%%d" "%DEPLOY_DIR%\bin\" >nul
)
if exist "%BUILD_DIR%\Release\yaml-cpp*.dll" copy "%BUILD_DIR%\Release\yaml-cpp*.dll" "%DEPLOY_DIR%\bin\" >nul

if exist "%DEPLOY_DIR%\bin\defect_detection_qt.exe" (
    for %%d in (Qt6Core.dll Qt6Gui.dll Qt6Widgets.dll) do (
        if exist "C:\Qt\6.6.2\msvc2019_64\bin\%%d" copy "C:\Qt\6.6.2\msvc2019_64\bin\%%d" "%DEPLOY_DIR%\bin\" >nul
    )
    if exist "C:\Qt\6.6.2\msvc2019_64\plugins\platforms\qwindows.dll" (
        mkdir "%DEPLOY_DIR%\bin\platforms" >nul 2>&1
        copy "C:\Qt\6.6.2\msvc2019_64\plugins\platforms\qwindows.dll" "%DEPLOY_DIR%\bin\platforms\" >nul
    )
)

echo [2/4] Copying config and models...
copy "%PROJECT_DIR%\config\config.yaml" "%DEPLOY_DIR%\config\" >nul
if exist "%PROJECT_DIR%\models\model.onnx" copy "%PROJECT_DIR%\models\model.onnx" "%DEPLOY_DIR%\models\" >nul
if exist "%PROJECT_DIR%\models\manifest.json" copy "%PROJECT_DIR%\models\manifest.json" "%DEPLOY_DIR%\models\" >nul

echo [3/4] Copying documentation...
copy "%PROJECT_DIR%\README.md" "%DEPLOY_DIR%\" >nul
if exist "%PROJECT_DIR%\LICENSE" copy "%PROJECT_DIR%\LICENSE" "%DEPLOY_DIR%\" >nul
copy "%PROJECT_DIR%\docs\*.md" "%DEPLOY_DIR%\docs\" >nul

echo [4/4] Creating start scripts...
REM start.bat - OpenCV version
copy "%PROJECT_DIR%\start_qt.bat" "%DEPLOY_DIR%\" >nul

echo @echo off > "%DEPLOY_DIR%\start.bat"
echo cd /d "%%~dp0bin" >> "%DEPLOY_DIR%\start.bat"
echo. >> "%DEPLOY_DIR%\start.bat"
echo if exist defect_detection_qt.exe ( >> "%DEPLOY_DIR%\start.bat"
echo     start "" defect_detection_qt.exe ..\config\config.yaml >> "%DEPLOY_DIR%\start.bat"
echo ) else ( >> "%DEPLOY_DIR%\start.bat"
echo     if exist defect_detection.exe ( >> "%DEPLOY_DIR%\start.bat"
echo         start "" defect_detection.exe ..\config\config.yaml >> "%DEPLOY_DIR%\start.bat"
echo     ) else ( >> "%DEPLOY_DIR%\start.bat"
echo         echo ERROR: executable not found >> "%DEPLOY_DIR%\start.bat"
echo         pause >> "%DEPLOY_DIR%\start.bat"
echo     ) >> "%DEPLOY_DIR%\start.bat"
echo ) >> "%DEPLOY_DIR%\start.bat"

echo.
echo ========================================
echo  Package created: %DEPLOY_DIR%
echo ========================================
dir /a-d /s /b "%DEPLOY_DIR%"
echo.
echo To run: deploy\start.bat
