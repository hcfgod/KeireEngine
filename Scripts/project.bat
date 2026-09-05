@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "PAUSE_ON_EXIT=0"
set "WINDOWS_POWERSHELL=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"

if not exist "%WINDOWS_POWERSHELL%" (
    echo Required Windows PowerShell host was not found: %WINDOWS_POWERSHELL%
    exit /b 1
)

echo %cmdcmdline% | findstr /I /C:" /c " >nul
if not errorlevel 1 set "PAUSE_ON_EXIT=1"

"%WINDOWS_POWERSHELL%" -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%project.ps1" %*
set "EXIT_CODE=%ERRORLEVEL%"

if not "%EXIT_CODE%"=="0" (
    echo.
    echo Project script failed with exit code %EXIT_CODE%.
)

if "%PAUSE_ON_EXIT%"=="1" (
    echo.
    pause
)

exit /b %EXIT_CODE%
