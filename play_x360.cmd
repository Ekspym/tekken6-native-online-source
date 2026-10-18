@echo off
rem Tekken 6 (Xbox 360, ReXGlue) - play. Controller via WinMM (MMJoystick like RPCS3); mapping: build\mmjoystick.ini
rem (created with defaults on first start). For an XInput pad use --input_backend=xinput, for SDL --input_backend=sdl.
set ROOT=%~dp0
if "%ROOT:~-1%"=="\" set ROOT=%ROOT:~0,-1%
set REX_GPU_VSYNC=1
set REX_FULLSCREEN=0
set REX_LOG_LEVEL=info
if not exist "%ROOT%\runs\play" mkdir "%ROOT%\runs\play"
if not exist "%ROOT%\user" mkdir "%ROOT%\user"
start "" /D "%ROOT%\build" "%ROOT%\build\tekken6.exe" --game_data_root="%ROOT%\game" --user_data_root="%ROOT%\user" --cache_root="%ROOT%\runs\play\cache" --log_file="%ROOT%\runs\play\tekken6.log" --fullscreen=false --async_shader_compilation=true --render_target_path_d3d12=rov --input_backend=mmjoystick --mnk_mode=true --mnk_user=1 --mnk_mouse=false --keybind_a=Numpad1 --keybind_b=Numpad2 --keybind_x=Numpad4 --keybind_y=Numpad5 --keybind_left_shoulder=Q --keybind_right_shoulder=E --keybind_left_trigger=R --keybind_right_trigger=T --keybind_lstick_press=F --keybind_rstick_press=G --keybind_lstick_up=W --keybind_lstick_down=S --keybind_lstick_left=A --keybind_lstick_right=D --keybind_rstick_up=Home --keybind_rstick_down=End --keybind_rstick_left=Delete --keybind_rstick_right=PageDown --keybind_dpad_up=Up --keybind_dpad_down=Down --keybind_dpad_left=Left --keybind_dpad_right=Right --keybind_start=Return --keybind_back=Space --keybind_guide=Backspace
