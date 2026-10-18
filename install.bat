@echo off
rem Installs Arklite into Oni Anniversary Edition's AE folder (Windows). Undo with uninstall.bat.
rem
rem   install.bat ["C:\path\to\Oni\AE"]
rem
rem Without a path it looks for AE around the folder this file is in (the zip extracted into Oni\AE,
rem into Oni, or next to Oni), then in the default install folder, then it asks.
rem   AE\Arklite.dll    the mod
rem   AE\realbink.dll   Arklite's loader (Daodan loads this name)
rem   AE\bink_orig.dll  AE's original realbink.dll (Bink), renamed; the loader forwards to it
setlocal EnableExtensions
set "HERE=%~dp0"
set "ONI_SHA256=363cf09684ca6b729b62f22f90701d7ebf9a33778e55b191f5040bccdcdd013d"

if not exist "%HERE%Arklite.dll" ( echo error: Arklite.dll is not next to install.bat & goto fail )
if not exist "%HERE%realbink.dll" ( echo error: realbink.dll is not next to install.bat & goto fail )

set "AE=%~1"
rem An AE folder has Oni.exe and realbink.dll (or bink_orig.dll once Arklite is in); Oni\ has no realbink.dll.
for %%D in ("%HERE%.." "%HERE%..\AE" "%HERE%AE" "%ProgramFiles(x86)%\Oni\AE" "%ProgramFiles%\Oni\AE") do (
	if not defined AE if exist "%%~D\Oni.exe" if exist "%%~D\realbink.dll" set "AE=%%~fD"
	if not defined AE if exist "%%~D\Oni.exe" if exist "%%~D\bink_orig.dll" set "AE=%%~fD"
)
if not defined AE set /p "AE=Oni's AE folder (for example C:\Program Files (x86)\Oni\AE): "
if not defined AE goto fail
if "%AE:~-1%"=="\" set "AE=%AE:~0,-1%"

if not exist "%AE%\Oni.exe" ( echo error: "%AE%" has no Oni.exe; give the AE folder ^(Oni\AE^), not Oni\ & goto fail )
if not exist "%AE%\realbink.dll" if not exist "%AE%\bink_orig.dll" ( echo error: "%AE%" has no realbink.dll; give the AE folder ^(Oni\AE^), and install Anniversary Edition if it isn't there & goto fail )
if not exist "%AE%\binkw32.dll" ( echo error: "%AE%" has no binkw32.dll; install Anniversary Edition first, it brings the Daodan DLL & goto fail )
certutil -hashfile "%AE%\Oni.exe" SHA256 2>nul | findstr /i /c:"%ONI_SHA256%" >nul || echo warning: AE\Oni.exe is not the one Arklite was made for; patches that don't match will be skipped ^(see AE\Arklite.log^)

rem Keep AE's original realbink.dll (Bink) as bink_orig.dll. When realbink.dll is not Arklite's
rem loader, it is the original: a first install, or the AE Installer put it back. The loader is
rem 3.5 KB and Bink about 280 KB, so the size tells them apart.
set "SIZE="
for %%F in ("%AE%\realbink.dll") do if exist "%%~F" set "SIZE=%%~zF"
if defined SIZE (
	if %SIZE% LSS 65536 goto have_bink
	if exist "%AE%\bink_orig.dll" del /f "%AE%\bink_orig.dll"
	move /y "%AE%\realbink.dll" "%AE%\bink_orig.dll" >nul || goto denied
)
:have_bink
if not exist "%AE%\bink_orig.dll" ( echo error: no original realbink.dll ^(Bink^) to keep; run the AE Installer again, then install.bat & goto fail )

rem One copy of the save file from before Arklite (custom levels can write save points to it).
if exist "%AE%\persist.dat" if not exist "%AE%\persist.dat.before-arklite" copy /y "%AE%\persist.dat" "%AE%\persist.dat.before-arklite" >nul

copy /y "%HERE%Arklite.dll" "%AE%\Arklite.dll" >nul || goto denied
copy /y "%HERE%realbink.dll" "%AE%\realbink.dll" >nul || goto denied
if not exist "%AE%\GameDataFolder\CustomLevels" mkdir "%AE%\GameDataFolder\CustomLevels"
echo Arklite installed into "%AE%".
echo Start Oni as usual; AE\Arklite.log lists what was patched.
goto end

:denied
echo error: couldn't write to "%AE%". Close Oni, or run install.bat as administrator ^(right-click, Run as administrator^).
:fail
echo Arklite was not installed.
:end
endlocal
if "%~1"=="" pause
