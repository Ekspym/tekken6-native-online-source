# Runs the ReXGlue Tekken 6 build for -Seconds with the settings of the author's test launcher (tools\portable_launcher.cpp);
# window on screen 1 in the background (window_guard.ps1 of the PS3 project, only reused as a tool). Log: runs\<Name>\tekken6.log
param([string]$Name = 'r1', [int]$Seconds = 120, [switch]$LowOverhead, [string[]]$Extra = @())
$root = $PSScriptRoot
$run = "$root\runs\$Name"
Remove-Item -Recurse -Force $run -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $run, "$root\user" | Out-Null

$env:TEKKEN6_YIELD_STARTUP_SPIN = '1'; $env:TEKKEN6_FRAME_PROBE_ATTEMPTS = '0'
$env:REX_GPU_VSYNC = '0'; $env:REX_FULLSCREEN = '0'; $env:REX_LOG_LEVEL = 'info'
if (-not $LowOverhead) {
    $env:TEKKEN6_TRACE_OUTPUT = '1'; $env:TEKKEN6_TRACE_RENDERING = '1'
    $env:TEKKEN6_TRACE_RENDERING_DETAIL = '1'; $env:TEKKEN6_TRACE_RENDERING_DETAIL_INTERVAL = '60'
}
$args2 = @("--game_data_root=$root\game", "--user_data_root=$root\user", "--cache_root=$run\cache",
           "--log_file=$run\tekken6.log", '--fullscreen=false', '--async_shader_compilation=true', '--render_target_path_d3d12=rov') + $Extra
$p = Start-Process -FilePath "$root\build\tekken6.exe" -ArgumentList $args2 -WorkingDirectory "$root\build" -PassThru
Start-Process powershell -WindowStyle Hidden -ArgumentList '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
    (Join-Path $root "tools\window_guard.ps1"), '-ProcessId', $p.Id, '-X', 0, '-Y', 0 | Out-Null
$exited = $p.WaitForExit($Seconds * 1000)
if (-not $exited) { Stop-Process -Id $p.Id -Force }
"run ${Name}: " + $(if ($exited) { "exited by itself, code $($p.ExitCode)" } else { "stopped after $Seconds s" })
