@echo off
setlocal
set "MSYS2_ROOT=D:\msys64"
for %%I in ("%~dp0.") do set "PROJECT_ROOT=%%~fI"
set "BUILD_ROOT=%PROJECT_ROOT%\build-local-mingw-rc"
if not exist "%MSYS2_ROOT%\mingw64\bin\cmake.exe" (
    echo MSYS2 MINGW64 CMake was not found at %MSYS2_ROOT%.
    exit /b 1
)
powershell -NoProfile -Command "Get-Process -Name RemoteControl -ErrorAction SilentlyContinue | Stop-Process -Force"
set "PATH=%MSYS2_ROOT%\mingw64\bin;%MSYS2_ROOT%\usr\bin;%PATH%"
"%MSYS2_ROOT%\mingw64\bin\cmake.exe" -S "%PROJECT_ROOT%" -B "%BUILD_ROOT%" -G Ninja -DCMAKE_BUILD_TYPE=Release -DGRCLIB_SOURCE_DIR="%PROJECT_ROOT%\..\grclib"
if errorlevel 1 exit /b %errorlevel%
"%MSYS2_ROOT%\mingw64\bin\cmake.exe" --build "%BUILD_ROOT%" --parallel
