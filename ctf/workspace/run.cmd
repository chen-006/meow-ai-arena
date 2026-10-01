@echo off
setlocal
chcp 65001 >nul
rem 找一个 Python 3.10 以上，在本目录运行给定的脚本。不带参数时显示开发用时。
rem 用 call 调用：python3 等可能是 .cmd 转发脚本，不加 call 会一去不回。
set "PY="
for %%P in (python python3) do if not defined PY (
  call %%P -c "import sys; sys.exit(sys.version_info < (3, 10))" >nul 2>nul && set "PY=%%P"
)
if not defined PY (
  call py -3 -c "import sys; sys.exit(sys.version_info < (3, 10))" >nul 2>nul && set "PY=py -3"
)
if not defined PY if exist "%USERPROFILE%\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe" (
  set "PY="%USERPROFILE%\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe""
)
if not defined PY (
  echo 没有找到 Python 3.10 以上的版本。请把情况告诉组织方。
  exit /b 1
)
pushd "%~dp0"
if "%~1"=="" (call %PY% -X utf8 clock.py) else (call %PY% -X utf8 %*)
set "CODE=%errorlevel%"
popd
exit /b %CODE%
