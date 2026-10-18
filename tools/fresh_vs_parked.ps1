# Fresh game against a game that has already played a match and parked again (no restart): instance A (player 1) and
# B (player 2) play a short match, A parks itself (T6_NET_AUTOPARK) and opens its next session, B is closed and a NEW
# player 2 instance boots and joins. The second match is compared (fight player objects + third RNG per frame).
#   .\tools\fresh_vs_parked.ps1 [-Name fp1] [-LagMs 25]
param([string]$Name = 'fp1', [int]$LagMs = 25, [int]$Char1 = 3, [int]$Char2 = 33, [int]$Stage = 5, [int]$Rounds = 3, [int]$RoundTime = 20)
. "$PSScriptRoot\t6_env.ps1"
function StartInst([int]$p, [string]$run, [int]$autopark) {
    Remove-Item -Recurse -Force $run -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force $run | Out-Null
    $env:T6_FIX_SEED = '1'; $env:T6_PLAY = "$root\tools\menu_vs.bin"; $env:T6_VIRTUAL_TB = '1'; $env:T6_SOUND_SEPARATE = '1'
    $env:T6_SNAP_EXCLUDE_FILE = "$root\tools\netplay_exclude.txt"
    $env:T6_CHAR_P1 = "$Char1"; $env:T6_CHAR_P2 = "$Char2"; $env:T6_STAGE = "$Stage"
    $env:T6_ROUNDS = "$Rounds"; $env:T6_ROUND_TIME = "$RoundTime"
    $env:T6_NET = '1'; $env:T6_NET_PLAYER = "$p"; $env:T6_NET_DELAY = '1'; $env:T6_NET_INPUT = 'random'
    $env:T6_NET_DUMP = '1'; $env:T6_RAND_LOG = '1'; $env:T6_NET_DUMP_DIR = $run
    $env:T6_NET_LOCAL_PORT = "$(7199 + $p)"; $env:T6_NET_REMOTE = "127.0.0.1:$(7202 - $p)"
    if ($autopark -gt 0) { $env:T6_NET_AUTOPARK = "$autopark" } else { Remove-Item Env:T6_NET_AUTOPARK -ErrorAction SilentlyContinue }
    [Environment]::SetEnvironmentVariable('ggpo.network.delay', "$LagMs", 'Process')
    foreach ($v in 'T6_SNAPTEST', 'T6_SNAP_FROM_MAP', 'T6_PAGEMAP', 'T6_PAGEMAP_STEPS', 'T6_REC', 'T6_HASH', 'T6_TASKDUMP', 'T6_LOG_MODE', 'T6_NET_HOLD') {
        Remove-Item "Env:$v" -ErrorAction SilentlyContinue
    }
    $env:REX_GPU_VSYNC = '1'; $env:REX_FULLSCREEN = '0'; $env:REX_LOG_LEVEL = 'info'
    $user = if ($p -eq 1) { "$root\user" } else { "$root\user2" }
    $a = @("--game_data_root=$root\game", "--user_data_root=$user", "--cache_root=$run\cache", "--log_file=$run\tekken6.log",
           '--fullscreen=false', '--async_shader_compilation=true', '--render_target_path_d3d12=rov', '--input_backend=mmjoystick')
    $proc = Start-Process -FilePath "$root\build\tekken6.exe" -ArgumentList $a -WorkingDirectory "$root\build" -PassThru
    Start-Process powershell -WindowStyle Hidden -ArgumentList '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
        "$root\tools\window_guard.ps1", '-ProcessId', $proc.Id, '-X', (($p - 1) * 660), '-Y', 0, '-Title', 'tekken' | Out-Null
    return $proc
}
function WaitLog([string]$log, [string]$pattern, [int]$count, [int]$seconds) {
    $t = Get-Date
    while (((Get-Date) - $t).TotalSeconds -lt $seconds) {
        $n = @(Select-String -Path $log -Pattern $pattern -ErrorAction SilentlyContinue).Count
        if ($n -ge $count) { return $true }
        Start-Sleep -Seconds 1
    }
    return $false
}
$ra = "$root\runs\${Name}_p1"; $rb0 = "$root\runs\${Name}_p2_first"; $rb = "$root\runs\${Name}_p2"
$a = StartInst 1 $ra 120
"A at its first session: $(WaitLog "$ra\tekken6.log" 'T6 net: session at' 1 120)"
$b0 = StartInst 2 $rb0 0
"A parked again after match 1: $(WaitLog "$ra\tekken6.log" 'T6 park: parked' 1 (120 + 45 * $Rounds))"
try { Stop-Process -Id $b0.Id -Force -ErrorAction Stop } catch {}
Start-Sleep -Seconds 2
$b = StartInst 2 $rb 0
"fresh B at its session: $(WaitLog "$rb\tekken6.log" 'T6 net: session at' 1 120)"
"A through match 2 (its second park): $(WaitLog "$ra\tekken6.log" 'T6 park: session closed' 2 (90 + 45 * $Rounds))"
foreach ($p in $a, $b) { try { Stop-Process -Id $p.Id -Force -ErrorAction Stop } catch {} }
Start-Sleep -Seconds 1
foreach ($r in $ra, $rb) {
    "== $r"
    Select-String -Path "$r\tekken6.log" -Pattern 'T6 net: session at|T6 park: (session|parked)|T6 crash|disconnected|peer ready' |
        ForEach-Object { '  ' + $_.Line.Substring([Math]::Min(60, $_.Line.Length)) }
}
& $py "$root\tools\state_diff.py" "$ra\fight_state.csv" "$rb\fight_state.csv" | Select-String -Pattern 'common|players|rng3|first' | ForEach-Object { '  ' + $_.Line.Trim() }
