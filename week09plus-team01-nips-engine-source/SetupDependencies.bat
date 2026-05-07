@echo off
setlocal EnableExtensions
chcp 65001 >nul

set "PROJECT_ROOT=%~dp0"
set "PROJECT_ROOT=%PROJECT_ROOT:~0,-1%"
set "VCPKG_DIR=%PROJECT_ROOT%\vcpkg"
set "VCPKG_EXE=%VCPKG_DIR%\vcpkg.exe"
set "TRIPLET=x64-windows"
set "MIN_MSVC_MAJOR=14"
set "MIN_MSVC_MINOR=44"

echo ============================================
echo  NipsEngine dependency setup
echo ============================================
echo.
echo Project root:
echo   %PROJECT_ROOT%
echo vcpkg root:
echo   %VCPKG_DIR%
echo install root:
echo   %PROJECT_ROOT%\vcpkg_installed
echo.

if not exist "%PROJECT_ROOT%\vcpkg.json" (
    echo ERROR: vcpkg.json was not found in the project root.
    echo Run this batch file from the repository root.
    echo.
    pause
    exit /b 1
)

echo Checking MSVC toolset...
powershell -NoProfile -ExecutionPolicy Bypass -File "%PROJECT_ROOT%\Scripts\CheckMsvcToolset.ps1" -MinMajor %MIN_MSVC_MAJOR% -MinMinor %MIN_MSVC_MINOR%
if errorlevel 1 (
    echo.
    pause
    exit /b 1
)

if not exist "%VCPKG_EXE%" if exist "%VCPKG_DIR%\bootstrap-vcpkg.bat" (
    echo vcpkg.exe was not found, but bootstrap-vcpkg.bat exists.
    echo Bootstrapping project-local vcpkg...
    call "%VCPKG_DIR%\bootstrap-vcpkg.bat"
    if errorlevel 1 (
        echo.
        echo ERROR: Failed to bootstrap vcpkg.
        pause
        exit /b 1
    )
)

if not exist "%VCPKG_EXE%" (
    echo Project-local vcpkg was not found.
    echo Installing vcpkg into:
    echo   %VCPKG_DIR%
    echo.

    if not exist "%PROJECT_ROOT%" (
        echo ERROR: Project root does not exist.
        pause
        exit /b 1
    )

    git clone https://github.com/microsoft/vcpkg "%VCPKG_DIR%"
    if errorlevel 1 (
        echo.
        echo ERROR: Failed to clone vcpkg.
        echo Check that Git is installed and network access is available.
        pause
        exit /b 1
    )

    call "%VCPKG_DIR%\bootstrap-vcpkg.bat"
    if errorlevel 1 (
        echo.
        echo ERROR: Failed to bootstrap vcpkg.
        pause
        exit /b 1
    )
)

if not exist "%VCPKG_EXE%" (
    echo.
    echo ERROR: vcpkg.exe still was not found after setup.
    echo   %VCPKG_EXE%
    pause
    exit /b 1
)

echo.
echo Installing dependencies from:
echo   %PROJECT_ROOT%\vcpkg.json
echo.
echo This project intentionally uses manifest mode from the repository root.
echo Do not install dependencies one by one with commands like "vcpkg install lua".
echo.

pushd "%PROJECT_ROOT%"
"%VCPKG_EXE%" install --triplet %TRIPLET%
set "INSTALL_RESULT=%ERRORLEVEL%"
popd

if not "%INSTALL_RESULT%"=="0" (
    echo.
    echo ERROR: vcpkg install failed.
    pause
    exit /b %INSTALL_RESULT%
)

echo.
echo Checking expected dependency files...

set "LUAJIT_HPP=%PROJECT_ROOT%\vcpkg_installed\%TRIPLET%\include\luajit\lua.hpp"
set "LUAJIT_LIB_DEBUG=%PROJECT_ROOT%\vcpkg_installed\%TRIPLET%\debug\lib\lua51.lib"
set "LUAJIT_LIB_RELEASE=%PROJECT_ROOT%\vcpkg_installed\%TRIPLET%\lib\lua51.lib"
set "LUAJIT_DLL_DEBUG=%PROJECT_ROOT%\vcpkg_installed\%TRIPLET%\debug\bin\lua51.dll"
set "LUAJIT_DLL_RELEASE=%PROJECT_ROOT%\vcpkg_installed\%TRIPLET%\bin\lua51.dll"
set "MINIAUDIO_H=%PROJECT_ROOT%\vcpkg_installed\%TRIPLET%\include\miniaudio.h"
set "JOLT_H=%PROJECT_ROOT%\vcpkg_installed\%TRIPLET%\include\Jolt\Jolt.h"
set "JOLT_LIB_DEBUG=%PROJECT_ROOT%\vcpkg_installed\%TRIPLET%\debug\lib\Jolt.lib"
set "JOLT_LIB_RELEASE=%PROJECT_ROOT%\vcpkg_installed\%TRIPLET%\lib\Jolt.lib"
set "RMLUI_H=%PROJECT_ROOT%\vcpkg_installed\%TRIPLET%\include\RmlUi\Core.h"
set "RMLUI_LIB_DEBUG=%PROJECT_ROOT%\vcpkg_installed\%TRIPLET%\debug\lib\rmlui.lib"
set "RMLUI_LIB_RELEASE=%PROJECT_ROOT%\vcpkg_installed\%TRIPLET%\lib\rmlui.lib"
set "RMLUI_DLL_DEBUG=%PROJECT_ROOT%\vcpkg_installed\%TRIPLET%\debug\bin\rmlui.dll"
set "RMLUI_DLL_RELEASE=%PROJECT_ROOT%\vcpkg_installed\%TRIPLET%\bin\rmlui.dll"
set "FREETYPE_H=%PROJECT_ROOT%\vcpkg_installed\%TRIPLET%\include\freetype2\ft2build.h"
set "FREETYPE_LIB_DEBUG=%PROJECT_ROOT%\vcpkg_installed\%TRIPLET%\debug\lib\freetype.lib"
set "FREETYPE_LIB_RELEASE=%PROJECT_ROOT%\vcpkg_installed\%TRIPLET%\lib\freetype.lib"
set "FREETYPE_DLL_DEBUG=%PROJECT_ROOT%\vcpkg_installed\%TRIPLET%\debug\bin\freetyped.dll"
set "FREETYPE_DLL_RELEASE=%PROJECT_ROOT%\vcpkg_installed\%TRIPLET%\bin\freetype.dll"

set "CHECK_FAILED=0"

if not exist "%LUAJIT_HPP%" (
    set "CHECK_FAILED=1"
    echo MISSING: %LUAJIT_HPP%
)
if not exist "%LUAJIT_LIB_DEBUG%" (
    set "CHECK_FAILED=1"
    echo MISSING: %LUAJIT_LIB_DEBUG%
)
if not exist "%LUAJIT_LIB_RELEASE%" (
    set "CHECK_FAILED=1"
    echo MISSING: %LUAJIT_LIB_RELEASE%
)
if not exist "%LUAJIT_DLL_DEBUG%" (
    set "CHECK_FAILED=1"
    echo MISSING: %LUAJIT_DLL_DEBUG%
)
if not exist "%LUAJIT_DLL_RELEASE%" (
    set "CHECK_FAILED=1"
    echo MISSING: %LUAJIT_DLL_RELEASE%
)
if not exist "%MINIAUDIO_H%" (
    set "CHECK_FAILED=1"
    echo MISSING: %MINIAUDIO_H%
)
if not exist "%JOLT_H%" (
    set "CHECK_FAILED=1"
    echo MISSING: %JOLT_H%
)
if not exist "%JOLT_LIB_DEBUG%" (
    set "CHECK_FAILED=1"
    echo MISSING: %JOLT_LIB_DEBUG%
)
if not exist "%JOLT_LIB_RELEASE%" (
    set "CHECK_FAILED=1"
    echo MISSING: %JOLT_LIB_RELEASE%
)
if not exist "%RMLUI_H%" (
    set "CHECK_FAILED=1"
    echo MISSING: %RMLUI_H%
)
if not exist "%RMLUI_LIB_DEBUG%" (
    set "CHECK_FAILED=1"
    echo MISSING: %RMLUI_LIB_DEBUG%
)
if not exist "%RMLUI_LIB_RELEASE%" (
    set "CHECK_FAILED=1"
    echo MISSING: %RMLUI_LIB_RELEASE%
)
if not exist "%RMLUI_DLL_DEBUG%" (
    set "CHECK_FAILED=1"
    echo MISSING: %RMLUI_DLL_DEBUG%
)
if not exist "%RMLUI_DLL_RELEASE%" (
    set "CHECK_FAILED=1"
    echo MISSING: %RMLUI_DLL_RELEASE%
)
if not exist "%FREETYPE_H%" (
    set "CHECK_FAILED=1"
    echo MISSING: %FREETYPE_H%
)
if not exist "%FREETYPE_LIB_DEBUG%" (
    set "CHECK_FAILED=1"
    echo MISSING: %FREETYPE_LIB_DEBUG%
)
if not exist "%FREETYPE_LIB_RELEASE%" (
    set "CHECK_FAILED=1"
    echo MISSING: %FREETYPE_LIB_RELEASE%
)
if not exist "%FREETYPE_DLL_DEBUG%" (
    set "CHECK_FAILED=1"
    echo MISSING: %FREETYPE_DLL_DEBUG%
)
if not exist "%FREETYPE_DLL_RELEASE%" (
    set "CHECK_FAILED=1"
    echo MISSING: %FREETYPE_DLL_RELEASE%
)

if exist "%PROJECT_ROOT%\NipsEngine\Jolt.lib" (
    set "CHECK_FAILED=1"
    echo ERROR: Unexpected local library found:
    echo   %PROJECT_ROOT%\NipsEngine\Jolt.lib
    echo Remove this file. The project must link Jolt from vcpkg_installed.
)

if "%CHECK_FAILED%"=="1" (
    echo.
    echo ERROR: Dependency setup completed, but expected files are missing or invalid.
    echo Try deleting vcpkg_installed and rerun this script.
    pause
    exit /b 1
)

echo.
echo NipsEngine dependencies are ready.
echo.
echo Next steps:
echo   1. GenerateProjectFiles.bat
echo   2. Open NipsEngine.sln
echo   3. Build Debug ^| x64, Release ^| x64, or Game ^| x64
echo.
pause
