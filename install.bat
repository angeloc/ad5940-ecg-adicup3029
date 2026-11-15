@echo off
rem Windows 10/11: installs GNU make and Python with winget, the Python
rem package of the viewer, and lets the Makefile download the toolchain, the
rem device pack, the CMSIS headers and the AD5940 library.
setlocal
cd /d "%~dp0"

where winget >nul 2>nul
if errorlevel 1 (
  echo winget not found: install "App Installer" from the Microsoft Store, then run this file again.
  exit /b 1
)

where make >nul 2>nul
if errorlevel 1 (
  echo Installing GNU make...
  winget install --id ezwinports.make -e --accept-package-agreements --accept-source-agreements
)

where py >nul 2>nul
if errorlevel 1 (
  echo Installing Python 3.14...
  winget install --id Python.Python.3.14 -e --accept-package-agreements --accept-source-agreements
)

rem Programs installed by winget are on the PATH of new terminals only
set "PATH=%LOCALAPPDATA%\Microsoft\WinGet\Links;%LOCALAPPDATA%\Programs\Python\Launcher;%PATH%"

where make >nul 2>nul
if errorlevel 1 goto newterminal
where py >nul 2>nul
if errorlevel 1 goto newterminal

echo Installing the Python package of the viewer...
py -3 -m pip install --user -r requirements.txt
if errorlevel 1 exit /b 1

echo Downloading the toolchain and the vendor code (about 400 MB)...
make deps
if errorlevel 1 exit /b 1

echo.
echo Done. Open a new terminal in this folder, then:
echo   make                      build the ECG firmware
echo   make flash DAPLINK=E:     copy it to the DAPLINK drive, then press URST
echo   make view                 start the live viewer
exit /b 0

:newterminal
echo The tools are installed but not on the PATH of this terminal yet.
echo Open a new terminal and run install.bat again.
exit /b 1
