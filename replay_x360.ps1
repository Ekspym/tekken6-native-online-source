# Replays runs\<Rec>.bin from boot (T6_PLAY) with per-step state hashes (T6_HASH) -> runs\<Name>\hash.txt.
# Window on screen 1 in the background (window_guard.ps1 of the PS3 project).
param([string]$Name = 'rp1', [string]$Rec = 'rec1', [int]$Seconds = 170, [string]$Ranges = '')
$root = $PSScriptRoot
$run = "$root\runs\$Name"
Remove-Item -Recurse -Force $run -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $run | Out-Null
$env:T6_FIX_SEED = "1"; $env:T6_PLAY = "$root\runs\$Rec.bin"; $env:T6_HASH = "$run\hash.txt"; $env:T6_HASH_RANGES = $Ranges
Remove-Item Env:T6_REC -ErrorAction SilentlyContinue
$env:REX_GPU_VSYNC = '1'; $env:REX_FULLSCREEN = '0'; $env:REX_LOG_LEVEL = 'info'
$a = @("--game_data_root=$root\game", "--user_data_root=$root\user", "--cache_root=$run\cache", "--log_file=$run\tekken6.log",
       '--fullscreen=false', '--async_shader_compilation=true', '--render_target_path_d3d12=rov', '--input_backend=mmjoystick')
$p = Start-Process -FilePath "$root\build\tekken6.exe" -ArgumentList $a -WorkingDirectory "$root\build" -PassThru
Start-Process powershell -WindowStyle Hidden -ArgumentList '-NoProfile','-ExecutionPolicy','Bypass','-File',
    (Join-Path $root "tools\window_guard.ps1"),'-ProcessId',$p.Id,'-X',0,'-Y',0,'-Title','tekken' | Out-Null
if (-not $p.WaitForExit($Seconds * 1000)) { Stop-Process -Id $p.Id -Force }
"replay ${Name}: $((Get-Content "$run\hash.txt").Count) steps hashed"
