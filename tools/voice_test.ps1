# (voice_test.ps1 = hold_test.ps1 with the launcher's rematch: park + new start; sound voice accounting 'T6 voices')
# Hold-point test: both instances boot WITHOUT match settings, park before the fight loading (T6_NET_HOLD, status
# 'state ready'), wait there -Idle1 / -Idle2 seconds, then both get a 'start' line (T6_NET_CMD) like the launcher sends
# it. Prints the time from 'start' to the session and compares the fight state of both instances.
param([string]$Name = 'h1', [int]$Seconds = 90, [int]$Idle = 10, [int]$Char1 = 3, [int]$Char2 = 33, [int]$Stage = 5,
      [int]$Seed = 424242, [int]$LagMs = 0, [string]$InputMode = 'random', [int]$Rounds = 3, [int]$Time = 0, [int]$Rematch = 0, [string]$MenuCmd = 'rematch', [int]$SlowP1 = 0, [int]$SndLife = 0, [int]$BootSeed2 = 0, [string]$Env2 = '', [switch]$NoDiag, [int]$PortBase = 7199, [string]$User1 = 'user', [string]$User2 = 'user2', [string[]]$GameArgs = @())
. "$PSScriptRoot\t6_env.ps1"
# (-PortBase / -User1 / -User2: a second test next to a running one - own UDP ports and profile folders)
foreach ($u in $User1, $User2) { if (-not (Test-Path "$root\$u")) { Copy-Item -Recurse "$root\user" "$root\$u" } }
$procs = @()
foreach ($p in 1, 2) {
    $run = "$root\runs\${Name}_p$p"
    Remove-Item -Recurse -Force $run -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force $run | Out-Null
    foreach ($v in 'T6_NET_PLAYER', 'T6_NET_REMOTE', 'T6_NET_LOCAL_PORT', 'T6_NET_SEED', 'T6_CHAR_P1', 'T6_CHAR_P2', 'T6_STAGE', 'T6_ROUNDS',
                   'T6_SNAPTEST', 'T6_REC', 'T6_HASH', 'T6_NET_PAGEHASH') { Remove-Item "Env:$v" -ErrorAction SilentlyContinue }
    if ($SlowP1 -gt 0 -and $p -eq 1) { $env:T6_TEST_SLOW_MS = "$SlowP1" } else { Remove-Item Env:T6_TEST_SLOW_MS -ErrorAction SilentlyContinue }  # (-SlowP1: only instance 1 too slow for 60 FPS)
    $env:T6_NET = '1'; $env:T6_NET_HOLD = '1'; $env:T6_FIX_SEED = $(if ($BootSeed2 -and $p -eq 2) { "$BootSeed2" } else { '1' });  # (-BootSeed2 n: instance 2 boots with another seed, like two players' PCs)
    $env:T6_PLAY = "$root\tools\menu_vs.bin"
    $env:T6_VIRTUAL_TB = '1'; $env:T6_SOUND_SEPARATE = '1'; $env:T6_SNAP_EXCLUDE_FILE = "$root\tools\netplay_exclude.txt"
    $env:T6_NET_STATUS = "$run\status.txt"; $env:T6_NET_CMD = "$run\cmd.txt"
    if ($NoDiag) { Remove-Item Env:T6_NET_DUMP, Env:T6_RAND_LOG -ErrorAction SilentlyContinue; $env:T6_NET_DUMP_DIR = $run }  # (-NoDiag: no page dumps / rand log - timing and profiles)
    else { $env:T6_NET_DUMP = '1'; $env:T6_RAND_LOG = '1'; $env:T6_NET_DUMP_DIR = $run }
    [Environment]::SetEnvironmentVariable('ggpo.network.delay', "$LagMs", 'Process')
    $env:REX_LOG_LEVEL = 'info'
    # -Env2 'NAME=VALUE;NAME=VALUE': instance 2 only (e.g. another internal resolution: still in sync?)
    foreach ($kv in ($Env2 -split ';' | Where-Object { $_ })) { $k, $v = $kv -split '=', 2; if ($p -eq 2) { Set-Item "Env:$k" $v } else { Remove-Item "Env:$k" -ErrorAction SilentlyContinue } }
    if ($SndLife) { $env:T6_SND_LIFE = "$run\snd_life.csv" } else { Remove-Item Env:T6_SND_LIFE -ErrorAction SilentlyContinue }  # (-SndLife 1: one line per sound voice)
    $user = if ($p -eq 1) { "$root\$User1" } else { "$root\$User2" }
    $a = @("--game_data_root=$root\game", "--user_data_root=$user", "--cache_root=$run\cache", "--log_file=$run\tekken6.log",
           '--fullscreen=false', '--async_shader_compilation=true', '--render_target_path_d3d12=rov', '--input_backend=mmjoystick') + $GameArgs  # (-GameArgs: e.g. the launcher's tearing flag)
    $proc = Start-Process -FilePath "$root\build\tekken6.exe" -ArgumentList $a -WorkingDirectory "$root\build" -PassThru
    Start-Process powershell -WindowStyle Hidden -ArgumentList '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
        "$root\tools\window_guard.ps1", '-ProcessId', $proc.Id, '-X', (($p - 1) * 660), '-Y', 0, '-Title', 'tekken' | Out-Null
    $procs += $proc
    # staggered boot: the next instance starts when this one stands at the hold point
    $t0 = Get-Date
    while (((Get-Date) - $t0).TotalSeconds -lt 240 -and -not $proc.HasExited) {
        if ((Test-Path "$run\status.txt") -and ((Get-Content "$run\status.txt" -ErrorAction SilentlyContinue) -match 'state ready')) { break }
        Start-Sleep -Milliseconds 500
    }
    "p$p ready after $([int]((Get-Date) - $t0).TotalSeconds) s"
}
Start-Sleep -Seconds $Idle
$tStart = Get-Date
foreach ($p in 1, 2) {
    $line = "start player=$p port=$($PortBase + $p) remote=127.0.0.1:$($PortBase + 3 - $p) delay=1 seed=$Seed char1=$Char1 char2=$Char2 stage=$Stage rounds=$Rounds input=$InputMode$(if ($Time) { " time=$Time" })"
    Set-Content -Path "$root\runs\${Name}_p$p\cmd.txt" -Value $line -Encoding ascii
}
$last = @{ 1 = ''; 2 = '' }
$rematches = 0
$deadline = (Get-Date).AddSeconds($Seconds)
while ((Get-Date) -lt $deadline) {
    foreach ($p in 1, 2) {
        $st = (Get-Content "$root/runs/${Name}_p$p/status.txt" -ErrorAction SilentlyContinue) -join ' | '
        if ($st -and $st -ne $last[$p]) { $last[$p] = $st; "p$p +$([Math]::Round(((Get-Date) - $tStart).TotalSeconds, 1)) s: $st" }
    }
    # -Rematch N: like the launcher - both park after the match, then a new 'start' from the hold point
    if ($rematches -lt $Rematch -and $last[1] -match '^state menu' -and $last[2] -match '^state menu') {
        Start-Sleep -Seconds 2
        foreach ($p in 1, 2) { Set-Content -Path "$root/runs/${Name}_p$p/cmd.txt" -Value 'park' -Encoding ascii }
        $rematches++
        "park $rematches sent at +$([Math]::Round(((Get-Date) - $tStart).TotalSeconds, 1)) s"
        $tp = Get-Date
        while (((Get-Date) - $tp).TotalSeconds -lt 40) {
            $r = 0
            foreach ($p in 1, 2) { if (((Get-Content "$root/runs/${Name}_p$p/status.txt" -ErrorAction SilentlyContinue) -join ' ') -match '^state ready') { $r++ } }
            if ($r -eq 2) { break }
            Start-Sleep -Milliseconds 300
        }
        Start-Sleep -Seconds 2
        foreach ($p in 1, 2) {
            $line = "start player=$p port=$($PortBase + $p) remote=127.0.0.1:$($PortBase + 3 - $p) delay=1 seed=$($Seed + $rematches) char1=$Char1 char2=$Char2 stage=$Stage rounds=$Rounds input=$InputMode skipintro=1$(if ($Time) { " time=$Time" })"
            Set-Content -Path "$root/runs/${Name}_p$p/cmd.txt" -Value $line -Encoding ascii
        }
        "start $rematches sent"
        $last[1] = ''; $last[2] = ''
        Start-Sleep -Seconds 4
    }
    if (($procs | Where-Object { -not $_.HasExited }).Count -eq 0) { break }
    Start-Sleep -Milliseconds 200
}
foreach ($proc in $procs) { if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force } }
foreach ($p in 1, 2) {
    $l = "$root\runs\${Name}_p$p\tekken6.log"
    Select-String -Path $l -Pattern 'T6 net: start after|T6 net: session at|session running|player setup .* side|T6 crash: host|rematch' |
        Select-Object -First 14 | ForEach-Object { "p${p}: " + $_.Line.Substring(13, 12) + ' ' + $_.Line.Substring(58, [Math]::Min(120, $_.Line.Length - 58)) }
}
& $py "$root\tools\state_diff.py" "$root\runs\${Name}_p1\fight_state.csv" "$root\runs\${Name}_p2\fight_state.csv" |
    Select-String -Pattern 'common|players|rng3'
