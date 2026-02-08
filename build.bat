@echo off
setlocal enabledelayedexpansion

set ROOT=%~dp0
set BUILD=%ROOT%build
set CONFIG=Release
if not "%~1"=="" set CONFIG=%~1

cmake -S "%ROOT%" -B "%BUILD%"
if errorlevel 1 exit /b %errorlevel%
cmake --build "%BUILD%" --config %CONFIG%
if errorlevel 1 exit /b %errorlevel%

set TEST_EXE=%BUILD%\bin\simplevm_tests.exe
if exist "%TEST_EXE%" goto run

set TEST_EXE=%BUILD%\bin\simplevm_tests_all.exe
if exist "%TEST_EXE%" goto run

set TEST_EXE=%BUILD%\bin\%CONFIG%\simplevm_tests.exe
if exist "%TEST_EXE%" goto run

set TEST_EXE=%BUILD%\bin\%CONFIG%\simplevm_tests_all.exe
if exist "%TEST_EXE%" goto run

set TEST_EXE=%BUILD%\%CONFIG%\simplevm_tests.exe
if exist "%TEST_EXE%" goto run

set TEST_EXE=%BUILD%\%CONFIG%\simplevm_tests_all.exe
if exist "%TEST_EXE%" goto run

echo simplevm_tests.exe not found in build output.
exit /b 1

:run
"%TEST_EXE%"
endlocal
