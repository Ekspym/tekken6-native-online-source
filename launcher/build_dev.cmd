@echo off
rem Developer's own launcher (statistics tab, -DT6_DEV=ON) -> launcher\build_dev\"Tekken6 Native Online Dev.exe".
rem NEVER packaged: tools\make_package.py takes launcher\build only and refuses an exe with "Dev" in its name.
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
set PATH=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%
set L=%~dp0
set L=%L:~0,-1%
if not exist %L%\build_dev\build.ninja (
  cmake -S %L% -B %L%\build_dev -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=cl -DCMAKE_PREFIX_PATH=C:/Qt/6.12.0/msvc2022_64 -DT6_DEV=ON || exit /b 1
)
cmake --build %L%\build_dev -j 12 || exit /b 1
rem the Qt runtime of the normal build next to it
copy /y %L%\build\*.dll %L%\build_dev\ >nul
for %%d in (generic iconengines imageformats networkinformation platforms styles tls) do if exist %L%\build\%%d xcopy /e /i /y /q %L%\build\%%d %L%\build_dev\%%d >nul
