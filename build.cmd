@echo off
setlocal enabledelayedexpansion

echo ====================================
echo btop4win Build Script
echo ====================================
echo.

:: Optional flag to skip auto-restart
set "BTOP_NO_RESTART=%3"

:: Gracefully stop any running instance before building (to prevent linker LNK1104)
echo Checking for running btop4win.exe ...
rem Kill if running (ignore errors)
taskkill /F /IM btop4win.exe >nul 2>&1
rem Wait for process to exit completely (no parentheses to avoid cmd parse issues)
powershell -Command "try { while(Get-Process btop4win -ErrorAction SilentlyContinue){ Start-Sleep -Milliseconds 200 } } catch {}"

echo.
:: Check if external folder exists, if not download dependencies
if not exist "external" (
    echo External dependencies not found. Downloading...
    echo.
    
    :: Download LHM-CppExport dependencies
    echo Downloading LibreHardwareMonitor dependencies...
    powershell -Command "try { $release = Invoke-RestMethod -Uri 'https://api.github.com/repos/aristocratos/LHM-CppExport/releases/latest'; $url = $release.assets[0].browser_download_url; Write-Host 'Downloading from:' $url; Invoke-WebRequest -Uri $url -OutFile 'LHM-CPPdll-x64.zip' } catch { Write-Error 'Failed to download dependencies'; exit 1 }"
    
    if not exist "LHM-CPPdll-x64.zip" (
        echo ERROR: Failed to download dependencies
        exit /b 1
    )
    
    :: Extract dependencies
    echo Extracting dependencies...
    powershell -Command "Expand-Archive -Path 'LHM-CPPdll-x64.zip' -DestinationPath '.' -Force"
    
    :: Clean up zip file
    del "LHM-CPPdll-x64.zip"
    
    echo Dependencies downloaded and extracted successfully.
    echo.
)

:: Check for MSBuild
set "MSBUILD_PATH="
for %%i in (
    "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
    "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
    "C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe"
    "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe"
    "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
) do (
    if exist "%%~i" (
        set "MSBUILD_PATH=%%~i"
        goto :found_msbuild
    )
)

echo ERROR: MSBuild not found. Please install Visual Studio 2019/2022 or Visual Studio Build Tools.
echo.
echo Download from: https://visualstudio.microsoft.com/downloads/
exit /b 1

:found_msbuild
echo Found MSBuild: !MSBUILD_PATH!
echo.

:: Set default configuration and platform
set "CONFIG=Release-LHM"
set "PLATFORM=x64"

:: Allow command line overrides
if not "%1"=="" set "CONFIG=%1"
if not "%2"=="" set "PLATFORM=%2"

echo Building btop4win...
echo Configuration: %CONFIG%
echo Platform: %PLATFORM%
echo.

:: Build the project
"!MSBUILD_PATH!" btop4win.sln -p:Configuration="%CONFIG%" -p:Platform=%PLATFORM% -m

if %ERRORLEVEL% neq 0 (
    echo.
    echo ERROR: Build failed!
    exit /b %ERRORLEVEL%
)

echo.
echo ====================================
echo Build completed successfully!
echo ====================================
echo.
echo Output location: %PLATFORM%\%CONFIG%\
echo.

:: List output files
if exist "%PLATFORM%\%CONFIG%\btop4win.exe" (
    echo Built files:
    dir "%PLATFORM%\%CONFIG%\*.exe" /b
    dir "%PLATFORM%\%CONFIG%\*.dll" /b 2>nul
    echo.
    
    if "%CONFIG%"=="Release-LHM" (
        echo NOTE: This is the LibreHardwareMonitor version.
        echo      Run as Administrator for full functionality.
    )


    echo Launching %PLATFORM%\%CONFIG%\btop4win.exe ...
    start "" "%PLATFORM%\%CONFIG%\btop4win.exe"

) else (
    echo WARNING: Expected output file not found.
)

echo.
echo Usage: build.cmd [configuration] [platform] [NORESTART]
echo   configuration: Debug, Release, Debug-LHM, Release-LHM (default: Release-LHM)
echo   platform: Win32, x64 (default: x64)
echo   third arg: specify NORESTART to skip auto-relaunch

echo.
echo Examples:
echo   build.cmd
echo   build.cmd Release x64
echo   build.cmd Debug-LHM x64

echo.
echo Note: To test WebSocket functionality, copy test_websocket.conf to btop.conf
echo       and open test_websocket_client.html in a web browser after starting btop4win.
