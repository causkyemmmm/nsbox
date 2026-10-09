@echo off
setlocal
echo ========================================================
echo  Building switch-tvbox for Nintendo Switch (OpenGL)
echo  Using devkitpro/devkita64 Docker container...
echo ========================================================

where docker >nul 2>nul
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Docker not found in PATH!
    echo Please ensure Docker Desktop is installed and running.
    pause
    exit /b 1
)

cd /d "%~dp0\.."

docker run --rm -v "%cd%":/data -w /data devkitpro/devkita64:20251117 bash -c "/data/scripts/build_switch.sh"

if %ERRORLEVEL% equ 0 (
    echo.
    echo ========================================================
    echo  [SUCCESS] Build completed!
    echo  Binary: cmake-build-switch\switch-tvbox.nro
    echo ========================================================
) else (
    echo.
    echo [ERROR] Build failed with code %ERRORLEVEL%.
)

pause
