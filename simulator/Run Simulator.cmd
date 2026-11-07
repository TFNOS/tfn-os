@echo off
rem The TFN OS simulator: build what changed, then start (simulator/README.md). Arguments go to run.py.
setlocal
cd /d "%~dp0.."
python simulator\run.py %*
if errorlevel 1 (
    echo.
    echo The simulator stopped with an error. See the message above.
    pause
)
