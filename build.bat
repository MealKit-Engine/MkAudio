@echo off
rem Builds MkAudio (Release, Ninja) into build\windows\ and runs the tests. Same as MkQA's build.bat.
rem Usage: build.bat            build + tests
rem        build.bat install    also copy to %LOCALAPPDATA%\MealKit\tools\MkAudio (the editor's [Tools] menu finds it there)
setlocal
if defined VCINSTALLDIR goto build
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VS=%%i"
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

:build
cd /d "%~dp0"
if not exist build\windows\CMakeCache.txt cmake -S . -B build\windows -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build build\windows --config Release || exit /b 1
ctest --test-dir build\windows -C Release --output-on-failure || exit /b 1
if /i "%~1"=="install" cmake --install build\windows --config Release --prefix "%LOCALAPPDATA%\MealKit\tools\MkAudio" || exit /b 1
echo [MkAudio] Build OK: %~dp0build\windows\bin\MkAudio.exe
