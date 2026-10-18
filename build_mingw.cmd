@echo off
rem Build with llvm-mingw (the toolchain the player's package ships) from a STAGED COPY of the project
rem (%T6PKG%\src, default ..\t6pkg\src next to this folder: own generated\ and own SDK out\ - the dev tree's generated code and SDK outputs are
rem never touched): codegen from game\Default.xex, the patch tools, then the whole game. It makes the prebuilt parts of
rem the package and is the speed reference against build_x360.cmd (clang, MSVC ABI).
set ROOT=%~dp0
set ROOT=%ROOT:~0,-1%
if not defined T6PKG set T6PKG=%ROOT%\..\t6pkg
set TC=%ROOT%\toolchain\llvm-mingw
set PATH=%TC%\bin;C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%
set SRC=%T6PKG%\src
set OUT=%T6PKG%\build
if not defined PY set PY=python
rem (the MinGW branch of CMakeLists.txt reads generated\default\tekken6_funcs.h at configure time: first codegen with
rem the development build's tool; the build's own codegen step may run again, the patch tools follow it anyway)
if not exist %SRC%\generated\default\tekken6_funcs.h (
  %ROOT%\third_party\rexglue-sdk\out\win-amd64\rexglue.exe -f codegen %SRC%\tekken6_manifest.toml || exit /b 1
)
if not exist %OUT%\build.ninja (
  cmake -S %SRC% -B %OUT% -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_FLAGS=-march=x86-64-v2 -DCMAKE_CXX_FLAGS=-march=x86-64-v2 -DREXSDK_DIR=%SRC%/third_party/rexglue-sdk -DCMAKE_EXPORT_COMPILE_COMMANDS=ON || exit /b 1
)
cmake --build %OUT% -j 12 --target tekken6_codegen -- -k 0 || exit /b 1
for %%t in (pagemap_patch rawstore_patch sndcall_patch faststore_patch) do %PY% -I %SRC%\tools\%%t.py %SRC%\generated\default || exit /b 1
rem (codegen created sources.cmake: configure again so the generated sources are part of the build)
cmake %OUT% || exit /b 1
if "%~1"=="" (cmake --build %OUT% -j 12 -- -k 0) else (cmake --build %OUT% -j 12 --target %*)
