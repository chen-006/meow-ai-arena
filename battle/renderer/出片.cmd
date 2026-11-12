@echo off
chcp 65001 >nul
rem 一条命令把一份回放渲染成 1080p 30 帧的 mp4（说明见 README.md）。
rem 用法：出片.cmd <回放.wrp> [-o 输出.mp4] [--from 秒] [--to 秒] [--procs 3] [--lord default] [--shots 机位表.json]
rem 中断后原样再跑一次即可接着渲（已渲好的帧会跳过）。
if "%~1"=="" (
  echo 用法：出片.cmd ^<回放.wrp^> [-o 输出.mp4] [--from 秒] [--to 秒] [--procs 3] [--lord default] [--shots 机位表.json]
  exit /b 1
)
set "PY=%A12_PYTHON%"
if "%PY%"=="" set "PY=python"
"%PY%" "%~dp0src\make_video.py" %*
