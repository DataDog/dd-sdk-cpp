@echo off
setlocal enabledelayedexpansion

REM Symbolicate crash reports on Windows using dbh
REM
REM Usage:
REM   symbolicate.bat [crash_report_path]
REM
REM If no crash report path is provided, the script will use the most recent
REM crash report from the .crashes\ directory.

REM Determine the directory where this script is located
set SCRIPT_DIR=%~dp0
set SCRIPT_DIR=%SCRIPT_DIR:~0,-1%

REM Check if dbh.exe is available
where dbh.exe >nul 2>&1
if %errorlevel% neq 0 (
    REM Try adding Windows Debugging Tools to PATH
    set "DBG_PATH=%ProgramFiles(x86)%\Windows Kits\10\Debuggers\x64"
    if exist "!DBG_PATH!\dbh.exe" (
        set "PATH=!DBG_PATH!;%PATH%"
        echo Added !DBG_PATH! to PATH
        echo.
    ) else (
        echo Error: dbh.exe not found in PATH
        echo.
        echo Please install Windows Debugging Tools from one of:
        echo   - Windows SDK: https://developer.microsoft.com/windows/downloads/windows-sdk/
        echo   - WinDbg Preview from Microsoft Store
        echo.
        echo Or ensure dbh.exe is in your PATH.
        exit /b 1
    )
)

REM Optional crash report path
set CRASH_REPORT=%~1

REM Build the Python command
if "%CRASH_REPORT%"=="" (
    set PYTHON_CMD=python "%SCRIPT_DIR%\process_crash_report.py"
) else (
    set PYTHON_CMD=python "%SCRIPT_DIR%\process_crash_report.py" "%CRASH_REPORT%"
)

echo ========================================================================
echo Crash Report Analysis
echo ========================================================================
echo.

REM Show the human-readable crash report details
%PYTHON_CMD%

echo.
echo ========================================================================
echo Symbolicated Stack Trace
echo ========================================================================
echo.

REM Generate and execute symbolication commands
set frame_num=0

REM Process each line from the crash report with dbh format
for /f "usebackq delims=" %%L in (`%PYTHON_CMD% --format dbh 2^>nul`) do (
    set "line=%%L"

    REM Check if line starts with # (comment)
    echo !line! | findstr /b /c:"#" >nul
    if !errorlevel! equ 0 (
        REM Comment line - print as-is with indentation
        echo    !line!
    ) else (
        REM Execute dbh command and capture output
        for /f "usebackq delims=" %%O in (`%%L 2^>nul`) do (
            set "output=%%O"
            REM Print first line of dbh output (usually module!symbol+offset)
            if defined output (
                echo !frame_num!: !output!
                set output=
                goto :next_frame
            )
        )
        REM If no output, print unknown
        echo !frame_num!: ??
        :next_frame
        set /a frame_num+=1
    )
)

echo.
echo ========================================================================

endlocal
