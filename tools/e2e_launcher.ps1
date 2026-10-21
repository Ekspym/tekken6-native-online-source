# End-to-end test: local lobby server + two launchers (--auto quick) -> both games -> status files -> one game closed.
#   .\tools\e2e_launcher.ps1 [-Seconds 240]
param([int]$Seconds = 240)
. "$PSScriptRoot\t6_env.ps1"
$exe = "$root\launcher\build\Tekken6 Native Online.exe"
$srv = Start-Process $py -ArgumentList "$root\server\t6_lobby_server.py", '--tcp', '7700', '--udp', '7701', '--db', "$env:TEMP\t6_e2e.db" -WindowStyle Hidden -PassThru
Start-Sleep 2
Remove-Item "$root\runs\online_1\status.txt", "$root\runs\online_2\status.txt" -ErrorAction SilentlyContinue
$env:T6_LAUNCHER_NOFOCUS = '1'
$l1 = Start-Process $exe -ArgumentList '--auto', 'quick' -WorkingDirectory $root -PassThru
# staggered boot: the launcher starts its game right away, the second one only when the first game is parked
$tb = Get-Date
while (((Get-Date) - $tb).TotalSeconds -lt 240 -and -not ((Get-Content "$root\runs\online_1\status.txt" -ErrorAction SilentlyContinue) -match 'state ready')) { Start-Sleep -Milliseconds 500 }
"game 1 parked after $([int]((Get-Date) - $tb).TotalSeconds) s"
$l2 = Start-Process $exe -ArgumentList '--instance', '2', '--auto', 'quick' -WorkingDirectory $root -PassThru
$guarded = @{}; $last = @{ 1 = ''; 2 = '' }; $t0 = Get-Date; $killed = $false
function St($i) { try { (Get-Content "$root\runs\online_$i\status.txt" -ErrorAction Stop) -join ' | ' } catch { '' } }
while (((Get-Date) - $t0).TotalSeconds -lt $Seconds) {
    foreach ($g in Get-Process tekken6 -ErrorAction SilentlyContinue) {
        if (-not $guarded[$g.Id]) {
            $guarded[$g.Id] = $true
            Start-Process powershell -WindowStyle Hidden -ArgumentList '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "$root\tools\window_guard.ps1", '-ProcessId', $g.Id, '-X', 0, '-Y', 0, '-Title', 'tekken' | Out-Null
            "{0,4:n0} s: game process {1} started" -f ((Get-Date) - $t0).TotalSeconds, $g.Id
        }
    }
    foreach ($i in 1, 2) { $s = St $i; if ($s -and $s -ne $last[$i]) { $last[$i] = $s; "{0,4:n0} s: [{1}] {2}" -f ((Get-Date) - $t0).TotalSeconds, $i, $s } }
    # once both are in the fight for 20 s: close game 1, the other must report the disconnect
    if (-not $killed -and $last[1] -match 'state fight' -and $last[2] -match 'state fight') {
        if (-not $fightAt) { $fightAt = Get-Date }
        if (((Get-Date) - $fightAt).TotalSeconds -gt 20) {
            $killed = $true
            $g1 = Get-CimInstance Win32_Process -Filter "Name='tekken6.exe'" | Where-Object { $_.CommandLine -match 'online_1' } | Select-Object -First 1
            if ($g1) { Stop-Process -Id $g1.ProcessId -Force; "{0,4:n0} s: game 1 closed" -f ((Get-Date) - $t0).TotalSeconds }
            $closeAt = Get-Date
        }
    }
    if ($killed -and ($last[2] -match 'disconnected' -or ((Get-Date) - $closeAt).TotalSeconds -gt 40)) { break }
    Start-Sleep -Milliseconds 500
}
"final [1] $(St 1)"; "final [2] $(St 2)"
foreach ($p in $l1, $l2, $srv) { try { Stop-Process -Id $p.Id -Force -ErrorAction Stop } catch {} }
Start-Sleep 1
Get-Process tekken6 -ErrorAction SilentlyContinue | Stop-Process -Force
