@echo off
rem Removes the Arklite test build (Windows): AE\arklite-test\, with the test build's options, logs
rem and saves. The release, its options and its saves stay as they were; Arklite's loader stays
rem too (it runs the release whenever there's no test build).
rem
rem   uninstall-test.bat ["C:\path\to\Oni\AE"]
setlocal EnableExtensions
set "HERE=%~dp0"

set "AE=%~1"
for %%D in ("%HERE%AE" "%HERE%." "%HERE%..\AE" "%HERE%.." "%HERE%..\.." "%ProgramFiles(x86)%\Oni\AE" "%ProgramFiles%\Oni\AE") do (
	if not defined AE if exist "%%~D\Oni.exe" if exist "%%~D\arklite-test" set "AE=%%~fD"
)
if not defined AE set /p "AE=Oni's AE folder (for example C:\Program Files (x86)\Oni\AE): "
if not defined AE goto fail
if "%AE:~-1%"=="\" set "AE=%AE:~0,-1%"
if not exist "%AE%\Oni.exe" ( echo error: "%AE%" has no Oni.exe; give the AE folder ^(Oni\AE^) & goto fail )
if not exist "%AE%\arklite-test" ( echo No test build in "%AE%": nothing to remove. & goto end )

rem First the switch, so a folder half removed (a file in use) never runs.
if exist "%AE%\arklite-test\active" del /f "%AE%\arklite-test\active" || goto denied
rmdir /s /q "%AE%\arklite-test"
if exist "%AE%\arklite-test" goto denied
echo The test build is removed; Oni runs the release, with its own options and saves.
goto end

:denied
echo error: couldn't remove "%AE%\arklite-test". Close Oni, or run uninstall-test.bat as administrator ^(right-click, Run as administrator^).
:fail
echo The test build was not removed.
:end
endlocal
if "%~1"=="" pause
exit /b
