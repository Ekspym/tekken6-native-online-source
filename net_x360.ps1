# Local two-instance netplay test (online plan step 4, PS3 tools\net_test.ps1): both instances replay runs\<Rec>.bin
# up to the fight (session start = fight loaded), then a GGPO session with test inputs (T6_NET_INPUT=random) or the
# controller. Logs: runs\<Name>_p1\tekken6.log, runs\<Name>_p2\tekken6.log. Windows on screen 1 in the background.
param([string]$Name = 'n1', [string]$Rec = 'rec_vs', [int]$Seconds = 150, [int]$LagMs = 0, [int]$Delay = 1,
      [string]$InputMode = 'random', [int]$Stagger = 0, [int]$Char1 = -1, [int]$Char2 = -1, [int]$Stage = -1, [int]$First = 1, [int]$Rounds = 3, [string]$Build = 'build')
$root = $PSScriptRoot
if (-not (Test-Path "$root\user2")) { Copy-Item -Recurse "$root\user" "$root\user2" }
$procs = @()
# -First 2: the player 2 instance starts first and waits for player 1 (which instance waits vs. which player it is)
$order = if ($First -eq 2) { 2, 1 } else { 1, 2 }
foreach ($p in $order) {
    $run = "$root\runs\${Name}_p$p"
    Remove-Item -Recurse -Force $run -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force $run | Out-Null
    # -Rec menu = the built-in menu path to the fight loading (tools\menu_vs.bin, like the launcher)
    $playFile = if ($Rec -eq 'menu') { "$root\tools\menu_vs.bin" } else { "$root\runs\$Rec.bin" }
    $env:T6_FIX_SEED = '1'; $env:T6_PLAY = $playFile; $env:T6_VIRTUAL_TB = '1'; $env:T6_SOUND_SEPARATE = '1'
    $env:T6_SNAP_EXCLUDE_FILE = "$root\tools\netplay_exclude.txt"
    foreach ($kv in @(@('T6_CHAR_P1', $Char1), @('T6_CHAR_P2', $Char2), @('T6_STAGE', $Stage))) {
        if ($kv[1] -ge 0) { Set-Item "Env:$($kv[0])" "$($kv[1])" } else { Remove-Item "Env:$($kv[0])" -ErrorAction SilentlyContinue }
    }
    if (-not $env:T6_ROUNDS_KEEP) { $env:T6_ROUNDS = "$Rounds" }  # first to 3 like online; a caller that sets T6_ROUNDS itself sets T6_ROUNDS_KEEP=1
    $env:T6_NET = '1'; $env:T6_NET_PLAYER = "$p"; $env:T6_NET_DELAY = "$Delay"; $env:T6_NET_INPUT = $InputMode
    $env:T6_NET_DUMP = "1"; $env:T6_RAND_LOG = "1"; $env:T6_NET_DUMP_DIR = $run; $env:T6_NET_LOCAL_PORT = "$(7199 + $p)"; $env:T6_NET_REMOTE = "127.0.0.1:$(7202 - $p)"
    [Environment]::SetEnvironmentVariable('ggpo.network.delay', "$LagMs", 'Process')
    foreach ($v in 'T6_SNAPTEST', 'T6_SNAP_FROM_MAP', 'T6_PAGEMAP', 'T6_PAGEMAP_STEPS', 'T6_REC', 'T6_HASH', 'T6_TASKDUMP', 'T6_LOG_MODE') {
        Remove-Item "Env:$v" -ErrorAction SilentlyContinue
    }
    $env:REX_GPU_VSYNC = '1'; $env:REX_FULLSCREEN = '0'; $env:REX_LOG_LEVEL = 'info'
    $user = if ($p -eq 1) { "$root\user" } else { "$root\user2" }
    $a = @("--game_data_root=$root\game", "--user_data_root=$user", "--cache_root=$run\cache", "--log_file=$run\tekken6.log",
           '--fullscreen=false', '--async_shader_compilation=true', '--render_target_path_d3d12=rov', '--input_backend=mmjoystick')
    $proc = Start-Process -FilePath "$root\$Build\tekken6.exe" -ArgumentList $a -WorkingDirectory "$root\$Build" -PassThru
    Start-Process powershell -WindowStyle Hidden -ArgumentList '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
        (Join-Path $root "tools\window_guard.ps1"), '-ProcessId', $proc.Id, '-X', (($p - 1) * 660), '-Y', 0, '-Title', 'tekken' | Out-Null
    $procs += $proc
    # The first instance replays up to the fight and waits for the peer there; only then the second one starts
    if ($p -eq $order[0]) {
        $t0 = Get-Date
        # (never before 20 s: the wait ended at once in a few runs and both instances booted together)
        while (((Get-Date) - $t0).TotalSeconds -lt 240 -and (((Get-Date) - $t0).TotalSeconds -lt 20 -or (-not $proc.HasExited -and
               -not (Select-String -Path "$run\tekken6.log" -Pattern 'T6 net: session at' -Quiet -ErrorAction SilentlyContinue)))) {
            Start-Sleep -Seconds 2
        }
    }
}
$deadline = (Get-Date).AddSeconds($Seconds)  # from the start of the second instance
foreach ($proc in $procs) {
    $left = [Math]::Max(1, [int]($deadline - (Get-Date)).TotalMilliseconds)
    if (-not $proc.WaitForExit($left)) { Stop-Process -Id $proc.Id -Force }
}
foreach ($p in 1, 2) {
    "== player $p"
    Select-String -Path "$root\runs\${Name}_p$p\tekken6.log" -Pattern 'T6 net:|T6 crash|hang watch: step' |
        Select-Object -Last 8 | ForEach-Object { $_.Line.Substring([Math]::Min(55, $_.Line.Length)) }
}
