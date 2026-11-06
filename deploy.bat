@echo off
rem 打包绿色版：build\snipshot.exe + Qt 运行库 + ffmpeg
setlocal
set QTDIR=E:\Tools\Qt\6.8.3\msvc2022_64
set STAGE=%~dp0dist\SnipShot

if not exist "%~dp0build\snipshot.exe" (
    call "%~dp0build.bat" || exit /b 1
)

if exist "%STAGE%" rmdir /s /q "%STAGE%"
mkdir "%STAGE%" || exit /b 1

copy /y "%~dp0build\snipshot.exe" "%STAGE%\" >nul || exit /b 1
"%QTDIR%\bin\windeployqt.exe" --release --no-translations "%STAGE%\snipshot.exe" || exit /b 1

if exist "E:\Tools\ffmpeg\ffmpeg.exe" copy /y "E:\Tools\ffmpeg\ffmpeg.exe" "%STAGE%\" >nul
copy /y "%~dp0README.md" "%STAGE%\" >nul

powershell -NoProfile -Command "Compress-Archive -Path '%STAGE%\*' -DestinationPath '%~dp0dist\SnipShot-win64.zip' -Force"
echo.
echo OK: %~dp0dist\SnipShot-win64.zip
