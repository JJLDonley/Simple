@echo off
setlocal enabledelayedexpansion

for %%I in ("%~dp0..\..") do set "ROOT_DIR=%%~fI\"
if "%SIMPLE_INSTALL_DIR%"=="" set SIMPLE_INSTALL_DIR=%LocalAppData%\Simple\bin

call "%ROOT_DIR%scripts\build\windows.bat"
if errorlevel 1 exit /b %errorlevel%

if not exist "%SIMPLE_INSTALL_DIR%" mkdir "%SIMPLE_INSTALL_DIR%"
copy /y "%ROOT_DIR%bin\svm.exe" "%SIMPLE_INSTALL_DIR%\svm.exe" >nul
copy /y "%ROOT_DIR%bin\simple.exe" "%SIMPLE_INSTALL_DIR%\simple.exe" >nul

powershell -NoProfile -ExecutionPolicy Bypass -Command "$dir=$env:SIMPLE_INSTALL_DIR; $path=[Environment]::GetEnvironmentVariable('Path','User'); if ($null -eq $path) { $path='' }; $parts=$path -split ';' | Where-Object { $_ -ne '' }; if ($parts -notcontains $dir) { [Environment]::SetEnvironmentVariable('Path', (($parts + $dir) -join ';'), 'User'); Write-Host ('PATH: added to current user PATH; open a new terminal or run: set PATH=' + $dir + ';%%PATH%%') } else { Write-Host 'PATH: already on current user PATH' }"
if errorlevel 1 exit /b %errorlevel%

echo Installed:
echo   %SIMPLE_INSTALL_DIR%\svm.exe
echo   %SIMPLE_INSTALL_DIR%\simple.exe
