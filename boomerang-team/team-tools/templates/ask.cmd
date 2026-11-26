@echo off
python "%~dp0ask_entry.py" %*
exit /b %errorlevel%
