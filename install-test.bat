@echo off
rem Installs this Arklite test build beside the release (Windows). Undo with uninstall-test.bat.
rem
rem   install-test.bat ["C:\path\to\Oni\AE"]
rem
rem Without a path it looks for Oni's AE folder: this file's own folder and the folders around it (the zip
rem extracted into AE or next to it), the Oni that Windows has registered, then Oni\AE, Games\Oni\AE and
rem Program Files\Oni\AE on every drive. More than one: it asks which; none: it asks for the path.
rem An AE folder has Oni.exe and AE's binkw32.dll (the Daodan DLL, over 1 MB; Oni\ has the plain Bink one).
rem
rem The release isn't touched:
rem   AE\arklite-test\Arklite.dll   this test build
rem   AE\arklite-test\build.txt     its commit and date (Options > More... shows them)
rem   AE\arklite-test\active        Oni runs the test build while this file is there
rem   AE\arklite-test\Arklite.ini, persist.dat, saves\
rem                                 its own options and saves: copies of the release's, made the first
rem                                 time only (a newer test build keeps the test ones)
rem   AE\realbink.dll               Arklite's loader, a version that can run the test build (it runs
rem                                 the release as before whenever arklite-test\active isn't there)
rem   AE\bink_orig.dll              AE's original realbink.dll (Bink), kept as install.bat keeps it
rem Options > More... switches between the test build and the release (from the next start).
setlocal EnableExtensions
set "HERE=%~dp0"
if "%HERE:~-1%"=="\" set "HERE=%HERE:~0,-1%"

for %%F in (Arklite.dll arklite_loader.dll build.txt) do if not exist "%HERE%\%%F" ( echo error: %%F is not next to install-test.bat & goto fail )

set "AE=%~1"
if defined AE goto have_ae
set "N=0"
rem 1. this file's folder and the ones around it
for %%D in ("%HERE%" "%HERE%\AE" "%HERE%\.." "%HERE%\..\AE" "%HERE%\..\.." "%HERE%\..\..\AE") do call :consider "%%~fD"
if %N% GTR 0 goto choose
rem 2. the Oni that Windows has registered (the original installer: ...\Oni\uninstal.log)
for %%K in ("HKLM\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Oni" "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Oni" "HKCU\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Oni") do call :registered %%K
rem 3. the usual places on every drive
for %%L in (C D E F G H I J K L M N O P Q R S T U V W X Y Z) do if exist "%%L:\" call :drive %%L
:choose
if %N%==0 goto ask
if %N%==1 ( set "AE=%C1%" & goto found )
echo Oni's AE folders found:
for /l %%I in (1,1,%N%) do call echo   %%I. %%C%%I%%
set "PICK="
set /p "PICK=Which one (1-%N%)? "
if not defined PICK goto fail
set "AE="
for /l %%I in (1,1,%N%) do if "%PICK%"=="%%I" call set "AE=%%C%%I%%"
if not defined AE ( echo error: "%PICK%" isn't one of them & goto fail )
goto have_ae
:found
echo Oni's AE folder: "%AE%"
goto have_ae
:ask
set /p "AE=Oni's AE folder (for example C:\Program Files (x86)\Oni\AE): "
if not defined AE goto fail
:have_ae
if "%AE:~-1%"=="\" set "AE=%AE:~0,-1%"
for %%A in ("%AE%") do set "AE=%%~fA"

if not exist "%AE%\Oni.exe" ( echo error: "%AE%" has no Oni.exe; give the AE folder ^(Oni\AE^) & goto fail )
if not exist "%AE%\binkw32.dll" ( echo error: "%AE%" has no binkw32.dll; install Anniversary Edition first, it brings the Daodan DLL & goto fail )
if not exist "%AE%\realbink.dll" if not exist "%AE%\bink_orig.dll" ( echo error: "%AE%" has no realbink.dll; give the AE folder ^(Oni\AE^), and install Anniversary Edition if it isn't there & goto fail )

rem The zip's files extracted straight into AE: AE\Arklite.dll is this test build (it replaced the release's).
set "SAME="
if /i "%AE%"=="%HERE%" set "SAME=1"
set "RELEASE=1"
if not exist "%AE%\Arklite.dll" set "RELEASE="
if defined SAME set "RELEASE="

rem Arklite's loader needs AE's original realbink.dll (Bink) as bink_orig.dll, as install.bat keeps it:
rem a realbink.dll over 64 KB is Bink (the loader is 4-5 KB).
set "SIZE="
for %%F in ("%AE%\realbink.dll") do if exist "%%~F" set "SIZE=%%~zF"
if defined SIZE (
	if %SIZE% LSS 65536 goto have_bink
	if exist "%AE%\bink_orig.dll" del /f "%AE%\bink_orig.dll"
	move /y "%AE%\realbink.dll" "%AE%\bink_orig.dll" >nul || goto denied
)
:have_bink
if not exist "%AE%\bink_orig.dll" for /d %%B in ("%AE%\AEInstaller\packages\*Daodan*") do call :restore_bink "%%~fB\plain\win_only\realbink.dll"
if not exist "%AE%\bink_orig.dll" call :restore_bink "%AE%\..\binkw32.dll"
if not exist "%AE%\bink_orig.dll" ( echo error: no original realbink.dll ^(Bink^) in "%AE%" to keep; run the AE Installer again, then install-test.bat & goto fail )

if not exist "%AE%\arklite-test" mkdir "%AE%\arklite-test" || goto denied
copy /y "%HERE%\Arklite.dll" "%AE%\arklite-test\Arklite.dll" >nul || goto denied
copy /y "%HERE%\build.txt" "%AE%\arklite-test\build.txt" >nul || goto denied
if defined SAME del /f "%AE%\Arklite.dll" >nul 2>nul

rem The release's options and saves, copied once: the test build reads and writes only these copies.
if exist "%AE%\arklite-test\copied-from-release.txt" goto copied
if exist "%AE%\Arklite.ini" copy /y "%AE%\Arklite.ini" "%AE%\arklite-test\Arklite.ini" >nul || goto denied
if exist "%AE%\persist.dat" copy /y "%AE%\persist.dat" "%AE%\arklite-test\persist.dat" >nul || goto denied
rem (xcopy: 1 is "no files", 2 and up are errors)
if exist "%AE%\saves" xcopy "%AE%\saves" "%AE%\arklite-test\saves\" /e /i /y /q >nul
if errorlevel 2 goto denied
echo The release's options and saves, copied for the test build> "%AE%\arklite-test\copied-from-release.txt"
echo Copied the release's options and saves for the test build ^(the release's stay as they are^).
:copied

copy /y "%HERE%\arklite_loader.dll" "%AE%\realbink.dll" >nul || goto denied
type nul > "%AE%\arklite-test\active" || goto denied
set /p DESC=<"%HERE%\build.txt"
echo Arklite test build %DESC% installed into "%AE%\arklite-test".
echo Start Oni as usual: the bottom of the screen shows the test build. Options ^> More... switches back
echo to the release; uninstall-test.bat removes the test build and its saves.
if defined SAME echo The zip was extracted straight into AE, so its Arklite.dll had replaced the release's there.
if not defined RELEASE echo Note: no Arklite release in "%AE%": switched off, Oni runs without Arklite. install.bat from a release zip puts one beside the test build.
goto end

:denied
echo error: couldn't write to "%AE%". Close Oni, or run install-test.bat as administrator ^(right-click, Run as administrator^).
:fail
echo The test build was not installed.
:end
endlocal
if "%~1"=="" pause
exit /b

rem ---- subroutines ----

rem An AE folder (Oni.exe, and AE's binkw32.dll: the Daodan DLL, over 1 MB) goes on the list once
:consider
if not exist "%~1\Oni.exe" exit /b
if not exist "%~1\binkw32.dll" exit /b
for %%F in ("%~1\binkw32.dll") do if %%~zF LSS 1000000 exit /b
for /l %%I in (1,1,%N%) do call :same "%~f1" %%I && exit /b
set /a N+=1
set "C%N%=%~f1"
exit /b

:same
call set "X=%%C%2%%"
if /i "%X%"=="%~1" exit /b 0
exit /b 1

rem The registered Oni: UninstallString "...unvise32.exe <Oni folder>\uninstal.log", or InstallLocation
:registered
set "V="
for /f "tokens=2,*" %%A in ('reg query "%~1" /v UninstallString 2^>nul ^| find /i "UninstallString"') do set "V=%%B"
rem (a goto, not a block: a block would expand %V:...% while V is still undefined, a syntax error)
if not defined V goto registered_location
set "V=%V:*.exe =%"
call :registered_dir "%V%"
:registered_location
set "V="
for /f "tokens=2,*" %%A in ('reg query "%~1" /v InstallLocation 2^>nul ^| find /i "InstallLocation"') do set "V=%%B"
if defined V call :consider "%V%\AE"
if defined V call :consider "%V%"
exit /b

:registered_dir
call :consider "%~dp1AE"
call :consider "%~dp1."
exit /b

:drive
for %%P in ("%1:\Oni\AE" "%1:\Games\Oni\AE" "%1:\Program Files (x86)\Oni\AE" "%1:\Program Files\Oni\AE") do call :consider "%%~P"
exit /b

:restore_bink
if exist "%AE%\bink_orig.dll" exit /b
if not exist "%~1" exit /b
if %~z1 LSS 65536 exit /b
copy /y "%~1" "%AE%\bink_orig.dll" >nul && echo AE's realbink.dll ^(Bink^) had been replaced; put it back from "%~f1".
exit /b
