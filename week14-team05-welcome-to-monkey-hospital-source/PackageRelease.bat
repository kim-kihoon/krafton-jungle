@echo off
setlocal EnableExtensions EnableDelayedExpansion

set SOLUTION_DIR=%~dp0
set PROJECT_DIR=%SOLUTION_DIR%KraftonEngine
set BUILD_OUTPUT=%PROJECT_DIR%\Bin\Release
set RELEASE_DIR=%SOLUTION_DIR%ReleaseBuild
set RELEASE_BIN=%RELEASE_DIR%\Bin
set BUILD_INFO_FILE=%PROJECT_DIR%\Source\Engine\Platform\BuildInfo.h
set VERSION_NAME=%~1
set PRODUCT_NAME=Welcome to Monkey Hospital
set SYMBOL_PATH=srv*C:\SymbolCache*\\SYMBOL-SERVER\Symbols\Team7
set BUILD_INFO_SYMBOL_PATH=srv*C:\\SymbolCache*\\\\SYMBOL-SERVER\\Symbols\\Team7

if "%VERSION_NAME%"=="" (
    for /f "usebackq delims=" %%i in (`powershell -NoProfile -Command "Get-Date -Format 'yyyyMMdd_HHmm'"`) do set DEFAULT_VERSION_NAME=%%i
    echo No VersionName was provided.
    echo Press Enter to use the default version name: !DEFAULT_VERSION_NAME!
    set /p VERSION_NAME=VersionName: 
    if "!VERSION_NAME!"=="" set VERSION_NAME=!DEFAULT_VERSION_NAME!
)

set GIT_COMMIT=Unknown
for /f %%i in ('git -C "%SOLUTION_DIR%." rev-parse --short HEAD 2^>nul') do set GIT_COMMIT=%%i

set BUILD_TIME=Unknown
for /f "usebackq delims=" %%i in (`powershell -NoProfile -Command "Get-Date -Format 'yyyy-MM-dd HH:mm:ss'"`) do set BUILD_TIME=%%i

echo ============================================
echo  Package Release
echo  Version: %VERSION_NAME%
echo ============================================

echo.
echo [1/5] Generating build metadata...
echo Config: Release
echo Version: %VERSION_NAME%
echo Commit: %GIT_COMMIT%
echo BuildTime: %BUILD_TIME%

(
echo #pragma once
echo.
echo namespace BuildInfo
echo {
echo 	inline constexpr const char* ProductName = "%PRODUCT_NAME%";
echo 	inline constexpr const char* BuildConfig = "Release";
echo 	inline constexpr const char* BuildVersion = "%VERSION_NAME%";
echo 	inline constexpr const char* GitCommit = "%GIT_COMMIT%";
echo 	inline constexpr const char* SymbolPath = "%BUILD_INFO_SYMBOL_PATH%";
echo 	inline constexpr const char* BuildTime = "%BUILD_TIME%";
echo }
) > "%BUILD_INFO_FILE%"

set VSWHERE="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`%VSWHERE% -latest -property installationPath`) do set VS_PATH=%%i
if not defined VS_PATH (
    echo ERROR: Visual Studio installation not found.
    goto :Fail
)
call "%VS_PATH%\Common7\Tools\VsDevCmd.bat" -no_logo

echo.
echo [2/5] Building Release x64...
msbuild "%SOLUTION_DIR%KraftonEngine.sln" /p:Configuration=Release /p:Platform=x64 /m /v:minimal
if errorlevel 1 goto :Fail

echo.
echo [3/5] Preparing package directory...
if exist "%RELEASE_DIR%" rmdir /s /q "%RELEASE_DIR%"
mkdir "%RELEASE_DIR%"
mkdir "%RELEASE_BIN%"

echo.
echo [4/5] Copying runtime files...
copy "%BUILD_OUTPUT%\KraftonEngine.exe" "%RELEASE_BIN%\" >nul
xcopy "%BUILD_OUTPUT%\*.dll" "%RELEASE_BIN%\" /y /q >nul
xcopy "%PROJECT_DIR%\Shaders" "%RELEASE_DIR%\Shaders\" /e /i /q >nul
xcopy "%PROJECT_DIR%\Content" "%RELEASE_DIR%\Content\" /e /i /q >nul
xcopy "%PROJECT_DIR%\Settings" "%RELEASE_DIR%\Settings\" /e /i /q >nul

(
echo @echo off
echo cd /d "%%~dp0"
echo start "" "%%~dp0Bin\KraftonEngine.exe"
) > "%RELEASE_DIR%\Play.bat"

(
echo Product: %PRODUCT_NAME%
echo Config: Release
echo BuildVersion: %VERSION_NAME%
echo GitCommit: %GIT_COMMIT%
echo SymbolPath: %SYMBOL_PATH%
echo Executable: KraftonEngine.exe
echo BuildTime: %BUILD_TIME%
) > "%RELEASE_DIR%\BuildInfo.txt"

echo.
echo [5/5] Uploading symbols...
call "%SOLUTION_DIR%UploadSymbols.bat" "%VERSION_NAME%" --no-pause
if errorlevel 1 goto :Fail

echo.
echo ============================================
echo  Package complete: %RELEASE_DIR%
echo ============================================
echo.
pause

endlocal
exit /b 0

:Fail
echo.
echo ============================================
echo  Package failed.
echo ============================================
echo.
pause
endlocal
exit /b 1
