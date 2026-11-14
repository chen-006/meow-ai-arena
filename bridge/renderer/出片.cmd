@echo off
chcp 65001 >nul
rem 一条命令把一份造桥回放渲成 1080p 30 帧的无声 mp4（说明见 README.md）。
rem 用法：出片.cmd <回放.html|回放.json> [-o 输出.mp4] [--t0 秒] [--t1 秒] [--still 帧号,...]；多段：出片.cmd --config 剪辑表.json
if "%~1"=="" (
  echo 用法：出片.cmd ^<回放.html^|回放.json^> [-o 输出.mp4] [--t0 秒] [--t1 秒] [--still 帧号,...]
  exit /b 1
)
set "PY=%A12_PYTHON%"
if "%PY%"=="" set "PY=python"
"%PY%" "%~dp0src\render3d.py" %*
