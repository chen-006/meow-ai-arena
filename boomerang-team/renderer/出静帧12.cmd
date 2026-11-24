@echo off
chcp 65001 >nul
rem 第 12 集 5v5 关键时刻静帧：出静帧12.cmd <回放> <名字>@<时刻> ... [--samples 32] [--out 目录]
if "%~1"=="" (
  echo 用法：出静帧12.cmd ^<回放^> ^<名字^>@^<时刻^> [...] [--samples 32] [--out 目录]
  exit /b 1
)
set "PY=%DART_PYTHON%"
if "%PY%"=="" set "PY=python"
"%PY%" "%~dp0src\stills12.py" %*
