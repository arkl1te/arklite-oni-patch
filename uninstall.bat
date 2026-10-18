@echo off
rem Removes Arklite from Oni's AE folder and puts AE's original realbink.dll back (Windows).
rem
rem   uninstall.bat [/purge] ["C:\path\to\Oni\AE"]
rem
rem Kept unless /purge: AE\Arklite.ini (settings) and AE\Arklite.log. Always kept: custom levels in
rem AE\GameDataFolder\CustomLevels (an empty folder is removed), recordings (*.akr), persist.dat
rem and persist.dat.before-arklite (the save file as it was before the first install).
setlocal EnableExtensions
set "HERE=%~dp0"
set "PURGE="
if /i "%~1"=="/purge" ( set "PURGE=1" & shift )

set "AE=%~1"
for %%D in ("%HERE%.." "%HERE%..\AE" "%HERE%AE" "%ProgramFiles(x86)%\Oni\AE" "%ProgramFiles%\Oni\AE") do (
	if not defined AE if exist "%%~D\Oni.exe" if exist "%%~D\Arklite.dll" set "AE=%%~fD"
)
if not defined AE set /p "AE=Oni's AE folder (for example C:\Program Files (x86)\Oni\AE): "
if not defined AE goto fail
if "%AE:~-1%"=="\" set "AE=%AE:~0,-1%"
if not exist "%AE%\Oni.exe" ( echo error: "%AE%" has no Oni.exe; give the AE folder ^(Oni\AE^) & goto fail )

rem Original Bink back first: without a realbink.dll, Daodan (and Oni with it) won't start.
rem Arklite's loader is 3.5 KB, Bink about 280 KB.
if not exist "%AE%\realbink.dll" goto bink_restore
for %%F in ("%AE%\realbink.dll") do if %%~zF GEQ 65536 goto bink_is_original
:bink_restore
if not exist "%AE%\bink_orig.dll" ( echo error: AE\bink_orig.dll ^(the original realbink.dll^) is missing; run the AE Installer to restore it & goto fail )
move /y "%AE%\bink_orig.dll" "%AE%\realbink.dll" >nul || goto denied
goto bink_done
:bink_is_original
rem realbink.dll is already the original (the AE Installer put it back).
if exist "%AE%\bink_orig.dll" del /f "%AE%\bink_orig.dll"
:bink_done

if exist "%AE%\Arklite.dll" del /f "%AE%\Arklite.dll" || goto denied
rmdir "%AE%\GameDataFolder\CustomLevels" 2>nul
if defined PURGE del /f "%AE%\Arklite.ini" "%AE%\Arklite.log" 2>nul
echo Arklite removed from "%AE%".
if not defined PURGE echo Kept AE\Arklite.ini and AE\Arklite.log (/purge removes them).
goto end

:denied
echo error: couldn't change "%AE%". Close Oni, or run uninstall.bat as administrator.
:fail
echo Nothing was removed.
:end
endlocal
if "%~1"=="" pause
