@echo off
rem WIN.BAT - jak WIN.COM w Win16: WIN [opcje] [program]
:again
A:\LOADER.EXE %1 %2 %3
rem kod 42 = ExitWindows(EW_RESTARTWINDOWS): uruchom od nowa
if errorlevel 42 goto again
