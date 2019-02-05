@echo off

set PRJ=wlines
set SRC=src/wlines.c src/vec/vec.c
set LIB=-lgdi32 -luser32 -lshlwapi
set OPT=all release debug test clean

goto :main

:all
call :release
call :debug
exit /b

:test
call :debug
if %ERRORLEVEL% NEQ 0 (exit /b)
type test_input.txt | %PRJ%-debug.exe -i -l 7 -nb 222222 -nf 00ccff -sb 00ccff -sf 000000 -fn "Open Sans" -fs 32
echo Error code: %ERRORLEVEL%
exit /b

:release
tcc -Wall -o %PRJ%.exe %SRC% %LIB%
exit /b

:debug
tcc -Wall -g -o %PRJ%-debug.exe %SRC% %LIB%
exit /b

:clean
del %PRJ%.exe %PRJ%-debug.exe
exit /b

:main
if "%1" == "" goto :%OPT%
(for %%a in (%OPT%) do (
    if "%1" == "%%a" goto :%%a
))

echo %OPT%
