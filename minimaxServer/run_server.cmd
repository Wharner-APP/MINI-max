@echo off
setlocal
cd /d "%~dp0"
if not exist data mkdir data
if not exist .venv (
  echo [MINI max] Creating Python virtual environment...
  py -3 -m venv .venv
  if errorlevel 1 (
    echo Failed to create .venv. Install Python 3.11+ and enable the py launcher.
    pause
    exit /b 1
  )
)
call .venv\Scripts\python.exe -m pip install --upgrade pip
call .venv\Scripts\python.exe -m pip install -r requirements.txt
call .venv\Scripts\python.exe main.py
pause
