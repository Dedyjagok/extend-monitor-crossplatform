@echo off
echo Building with MinGW-w64...
echo.

REM Check for g++
where g++ >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] g++ not found
    echo Install MinGW-w64 or add it to PATH
    pause
    exit /b 1
)

echo [INFO] Building server...
g++ -std=c++17 -O3 server.cpp -o server.exe -ld3d11 -ldxgi -lws2_32

if %ERRORLEVEL% EQU 0 (
    echo [SUCCESS] server.exe built
) else (
    echo [ERROR] Build failed
    pause
    exit /b 1
)

echo.
echo Build complete!
echo Run: server.exe
pause