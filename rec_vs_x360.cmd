@echo off
rem Tekken 6 (Xbox 360) - record a VERSUS match for the netplay tests -> runs\rec_vs.bin (both players' inputs)
rem Player 1 = joystick (MMJoystick, build\mmjoystick.ini), player 2 = keyboard, mapped like the user's RPCS3 player 2:
rem   arrows = d-pad, WASD = left stick, Home/End/Delete/PageDown = right stick, Numpad1 = Cross/A, Numpad2 = Circle/B,
rem   Numpad4 = Square/X, Numpad5 = Triangle/Y, Q/E = L1/R1, R/T = L2/R2, F/G = L3/R3, Return = Start, Space = Select/Back,
rem   Backspace = PS/Guide. The keyboard works while the game window has focus.
set ROOT=%~dp0
if "%ROOT:~-1%"=="\" set ROOT=%ROOT:~0,-1%
set REX_GPU_VSYNC=1
set REX_FULLSCREEN=0
set REX_LOG_LEVEL=info
set T6_FIX_SEED=1
set T6_VIRTUAL_TB=1
set T6_REC=%ROOT%\runs\rec_vs.bin
set P2_KEYBOARD=--mnk_mode=true --mnk_user=1 --mnk_mouse=false --keybind_a=Numpad1 --keybind_b=Numpad2 --keybind_x=Numpad4 --keybind_y=Numpad5 --keybind_left_shoulder=Q --keybind_right_shoulder=E --keybind_left_trigger=R --keybind_right_trigger=T --keybind_lstick_press=F --keybind_rstick_press=G --keybind_lstick_up=W --keybind_lstick_down=S --keybind_lstick_left=A --keybind_lstick_right=D --keybind_rstick_up=Home --keybind_rstick_down=End --keybind_rstick_left=Delete --keybind_rstick_right=PageDown --keybind_dpad_up=Up --keybind_dpad_down=Down --keybind_dpad_left=Left --keybind_dpad_right=Right --keybind_start=Return --keybind_back=Space --keybind_guide=Backspace
if not exist "%ROOT%\runs\play" mkdir "%ROOT%\runs\play"
if not exist "%ROOT%\user" mkdir "%ROOT%\user"
start "" /D "%ROOT%\build" "%ROOT%\build\tekken6.exe" --game_data_root="%ROOT%\game" --user_data_root="%ROOT%\user" --cache_root="%ROOT%\runs\play\cache" --log_file="%ROOT%\runs\play\rec_vs.log" --fullscreen=false --async_shader_compilation=true --render_target_path_d3d12=rov --input_backend=mmjoystick %P2_KEYBOARD%
