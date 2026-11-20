@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Run-CrossTech_OneToOne_AppInsights_To_DataDog.ps1" %*
exit /b %ERRORLEVEL%