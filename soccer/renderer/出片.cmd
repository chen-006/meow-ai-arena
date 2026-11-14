@echo off
chcp 65001 >nul
rem 一条命令把一局足球回放渲成 1080p 60 帧的无声 mp4（说明见 README.md）。
rem 用法：出片.cmd <回放.txt> [-o 输出.mp4] [--start 步] [--end 步] [--no-replay] [--still 帧号,...] [其它见 -h]
if "%~1"=="" (
  echo 用法：出片.cmd ^<回放.txt^> [-o 输出.mp4] [--start 步] [--end 步] [--no-replay] [--still 帧号,...]
  exit /b 1
)
set "PY=%A12_PYTHON%"
if "%PY%"=="" set "PY=python"
"%PY%" "%~dp0src\render_soccer.py" %*
