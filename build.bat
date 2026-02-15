@echo off
REM build.bat - Quick build script for Windows

echo ========================================
echo Monitor Extender - Build Script
echo ========================================
echo.

REM Check for Visual Studio
where cl >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Visual Studio not found in PATH
    echo Please run this from "Developer Command Prompt for VS"
    echo Or run: "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
    pause
    exit /b 1
)

echo [INFO] Compiler found: Visual Studio
echo.

REM Check for SDL2 using vcpkg or system install
set SDL2_FOUND=0
if exist "C:\SDL2" (
    set SDL2_FOUND=1
    set SDL2_INCLUDE=C:\SDL2\include
    set SDL2_LIB=C:\SDL2\lib\x64
) else if exist "C:\vcpkg\installed\x64-windows\include\SDL2" (
    set SDL2_FOUND=1
    set SDL2_INCLUDE=C:\vcpkg\installed\x64-windows\include
    set SDL2_LIB=C:\vcpkg\installed\x64-windows\lib
)

if %SDL2_FOUND%==0 (
    echo [WARNING] SDL2 not found in common locations
    echo Download from: https://github.com/libsdl-org/SDL/releases
    echo Extract to C:\SDL2
    echo OR install via vcpkg: vcpkg install sdl2:x64-windows
    echo.
    echo Building server only...
    goto :build_server
)

:build_client
echo [INFO] Building client...
echo [INFO] SDL2 Include: %SDL2_INCLUDE%
echo [INFO] SDL2 Lib: %SDL2_LIB%
cl /std:c++17 /O2 /EHsc /Fe:client.exe client.cpp ^
   /I"%SDL2_INCLUDE%" ^
   /link /LIBPATH:"%SDL2_LIB%" SDL2.lib SDL2main.lib ws2_32.lib ^
   /SUBSYSTEM:CONSOLE

if %ERRORLEVEL% EQU 0 (
    echo [SUCCESS] client.exe built successfully
    echo.
    echo [INFO] Make sure SDL2.dll is in the same folder as client.exe
    echo [INFO] Copy from: %SDL2_LIB%\..\bin\SDL2.dll
) else (
    echo [ERROR] Client build failed
)
echo.


:build_server
echo [INFO] Building server (console)...
cl /std:c++17 /O2 /EHsc /Fe:server.exe server.cpp ^
   d3d11.lib dxgi.lib ws2_32.lib

if %ERRORLEVEL% EQU 0 (
    echo [SUCCESS] server.exe built successfully
) else (
    echo [ERROR] Server build failed
)
echo.

echo [INFO] Building server-tray (system tray)...
cl /std:c++17 /O2 /EHsc /Fe:server-tray.exe server-tray.cpp ^
   d3d11.lib dxgi.lib ws2_32.lib shell32.lib user32.lib

if %ERRORLEVEL% EQU 0 (
    echo [SUCCESS] server-tray.exe built successfully
    echo [INFO] Use server-tray.exe to run in system tray
) else (
    echo [ERROR] Server-tray build failed
)
echo.

echo ========================================
echo Build complete!
echo ========================================
echo.
echo Executables:
if exist "server.exe" echo   - server.exe (run on Windows with extended monitor)
if exist "client.exe" echo   - client.exe (run on receiving device)
echo.
echo Usage:
echo   1. Start server:  server.exe
echo   2. Start client:  client.exe ^<server_ip^>
echo.
echo Note: If client.exe fails to run, copy SDL2.dll to this folder
echo.
pause