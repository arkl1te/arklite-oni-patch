@echo off
rem Installs this Arklite test build beside the release (Windows). Undo with uninstall-test.bat.
rem
rem   install-test.bat ["C:\path\to\Oni\AE"]
rem
rem Needs Arklite's release installed (install.bat from a release zip). The release isn't touched:
rem   AE\arklite-test\Arklite.dll   this test build
rem   AE\arklite-test\build.txt     its commit and date (Options > More... shows them)
rem   AE\arklite-test\active        Oni runs the test build while this file is there
rem   AE\arklite-test\Arklite.ini, persist.dat, saves\
rem                                 its own options and saves: copies of the release's, made the first
rem                                 time only (a newer test build keeps the test ones)
rem   AE\realbink.dll               Arklite's loader, a version that can run the test build (it runs
rem                                 the release as before whenever arklite-test\active isn't there)
rem Options > More... switches between the test build and the release (from the next start).
setlocal EnableExtensions
set "HERE=%~dp0"

for %%F in (Arklite.dll arklite_loader.dll build.txt) do if not exist "%HERE%%%F" ( echo error: %%F is not next to install-test.bat & goto fail )

set "AE=%~1"
rem As install.bat finds AE (the nearest folder wins), but Arklite must be in it already.
for %%D in ("%HERE%AE" "%HERE%." "%HERE%..\AE" "%HERE%.." "%HERE%..\.." "%ProgramFiles(x86)%\Oni\AE" "%ProgramFiles%\Oni\AE") do (
	if not defined AE if exist "%%~D\Oni.exe" if exist "%%~D\bink_orig.dll" if exist "%%~D\Arklite.dll" set "AE=%%~fD"
)
if not defined AE set /p "AE=Oni's AE folder (for example C:\Program Files (x86)\Oni\AE): "
if not defined AE goto fail
if "%AE:~-1%"=="\" set "AE=%AE:~0,-1%"

if not exist "%AE%\Oni.exe" ( echo error: "%AE%" has no Oni.exe; give the AE folder ^(Oni\AE^) & goto fail )
if not exist "%AE%\Arklite.dll" ( echo error: Arklite isn't installed in "%AE%": install the release first ^(install.bat from a release zip^) & goto fail )
if not exist "%AE%\bink_orig.dll" ( echo error: Arklite isn't installed in "%AE%": install the release first ^(install.bat from a release zip^) & goto fail )
rem The loader is 4-5 KB, Bink about 280 KB: a big realbink.dll means the AE Installer put Bink back.
set "SIZE=0"
for %%F in ("%AE%\realbink.dll") do if exist "%%~F" set "SIZE=%%~zF"
if %SIZE% GEQ 65536 ( echo error: AE's realbink.dll isn't Arklite's loader ^(the AE Installer put Bink back^): run the release's install.bat first & goto fail )

if not exist "%AE%\arklite-test" mkdir "%AE%\arklite-test" || goto denied
copy /y "%HERE%Arklite.dll" "%AE%\arklite-test\Arklite.dll" >nul || goto denied
copy /y "%HERE%build.txt" "%AE%\arklite-test\build.txt" >nul || goto denied

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

copy /y "%HERE%arklite_loader.dll" "%AE%\realbink.dll" >nul || goto denied
type nul > "%AE%\arklite-test\active" || goto denied
set /p DESC=<"%HERE%build.txt"
echo Arklite test build %DESC% installed into "%AE%\arklite-test".
echo Start Oni as usual: the bottom of the screen shows the test build. Options ^> More... switches back
echo to the release; uninstall-test.bat removes the test build and its saves.
goto end

:denied
echo error: couldn't write to "%AE%". Close Oni, or run install-test.bat as administrator ^(right-click, Run as administrator^).
:fail
echo The test build was not installed.
:end
endlocal
if "%~1"=="" pause
exit /b
