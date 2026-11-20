@echo off
chcp 65001 >nul
rem 一条命令把一份回放渲染成 1080p 30 帧的 mp4（说明见 README.md）。
rem 用法：出片.cmd <回放文件> [-o 输出.mp4] [--procs 3] [--samples 12] [--roster astra,fable,...] [--frames a:b] [--keep]
rem 中断后原样再跑一次即可接着渲（已渲好的帧会跳过）。
if "%~1"=="" (
  echo 用法：出片.cmd ^<回放文件^> [-o 输出.mp4] [--procs 3] [--samples 12] [--roster astra,fable,...]
  exit /b 1
)
set "PY=%DART_PYTHON%"
if "%PY%"=="" set "PY=python"
"%PY%" "%~dp0src\make_video.py" %*
