@echo off
chcp 65001 > nul
setlocal enabledelayedexpansion

REM =======================================================================
REM 1. Build Configuration & Tool Path Arguments
REM =======================================================================
REM %1 : 빌드 구성 (Debug / Release) - 비어있으면 기본값 Debug
set CONFIG=Debug
if "%~1" NEQ "" (
    set CONFIG=%~1
)

REM %2 : 커스텀 SDK 도구 경로 (선택 사항)
set CUSTOM_TOOL_PATH=%~2

echo =======================================================================
echo  [Symbol Publisher] Target Configuration: %CONFIG%
if "%CUSTOM_TOOL_PATH%" NEQ "" (
    echo  [Symbol Publisher] Custom Tool Path   : %CUSTOM_TOOL_PATH%
)
echo =======================================================================

REM =======================================================================
REM Path Configuration
REM =======================================================================
set ROOT_PATH=%~dp0..
for %%i in ("%ROOT_PATH%") do set ROOT_PATH=%%~fi

REM =======================================================================
REM Project & Dynamic Paths
REM =======================================================================
set PROJECT=KraftonEngine

set BUILD_PATH=%ROOT_PATH%\%PROJECT%\Bin\%CONFIG%
set EXE=%BUILD_PATH%\%PROJECT%.exe
set PDB=%BUILD_PATH%\%PROJECT%.pdb
set SRCINFO=%BUILD_PATH%\srcsrv.txt
set TEMP_RAW_LIST=%BUILD_PATH%\raw_files.txt
set PS1=%~dp0gen_srcsrv.ps1

REM =======================================================================
REM Servers
REM =======================================================================
set SOURCE_SERVER=\\172.21.10.44\sources
set SYMBOL_SERVER=\\172.21.10.44\symbols

REM =======================================================================
REM 2. Tools Path Resolution (입력값 검증 또는 자동 탐색)
REM =======================================================================
set SDK_TOOLS=

REM 사용자가 수동으로 도구 경로를 넘겨준 경우 최우선 검증
if "%CUSTOM_TOOL_PATH%" NEQ "" (
    if exist "%CUSTOM_TOOL_PATH%\srcsrv\srctool.exe" (
        set SDK_TOOLS=%CUSTOM_TOOL_PATH%
        goto :TOOLS_FOUND
    ) else (
        echo [WARNING] 입력된 커스텀 경로에 srctool.exe가 없습니다. 자동 탐색을 시도합니다.
    )
)

REM 자동 탐색 로직 (기존 경로 리스트 순회)
set POSSIBLE_PATHS=(^
    "C:\Program Files (x86)\Windows Kits\10\Debuggers\x64"^
    "C:\Program Files (x86)\Windows Kits\10\Debuggers\x86"^
    "C:\Program Files (x86)\Windows Kits\10\Debuggers\arm64"^
    "C:\Program Files\Windows Kits\11\Debuggers\x64"^
    "C:\Program Files\Windows Kits\10\Debuggers\x64"^
    "D:\Windows Kits\10\Debuggers\x64"^
)

for %%p in %POSSIBLE_PATHS% do (
    if exist "%%~p\srcsrv\srctool.exe" (
        set SDK_TOOLS=%%~p
        goto :TOOLS_FOUND
    )
)

:TOOLS_NOT_FOUND
echo -----------------------------------------------------------------------
echo [ERROR] Windows SDK Debugging Tools를 찾을 수 없습니다.
echo [원인] 팀원 컴퓨터에 "Debugging Tools for Windows"가 설치되어 있지 않거나
echo        지정된 커스텀 경로가 올바르지 않습니다.
echo [해결방법] 
echo   1. 바로가기 .bat 파일을 편집기(메모장)로 열어 수동 경로를 세팅하거나
echo   2. Visual Studio Installer - [개별 구성 요소]에서 
echo      [Debugging Tools for Windows]를 체크하여 설치하세요.
echo -----------------------------------------------------------------------
pause
exit /b 1

:TOOLS_FOUND
set SRCSRV_TOOLS=%SDK_TOOLS%\srcsrv
echo [INFO] Using SDK Tools at: %SDK_TOOLS%

echo =========================================
echo 0. Check files
echo =========================================

if not exist "%BUILD_PATH%" mkdir "%BUILD_PATH%"

if not exist "%PDB%" (
    echo [ERROR] PDB not found at: %PDB%
    echo [ERROR] %CONFIG% 빌드가 먼저 완료되었는지 확인하세요.
    exit /b 1
)
if not exist "%PS1%" (
    echo [ERROR] gen_srcsrv.ps1 not found: %PS1%
    exit /b 1
)
if not exist "%EXE%" (
    echo [ERROR] EXE not found: %EXE%
    echo [ERROR] EXE is required to extract GUID.
    exit /b 1
)

REM =======================================================================
REM 0-1. Extract PDB GUID via PowerShell (cv info 방식)
REM =======================================================================
echo =========================================
echo 0-1. Extract PDB GUID
echo =========================================

for /f "usebackq delims=" %%G in (`powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "try {" ^
    "  $b = [IO.File]::ReadAllBytes('%PDB%');" ^
    "  $magic = [Text.Encoding]::ASCII.GetString($b[0..28]);" ^
    "  if ($magic -notlike 'Microsoft C/C++*') { throw 'bad magic' }" ^
    "  $pageSize  = [BitConverter]::ToInt32($b, 32);" ^
    "  $rootPage  = [BitConverter]::ToInt32($b, 40);" ^
    "  $rootOff   = $rootPage * $pageSize;" ^
    "  $numStream = [BitConverter]::ToInt32($b, $rootOff);" ^
    "  $pdbOff    = $rootOff + 4 + $numStream * 4 + 4;" ^
    "  $guidBytes = $b[$pdbOff..($pdbOff+15)];" ^
    "  $guid = [guid]$guidBytes;" ^
    "  Write-Output $guid.ToString('N').ToUpper()" ^
    "} catch {" ^
    "  Write-Output (Get-Date -Format 'yyyyMMdd_HHmmss')" ^
    "}"`) do set BUILD_GUID=%%G

echo [INFO] Build GUID / Key: !BUILD_GUID!

if "!BUILD_GUID!"=="" (
    echo [ERROR] Failed to generate GUID key.
    exit /b 1
)

REM =======================================================================
REM 1. Copy Source Files to Versioned Folder
REM =======================================================================
echo =========================================
echo 1. Copy Source to Source Server [!BUILD_GUID!]
echo =========================================

set VERSIONED_SRC=%SOURCE_SERVER%\%PROJECT%\!BUILD_GUID!

xcopy "%EXE%" "!VERSIONED_SRC!\Bin\" /Y /I
xcopy "%PDB%" "!VERSIONED_SRC!\Bin\" /Y /I

xcopy "%ROOT_PATH%\%PROJECT%\*.cpp"        "!VERSIONED_SRC!\"              /Y /D /I
xcopy "%ROOT_PATH%\%PROJECT%\*.h"          "!VERSIONED_SRC!\"              /Y /D /I
xcopy "%ROOT_PATH%\%PROJECT%\Source\*"     "!VERSIONED_SRC!\Source\"       /E /I /Y /D
xcopy "%ROOT_PATH%\%PROJECT%\Shaders\*"    "!VERSIONED_SRC!\Shaders\"      /E /I /Y /D
xcopy "%ROOT_PATH%\%PROJECT%\ThirdParty\*" "!VERSIONED_SRC!\ThirdParty\"   /E /I /Y /D

if errorlevel 1 (echo [ERROR] xcopy failed & exit /b 1)

REM =======================================================================
REM 2. Generate srcsrv.txt pointing to versioned folder
REM =======================================================================
echo =========================================
echo 2. Generating Source Index (GUID: !BUILD_GUID!)
echo =========================================

"%SRCSRV_TOOLS%\srctool.exe" -r "%PDB%" > "%TEMP_RAW_LIST%"

echo [DEBUG] raw_files.txt 앞 5줄:
for /f "tokens=*" %%a in ('type "%TEMP_RAW_LIST%" ^| findstr /n "." ^| findstr /b "[1-5]:"') do echo %%a

powershell -NoProfile -ExecutionPolicy Bypass -File "%PS1%" ^
    -Project      "%PROJECT%"         ^
    -SourceServer "%SOURCE_SERVER%"   ^
    -BuildGuid    "!BUILD_GUID!"      ^
    -RawList      "%TEMP_RAW_LIST%"   ^
    -SrcInfo      "%SRCINFO%"

if errorlevel 1 (
    echo [ERROR] No source files indexed or PowerShell failed.
    exit /b 1
)

if exist "%TEMP_RAW_LIST%" del "%TEMP_RAW_LIST%"

echo [INFO] Running pdbstr...
"%SRCSRV_TOOLS%\pdbstr.exe" -w -p:"%PDB%" -s:srcsrv -i:"%SRCINFO%"
if errorlevel 1 (echo [ERROR] pdbstr injection failed & exit /b 1)

REM =======================================================================
REM 3. Upload Indexed PDB and EXE to Symbol Server
REM =======================================================================
echo =========================================
echo 3. Upload to Symbol Server
echo =========================================

set NEED_PDB_UPLOAD=1
if exist "%SYMBOL_SERVER%\000Admin\history.txt" (
    findstr /I /C:"\"%PROJECT%.pdb\"" "%SYMBOL_SERVER%\000Admin\history.txt" >nul 2>&1
    if !errorlevel! equ 0 (set NEED_PDB_UPLOAD=0)
)

if !NEED_PDB_UPLOAD! equ 0 (
    echo [SKIP] PDB already in Symbol Server.
) else (
    echo [UPLOAD] Uploading Indexed PDB...
    "%SDK_TOOLS%\symstore.exe" add /f "%PDB%" /s "%SYMBOL_SERVER%" /t "%PROJECT%"
    if !errorlevel! geq 1 (echo [ERROR] symstore PDB failed & exit /b 1)
)

if exist "%EXE%" (
    set NEED_EXE_UPLOAD=1
    if exist "%SYMBOL_SERVER%\000Admin\history.txt" (
        findstr /I /C:"\"%PROJECT%.exe\"" "%SYMBOL_SERVER%\000Admin\history.txt" >nul 2>&1
        if !errorlevel! equ 0 (set NEED_EXE_UPLOAD=0)
    )
    if !NEED_EXE_UPLOAD! equ 0 (
        echo [SKIP] EXE already in Symbol Server.
    ) else (
        echo [UPLOAD] Uploading EXE...
        "%SDK_TOOLS%\symstore.exe" add /f "%EXE%" /s "%SYMBOL_SERVER%" /t "%PROJECT%"
        if !errorlevel! geq 1 (echo [ERROR] symstore EXE failed & exit /b 1)
    )
)

REM =======================================================================
REM 4. Verify
REM =======================================================================
echo =========================================
echo 4. Verify PDB Indexing
echo =========================================

"%SRCSRV_TOOLS%\srctool.exe" "%PDB%"
"%SRCSRV_TOOLS%\pdbstr.exe" -r -p:"%PDB%" -s:srcsrv

echo =========================================
echo DONE  [CONFIG: %CONFIG%] [GUID: !BUILD_GUID!]
echo =========================================
pause
endlocal