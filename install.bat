@echo off
setlocal enabledelayedexpansion

set ROOT_DIR=%~dp0
if "%SIMPLE_INSTALL_DIR%"=="" set SIMPLE_INSTALL_DIR=%LocalAppData%\Simple\bin

if not exist "%ROOT_DIR%bin\svm.exe" call "%ROOT_DIR%build.bat"
if errorlevel 1 exit /b %errorlevel%

if not exist "%SIMPLE_INSTALL_DIR%" mkdir "%SIMPLE_INSTALL_DIR%"
copy /y "%ROOT_DIR%bin\svm.exe" "%SIMPLE_INSTALL_DIR%\svm.exe" >nul
copy /y "%ROOT_DIR%bin\simple.exe" "%SIMPLE_INSTALL_DIR%\simple.exe" >nul

echo Installed:
echo   %SIMPLE_INSTALL_DIR%\svm.exe
echo   %SIMPLE_INSTALL_DIR%\simple.exe
echo.
echo Add this directory to PATH if it is not already there:
echo   %SIMPLE_INSTALL_DIR%
echo.
echo Current-user PATH command:
echo   setx PATH "%%PATH%%;%SIMPLE_INSTALL_DIR%"
