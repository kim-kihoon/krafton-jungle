@echo off
setlocal

set DEBUGGER_PATH=C:\Program Files (x86)\Windows Kits\10\Debuggers\x64
set SRCSRV_PATH=%DEBUGGER_PATH%\srcsrv
set SYMBOL_STORE=\\SYMBOL-SERVER\Symbols\Team7
set PRODUCT_NAME=KraftonEngine_Team7
set RELEASE_BIN=C:\development\Jungle_Week12_Team7\KraftonEngine\Bin\Release
set LOG_DIR=C:\development\Jungle_Week12_Team7\KraftonEngine\Saves\SymbolLogs
set REPO_ROOT=C:\development\Jungle_Week12_Team7
set SOURCE_INDEX_SCRIPT=%REPO_ROOT%\Scripts\GenerateSrcSrvStream.ps1

set VERSION_NAME=%~1
set NO_PAUSE=%~2
set MISSING_DEBUG_TOOLS=0

if "%VERSION_NAME%"=="" (
    echo Usage: UploadSymbols.bat [VersionName]
    echo Example: UploadSymbols.bat Week12_Release_20260527_2130
    goto :Fail
)

call :CheckWindowsDebugTool "%DEBUGGER_PATH%\symstore.exe" "symstore.exe"
if errorlevel 1 set MISSING_DEBUG_TOOLS=1
call :CheckWindowsDebugTool "%SRCSRV_PATH%\srctool.exe" "srctool.exe"
if errorlevel 1 set MISSING_DEBUG_TOOLS=1
call :CheckWindowsDebugTool "%SRCSRV_PATH%\pdbstr.exe" "pdbstr.exe"
if errorlevel 1 set MISSING_DEBUG_TOOLS=1
if "%MISSING_DEBUG_TOOLS%"=="1" goto :MissingWindowsDebugTools

if not exist "%RELEASE_BIN%" (
    echo ERROR: Release bin folder not found: %RELEASE_BIN%
    goto :Fail
)

if not exist "%SOURCE_INDEX_SCRIPT%" (
    echo ERROR: Source indexing script not found: %SOURCE_INDEX_SCRIPT%
    goto :Fail
)

if not exist "%LOG_DIR%" mkdir "%LOG_DIR%"

echo ========================================
echo Source indexing Release PDBs...
echo Version: %VERSION_NAME%
echo ========================================

for %%F in ("%RELEASE_BIN%\*.pdb") do (
    if exist "%%~fF" call :SourceIndexPdb "%%~fF" "%VERSION_NAME%"
)
if errorlevel 1 goto :Fail

echo ========================================
echo Checking Release PDB source info...
echo Version: %VERSION_NAME%
echo Logs: %LOG_DIR%
echo ========================================

for %%F in ("%RELEASE_BIN%\*.pdb") do (
    if exist "%%~fF" call :CheckPdbSourceInfo "%%~fF" "Release" "%VERSION_NAME%"
)

if not exist "%SYMBOL_STORE%" (
    echo ERROR: Symbol store path not accessible: %SYMBOL_STORE%
    goto :Fail
)

echo ========================================
echo Uploading Release symbols...
echo Version: %VERSION_NAME%
echo ========================================

"%DEBUGGER_PATH%\symstore.exe" add /r /f "%RELEASE_BIN%\*.pdb" /s "%SYMBOL_STORE%" /t "%PRODUCT_NAME%" /v "%VERSION_NAME%"
if errorlevel 1 goto :Fail

"%DEBUGGER_PATH%\symstore.exe" add /r /f "%RELEASE_BIN%\*.exe" /s "%SYMBOL_STORE%" /t "%PRODUCT_NAME%" /v "%VERSION_NAME%"
if errorlevel 1 goto :Fail

"%DEBUGGER_PATH%\symstore.exe" add /r /f "%RELEASE_BIN%\*.dll" /s "%SYMBOL_STORE%" /t "%PRODUCT_NAME%" /v "%VERSION_NAME%"
if errorlevel 1 goto :Fail

echo.
echo Upload complete.
echo Source info logs: %LOG_DIR%
if /i not "%NO_PAUSE%"=="--no-pause" pause

endlocal
exit /b 0

:Fail
echo.
echo UploadSymbols failed.
echo.
if /i not "%NO_PAUSE%"=="--no-pause" pause
endlocal
exit /b 1

:MissingWindowsDebugTools
call :PrintWindowsSdkInstallHelp
goto :Fail

:CheckWindowsDebugTool
if exist "%~1" exit /b 0
echo ERROR: %~2 not found: %~1
exit /b 1

:PrintWindowsSdkInstallHelp
echo.
echo Required Windows Debugging Tools were not found.
echo.
echo Install or modify Windows SDK and enable:
echo   Debugging Tools for Windows
echo.
echo Expected tool locations:
echo   %DEBUGGER_PATH%\symstore.exe
echo   %SRCSRV_PATH%\srctool.exe
echo   %SRCSRV_PATH%\pdbstr.exe
echo.
echo Installer options:
echo   1. Visual Studio Installer - Individual components - Windows SDK
echo   2. Windows SDK standalone installer - Debugging Tools for Windows
echo.
echo After installing, reopen the terminal and run PackageRelease.bat again.
echo.
exit /b 0

:SourceIndexPdb
set PDB_FILE=%~1
set VERSION_NAME=%~2

echo.
echo Source indexing PDB:
echo %PDB_FILE%

powershell -NoProfile -ExecutionPolicy Bypass -File "%SOURCE_INDEX_SCRIPT%" -PdbFile "%PDB_FILE%" -RepoRoot "%REPO_ROOT%" -SrcSrvPath "%SRCSRV_PATH%" -LogDir "%LOG_DIR%" -VersionName "%VERSION_NAME%"
exit /b %ERRORLEVEL%

:CheckPdbSourceInfo
set PDB_FILE=%~1
set CONFIG_NAME=%~2
set VERSION_NAME=%~3
set PDB_NAME=%~n1

echo.
echo Checking PDB:
echo %PDB_FILE%

"%SRCSRV_PATH%\srctool.exe" -r "%PDB_FILE%" > "%LOG_DIR%\%CONFIG_NAME%_%VERSION_NAME%_%PDB_NAME%_sources.txt" 2>&1

"%SRCSRV_PATH%\srctool.exe" -u "%PDB_FILE%" > "%LOG_DIR%\%CONFIG_NAME%_%VERSION_NAME%_%PDB_NAME%_unindexed.txt" 2>&1

"%SRCSRV_PATH%\pdbstr.exe" -r -p:"%PDB_FILE%" -s:srcsrv > "%LOG_DIR%\%CONFIG_NAME%_%VERSION_NAME%_%PDB_NAME%_srcsrv.txt" 2>&1

exit /b 0
