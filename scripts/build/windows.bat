@echo off
setlocal enabledelayedexpansion

for %%I in ("%~dp0..\..") do set "ROOT_DIR=%%~fI\"
set BUILD_DIR=%ROOT_DIR%build
if "%CONFIG%"=="" set CONFIG=Release
if "%JOBS%"=="" set JOBS=2

cmake -S "%ROOT_DIR%" -B "%BUILD_DIR%" -DCMAKE_BUILD_TYPE=%CONFIG%
if errorlevel 1 exit /b %errorlevel%

cmake --build "%BUILD_DIR%" --target simplevm_runtime_static --config %CONFIG% --parallel %JOBS%
if errorlevel 1 exit /b %errorlevel%
cmake --build "%BUILD_DIR%" --target simplevm_runtime_shared --config %CONFIG% --parallel %JOBS%
if errorlevel 1 exit /b %errorlevel%
cmake --build "%BUILD_DIR%" --target simplevm --config %CONFIG% --parallel %JOBS%
if errorlevel 1 exit /b %errorlevel%
cmake --build "%BUILD_DIR%" --target simple_stub --config %CONFIG% --parallel %JOBS%
if errorlevel 1 exit /b %errorlevel%

if exist "%ROOT_DIR%bin" rmdir /s /q "%ROOT_DIR%bin"
mkdir "%ROOT_DIR%bin"
copy /y "%BUILD_DIR%\bin\svm.exe" "%ROOT_DIR%bin\svm.exe" >nul
copy /y "%BUILD_DIR%\bin\simple.exe" "%ROOT_DIR%bin\simple.exe" >nul

echo Built:
echo   %ROOT_DIR%bin\svm.exe
echo   %ROOT_DIR%bin\simple.exe
