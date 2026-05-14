@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "ROOT_DIR=%~dp0"
set "SOLUTION=%ROOT_DIR%NipsEngine.sln"
set "PROJECT_DIR=%ROOT_DIR%NipsEngine"
set "BUILD_DIR=%PROJECT_DIR%\Bin\Release"
set "PACKAGE_DIR=%ROOT_DIR%EditorReleaseBuild"
set "CONFIGURATION=Release"
set "PLATFORM=x64"
set "EXE_NAME=NipsEngine.exe"

echo ============================================
echo  NipsEngine Editor Release x64 Build
echo ============================================
echo.

if not exist "%SOLUTION%" (
    echo Solution not found: "%SOLUTION%"
    pause
    exit /b 1
)

where msbuild.exe >nul 2>nul
if errorlevel 1 (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if not exist "!VSWHERE!" (
        echo vswhere.exe not found. Install Visual Studio 2022 with C++ build tools.
        pause
        exit /b 1
    )

    for /f "usebackq delims=" %%I in (`"!VSWHERE!" -latest -products * -requires Microsoft.Component.MSBuild -property installationPath`) do set "VS_PATH=%%I"
    if not defined VS_PATH (
        echo Visual Studio with MSBuild was not found.
        pause
        exit /b 1
    )

    call "!VS_PATH!\Common7\Tools\VsDevCmd.bat" -no_logo -arch=x64 -host_arch=x64
    if errorlevel 1 (
        echo Failed to load the Visual Studio build environment.
        pause
        exit /b 1
    )
)

echo [1/3] Building %CONFIGURATION%^|%PLATFORM% editor target...
msbuild "%SOLUTION%" /t:Build /p:Configuration=%CONFIGURATION% /p:Platform=%PLATFORM% /m /v:minimal
if errorlevel 1 (
    echo.
    echo Build failed.
    pause
    exit /b 1
)

if not exist "%BUILD_DIR%\%EXE_NAME%" (
    echo.
    echo Build output not found: "%BUILD_DIR%\%EXE_NAME%"
    pause
    exit /b 1
)

echo.
echo [2/3] Preparing standalone folder...
if "%PACKAGE_DIR%"=="" (
    echo Package directory is empty.
    pause
    exit /b 1
)

if /i "%PACKAGE_DIR%"=="%ROOT_DIR%" (
    echo Refusing to clean repository root.
    pause
    exit /b 1
)

if exist "%PACKAGE_DIR%" rmdir /s /q "%PACKAGE_DIR%"
mkdir "%PACKAGE_DIR%"
if errorlevel 1 (
    echo Failed to create package directory: "%PACKAGE_DIR%"
    pause
    exit /b 1
)

echo.
echo [3/3] Copying editor standalone files...
copy /y "%BUILD_DIR%\%EXE_NAME%" "%PACKAGE_DIR%\" >nul
if errorlevel 1 goto :copy_failed

if exist "%BUILD_DIR%\*.dll" (
    xcopy "%BUILD_DIR%\*.dll" "%PACKAGE_DIR%\" /Y /D /Q >nul
    if errorlevel 1 goto :copy_failed
)

if exist "%PROJECT_DIR%\imgui.ini" (
    copy /y "%PROJECT_DIR%\imgui.ini" "%PACKAGE_DIR%\" >nul
    if errorlevel 1 goto :copy_failed
)

if exist "%PROJECT_DIR%\imgui_editor.ini" (
    copy /y "%PROJECT_DIR%\imgui_editor.ini" "%PACKAGE_DIR%\" >nul
    if errorlevel 1 goto :copy_failed
)

call :copy_dir Shaders
if errorlevel 1 goto :copy_failed

call :copy_dir Asset
if errorlevel 1 goto :copy_failed

call :copy_dir Settings
if errorlevel 1 goto :copy_failed

call :copy_dir Saves
if errorlevel 1 goto :copy_failed

echo.
echo ============================================
echo  Editor Release build complete
echo  Output: "%PACKAGE_DIR%"
echo ============================================
echo.
pause
exit /b 0

:copy_dir
set "DIR_NAME=%~1"
if exist "%PROJECT_DIR%\%DIR_NAME%\" (
    xcopy "%PROJECT_DIR%\%DIR_NAME%" "%PACKAGE_DIR%\%DIR_NAME%\" /E /I /Y /Q >nul
    if errorlevel 1 exit /b 1
)
exit /b 0

:copy_failed
echo.
echo Failed to copy editor standalone files.
pause
exit /b 1
