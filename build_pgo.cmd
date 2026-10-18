@echo off
rem Profile-guided build of the game (PS3 did the same with its native DLL).
rem   build_pgo.cmd gen   - instrumented build in build_pgo\ (slow exe; writes runs\pgo\*.profraw at session frame 4000)
rem   build_pgo.cmd use   - merges runs\pgo\*.profraw into runs\pgo\t6.profdata and rebuilds build\ with it
rem   build_pgo.cmd off   - build\ back to the plain flags
rem Between gen and use: net_x360.ps1 -Build build_pgo ... (a full match with rollbacks, both instances).
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
set ROOT=%~dp0
set ROOT=%ROOT:~0,-1%
set PATH=%ROOT%\toolchain\llvm\bin;C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%
set BASE=-march=x86-64-v2
if "%~1"=="gen" (
  cmake -S %ROOT% -B %ROOT%\build_pgo -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ "-DCMAKE_C_FLAGS=%BASE%" "-DCMAKE_CXX_FLAGS=%BASE%" "-DCMAKE_EXE_LINKER_FLAGS=" "-DCMAKE_SHARED_LINKER_FLAGS=" -DT6_PGO=gen -DREXSDK_DIR=%ROOT%/third_party/rexglue-sdk || exit /b 1
  cmake --build %ROOT%\build_pgo -j 12 || exit /b 1
  exit /b 0
)
if "%~1"=="use" (
  llvm-profdata merge -o %ROOT%\runs\pgo\t6.profdata %ROOT%\runs\pgo\*.profraw || exit /b 1
  cmake -S %ROOT% -B %ROOT%\build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ "-DCMAKE_C_FLAGS=%BASE%" "-DCMAKE_CXX_FLAGS=%BASE%" -DT6_PGO=use -DT6_PGO_DATA=%ROOT:\=/%/runs/pgo/t6.profdata -DREXSDK_DIR=%ROOT%/third_party/rexglue-sdk || exit /b 1
  cmake --build %ROOT%\build -j 12 || exit /b 1
  exit /b 0
)
if "%~1"=="off" (
  cmake -S %ROOT% -B %ROOT%\build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ "-DCMAKE_C_FLAGS=%BASE%" "-DCMAKE_CXX_FLAGS=%BASE%" -DT6_PGO= -DREXSDK_DIR=%ROOT%/third_party/rexglue-sdk || exit /b 1
  cmake --build %ROOT%\build -j 12 || exit /b 1
  exit /b 0
)
echo usage: build_pgo.cmd gen ^| use ^| off
exit /b 1
