@echo off
rem Removes the Arklite test build (Windows): AE\arklite-test\, with the test build's options, logs
rem and saves. The release, its options and its saves stay as they were; Arklite's loader stays too
rem (it runs the release whenever there's no test build).
rem
rem   uninstall-test.bat ["C:\path\to\Oni\AE"]
rem
rem Without a path it looks where install-test.bat does (this file's folder and the ones around it, the
rem Oni that Windows has registered, Oni\AE, Games\Oni\AE and Program Files\Oni\AE on every drive) for an
rem AE folder with a test build in it. More than one: it asks which.
setlocal EnableExtensions
set "HERE=%~dp0"
if "%HERE:~-1%"=="\" set "HERE=%HERE:~0,-1%"

set "AE=%~1"
if defined AE goto have_ae
set "N=0"
for %%D in ("%HERE%" "%HERE%\AE" "%HERE%\.." "%HERE%\..\AE" "%HERE%\..\.." "%HERE%\..\..\AE") do call :consider "%%~fD"
if %N% GTR 0 goto choose
for %%K in ("HKLM\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Oni" "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Oni" "HKCU\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Oni") do call :registered %%K
for %%L in (C D E F G H I J K L M N O P Q R S T U V W X Y Z) do if exist "%%L:\" call :drive %%L
:choose
if %N%==0 ( echo No test build found: nothing to remove. & goto end )
if %N%==1 ( set "AE=%C1%" & goto have_ae )
echo Test builds found in:
for /l %%I in (1,1,%N%) do call echo   %%I. %%C%%I%%
set "PICK="
set /p "PICK=Which one (1-%N%)? "
if not defined PICK goto fail
set "AE="
for /l %%I in (1,1,%N%) do if "%PICK%"=="%%I" call set "AE=%%C%%I%%"
if not defined AE ( echo error: "%PICK%" isn't one of them & goto fail )
:have_ae
if "%AE:~-1%"=="\" set "AE=%AE:~0,-1%"
for %%A in ("%AE%") do set "AE=%%~fA"
if not exist "%AE%\Oni.exe" ( echo error: "%AE%" has no Oni.exe; give the AE folder ^(Oni\AE^) & goto fail )
if not exist "%AE%\arklite-test" ( echo No test build in "%AE%": nothing to remove. & goto end )

rem First the switch, so a folder half removed (a file in use) never runs.
if exist "%AE%\arklite-test\active" del /f "%AE%\arklite-test\active" || goto denied
rmdir /s /q "%AE%\arklite-test"
if exist "%AE%\arklite-test" goto denied
echo The test build is removed from "%AE%"; Oni runs the release, with its own options and saves.
goto end

:denied
echo error: couldn't remove "%AE%\arklite-test". Close Oni, or run uninstall-test.bat as administrator ^(right-click, Run as administrator^).
:fail
echo The test build was not removed.
:end
endlocal
if "%~1"=="" pause
exit /b

rem ---- subroutines ----

rem An AE folder with a test build (Oni.exe and arklite-test\) goes on the list once
:consider
if not exist "%~1\Oni.exe" exit /b
if not exist "%~1\arklite-test\" exit /b
for /l %%I in (1,1,%N%) do call :same "%~f1" %%I && exit /b
set /a N+=1
set "C%N%=%~f1"
exit /b

:same
call set "X=%%C%2%%"
if /i "%X%"=="%~1" exit /b 0
exit /b 1

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
