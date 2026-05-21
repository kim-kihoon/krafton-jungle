@echo off
setlocal EnableExtensions

:: JSEngine.sln 및 Visual Studio 프로젝트 파일 재생성 진입점
:: 모든 개발자가 같은 런타임을 쓰도록 저장소에 포함된 Python 우선 사용
set "SCRIPT_DIR=%~dp0"
set "BUNDLED_PYTHON=%SCRIPT_DIR%Scripts\python\python.exe"
set "GENERATOR=%SCRIPT_DIR%Scripts\GenerateProjectFiles.py"

if not exist "%GENERATOR%" (
    echo GenerateProjectFiles.py not found: %GENERATOR%
    goto :Fail
)

if exist "%BUNDLED_PYTHON%" (
    "%BUNDLED_PYTHON%" "%GENERATOR%" %*
    goto :AfterRun
)

:: 저장소 내 Python이 없을 때만 시스템 Python으로 대체 실행
where py >nul 2>nul
if not errorlevel 1 (
    py -3 "%GENERATOR%" %*
    goto :AfterRun
)

where python >nul 2>nul
if not errorlevel 1 (
    python "%GENERATOR%" %*
    goto :AfterRun
)

echo Python not found. Expected bundled Python at:
echo   %BUNDLED_PYTHON%
goto :Fail

:AfterRun
if errorlevel 1 goto :Fail
echo.
echo Project files generated successfully.
pause
exit /b 0

:Fail
echo.
echo GenerateProjectFiles failed.
pause
exit /b 1
