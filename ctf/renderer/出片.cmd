@echo off
chcp 65001 >nul
rem 一条命令把一局回放渲染成 1920x1080 的 mp4（说明见 README.md）。
rem 用法：出片.cmd ^<回放^> [-o 输出.mp4] [--from 秒] [--to 秒] [--fps 30] [--still 回合]
if "%~1"=="" (
  echo 用法：出片.cmd ^<回放^> [-o 输出.mp4] [--from 秒] [--to 秒] [--fps 30] [--still 回合]
  exit /b 1
)
set "PY=%ARENA_PYTHON%"
if "%PY%"=="" set "PY=python"
"%PY%" "%~dp0src\make_video.py" %*
