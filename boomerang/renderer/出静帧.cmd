@echo off
chcp 65001 >nul
rem 出定妆照、角色阵容、地图主题全景（说明见 README.md）。例：出静帧.cmd lineup、出静帧.cmd themes
set "PY=%DART_PYTHON%"
if "%PY%"=="" set "PY=python"
"%PY%" "%~dp0src\stills.py" %*
