@echo off
REM build-mingw.bat - MinGW build script for Windows
REM Requires: MinGW-w64 with g++ installed

echo ========================================
echo Monitor Extender - MinGW Build Script
echo ========================================
echo.

REM Check for g++
where g++ >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] g++ not found in PATH
    echo Please install MinGW-w64 from: https://www.mingw-w64.org/
    echo Or use MSYS2: https://www.msys2.org/
    pause
    exit /b 1
)

echo [INFO] Compiler found
g++ --version | findstr "g++"
echo.

echo [INFO] Building server (console)...
g++ -std=c++17 -O3 server.cpp -o server.exe -ld3d11 -ldxgi -lgdi32 -lws2_32

if %ERRORLEVEL% EQU 0 (
    echo [SUCCESS] server.exe built successfully
) else (
    echo [ERROR] Server build failed
    pause
    exit /b 1
)
echo.

echo [INFO] Building server-tray (system tray)...
g++ -std=c++17 -O3 server-tray.cpp -o server-tray.exe -mwindows -ld3d11 -ldxgi -lgdi32 -lws2_32 -lshell32 -luser32

if %ERRORLEVEL% EQU 0 (
    echo [SUCCESS] server-tray.exe built successfully
    echo [INFO] Use server-tray.exe to run in system tray
) else (
    echo [ERROR] Server-tray build failed
    pause
    exit /b 1
)
echo.

echo ========================================
echo Build complete!
echo ========================================
echo.
echo Executables:
if exist "server.exe" echo   - server.exe (console version)
if exist "server-tray.exe" echo   - server-tray.exe (system tray version)
echo.
echo Usage:
echo   1. Console:     server.exe
echo   2. System Tray: server-tray.exe
echo.
pause