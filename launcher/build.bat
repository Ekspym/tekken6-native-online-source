@echo off
rem Builds "Tekken6 Native Online.exe" (MSVC + Qt 6.12, like the PS3 launcher) and copies the Qt runtime next to it.
rem Output: launcher\build\Tekken6 Native Online.exe
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set "PATH=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
cd /d "%~dp0"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.12.0/msvc2022_64 || exit /b 1
cmake --build build || exit /b 1
"C:\Qt\6.12.0\msvc2022_64\bin\windeployqt.exe" --no-translations --no-system-d3d-compiler --no-opengl-sw "build\Tekken6 Native Online.exe" >nul
