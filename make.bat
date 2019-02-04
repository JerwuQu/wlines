@echo off

set TCC=tcc\tcc.exe
set PRJ=wlines
set SRC=src/wlines.c src/vec/vec.c
set LIB=-lgdi32 -luser32
set OPT=all release debug test clean

goto :main

:all
call :release
call :debug
exit /b

:test
call :debug
if %ERRORLEVEL% NEQ 0 (exit /b)
type test_input.txt | %PRJ%-debug.exe
exit /b

:release
%TCC% -Wall -o %PRJ%.exe %SRC% %LIB%
exit /b

:debug
%TCC% -Wall -g -o %PRJ%-debug.exe %SRC% %LIB%
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
