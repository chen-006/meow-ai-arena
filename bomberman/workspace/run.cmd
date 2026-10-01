@echo off
setlocal
set "BOMB_RUNTIME="
if defined BOMB_PYTHON if exist "%BOMB_PYTHON%" set "BOMB_RUNTIME=%BOMB_PYTHON%"
if not defined BOMB_RUNTIME if exist "%~dp0.runtime\python.exe" set "BOMB_RUNTIME=%~dp0.runtime\python.exe"
if not defined BOMB_RUNTIME if exist "%USERPROFILE%\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe" set "BOMB_RUNTIME=%USERPROFILE%\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"
if defined BOMB_RUNTIME goto found
python3 -c "import sys; sys.exit(0 if sys.version_info >= (3,10) else 1)" >nul 2>nul
if not errorlevel 1 set "BOMB_RUNTIME=python3"
if defined BOMB_RUNTIME goto found
python -c "import sys; sys.exit(0 if sys.version_info >= (3,10) else 1)" >nul 2>nul
if not errorlevel 1 set "BOMB_RUNTIME=python"
if not defined BOMB_RUNTIME (
  echo Python 3.10+ not found. Set BOMB_PYTHON to the full executable path.
  exit /b 1
)
:found
call "%BOMB_RUNTIME%" -c "import sys; sys.exit(0 if sys.version_info >= (3,10) else 1)"
if errorlevel 1 exit /b 1
pushd "%~dp0"
if "%~1"=="" (
  call "%BOMB_RUNTIME%" -X utf8 clock.py
) else (
  call "%BOMB_RUNTIME%" -X utf8 %*
)
set "BOMB_EXIT=%errorlevel%"
popd
exit /b %BOMB_EXIT%
