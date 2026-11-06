@echo off
rem SnipShot Windows build script (MSVC 2022 + Qt 6.8.3)
setlocal
call "E:\Tools\IDE\VS\App\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set QTDIR=E:\Tools\Qt\6.8.3\msvc2022_64
cmake -S "%~dp0." -B "%~dp0build" -G Ninja -DCMAKE_PREFIX_PATH=%QTDIR% -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build "%~dp0build" || exit /b 1
echo.
echo Build OK: %~dp0build\snipshot.exe
