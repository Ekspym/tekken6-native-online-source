@echo off
rem Tekken 6 Xbox 360 (ReXGlue).
rem Configure + build. Usage: build_x360.cmd [targets...]  (default: all). Only the clang toolchain is shared.
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
set ROOT=%~dp0
set ROOT=%ROOT:~0,-1%
set PATH=%ROOT%\toolchain\llvm\bin;C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%
if not exist %ROOT%\build\build.ninja (
  cmake -S %ROOT% -B %ROOT%\build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_FLAGS=-march=x86-64-v2 -DCMAKE_CXX_FLAGS=-march=x86-64-v2 -DREXSDK_DIR=%ROOT%/third_party/rexglue-sdk || exit /b 1
)
if "%~1"=="" (cmake --build %ROOT%\build -j 12) else (cmake --build %ROOT%\build -j 12 --target %*)
