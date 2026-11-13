@echo off
rem Starts an Arklite dedicated server: Oni without its window, and a console for the commands
rem (type help there). The installer puts this file in Oni\AE, next to Oni.exe. There, run:
rem
rem   server.bat <map folder in GameDataFolder\CustomLevels> [port]
rem
rem The server name, max players and limits are the Host dialog's last ones (AE\Arklite.ini [host]).
setlocal
cd /d "%~dp0"
if not exist Oni.exe ( echo error: put server.bat in Oni\AE, next to Oni.exe & pause & exit /b 1 )
if "%~1"=="" ( echo usage: server.bat MapFolder [port]   ^(a folder in GameDataFolder\CustomLevels^) & pause & exit /b 1 )
set "ARKLITE_DEDICATED=%~1"
if not "%~2"=="" set "ARKLITE_DEDICATED=%~1:%~2"
start "" Oni.exe -noswitch
