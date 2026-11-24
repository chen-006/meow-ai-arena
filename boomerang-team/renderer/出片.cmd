@echo off
chcp 65001 >nul
rem 一条命令把第 12 集 5v5 回放渲染成 1080p 30 帧的 mp4（说明见 README.md）。
rem 用法：出片.cmd <回放文件> [-o 输出.mp4] [--procs 3] [--samples 12] [--roster a,b,...] [--frames a:b] [--keep]
if "%~1"=="" (
  echo 用法：出片.cmd ^<回放文件^> [-o 输出.mp4] [--procs 3] [--samples 12]
  exit /b 1
)
set "PY=%DART_PYTHON%"
if "%PY%"=="" set "PY=python"
"%PY%" "%~dp0src\make_video.py" %*
