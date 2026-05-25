@echo off
setlocal enabledelayedexpansion

echo ========================================
echo   PCB Defect Detection - CI Pipeline
echo ========================================
echo.

echo ---- [1/3] Build ----
set "vcvars=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
set "cmake=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "builddir=%~dp0..\build"
set "srcdir=%~dp0.."

if not exist "%vcvars%" (
    echo ERROR: vcvars64.bat not found
    exit /b 1
)
if not exist "%cmake%" (
    echo ERROR: cmake not found
    exit /b 1
)

echo    Source: %srcdir%
echo    Build:  %builddir%

call "%vcvars%"
if errorlevel 1 exit /b 1

"%cmake%" -B "%builddir%" -S "%srcdir%" -G "Visual Studio 16 2019" -A x64 -DOpenCV_DIR="C:\Opencv2\opencv\build" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 (
    echo   [FAIL] CMake configure
    exit /b 1
)
echo   [PASS] CMake configure

"%cmake%" --build "%builddir%" --config Release
if errorlevel 1 (
    echo   [FAIL] Build
    exit /b 1
)
echo   [PASS] Build
echo.

echo ---- [2/3] Tests ----
set "python=C:\Users\29814\.conda\envs\yolov5\python.exe"
if not exist "%python%" set "python=python"

"%python%" "%~dp0run_tests.py"
set "test_exit=%errorlevel%"
echo.

echo ---- [3/3] Summary ----
if "%test_exit%"=="0" (
    echo   [PASS] All tests
) else (
    echo   [FAIL] Some tests failed
)

echo.
if "%test_exit%"=="0" (
    echo ========================================
    echo   CI PASSED
    echo ========================================
) else (
    echo ========================================
    echo   CI FAILED
    echo ========================================
)
exit /b %test_exit%
