@echo off
rem Tekken 6 (Xbox 360) - play AND record both players' inputs to runs\rec1.bin (determinism test, online step 2)
set ROOT=%~dp0
if "%ROOT:~-1%"=="\" set ROOT=%ROOT:~0,-1%
set REX_GPU_VSYNC=1
set REX_FULLSCREEN=0
set REX_LOG_LEVEL=info
set T6_FIX_SEED=1
set T6_REC=%ROOT%\runs\rec1.bin
if not exist "%ROOT%\runs\play" mkdir "%ROOT%\runs\play"
if not exist "%ROOT%\user" mkdir "%ROOT%\user"
start "" /D "%ROOT%\build" "%ROOT%\build\tekken6.exe" --game_data_root="%ROOT%\game" --user_data_root="%ROOT%\user" --cache_root="%ROOT%\runs\play\cache" --log_file="%ROOT%\runs\play\rec1.log" --fullscreen=false --async_shader_compilation=true --render_target_path_d3d12=rov --input_backend=mmjoystick
