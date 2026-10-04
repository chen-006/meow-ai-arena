@echo off
setlocal
chcp 65001 >nul
rem 找一个 Python 3.10 以上，打开入口菜单。
set "PY="
for %%P in (python python3) do if not defined PY (
  call %%P -c "import sys; sys.exit(sys.version_info < (3, 10))" >nul 2>nul && set "PY=%%P"
)
if not defined PY (
  call py -3 -c "import sys; sys.exit(sys.version_info < (3, 10))" >nul 2>nul && set "PY=py -3"
)
if not defined PY (
  echo 没有找到 Python 3.10 以上的版本。
  echo 只想看回放的话，直接用浏览器打开 land\replays\index.html、bomberman\replays\、ctf\replays\、diplomacy\replays\ 里的网页即可。
  echo 想挑战 AI，请先到 https://www.python.org/downloads/ 安装 Python（安装时勾选 Add python.exe to PATH）。
  pause
  exit /b 1
)
pushd "%~dp0"
call %PY% -X utf8 start.py
popd
pause
