# End-to-end test of whole matches: local lobby server + two launchers (--auto quick --auto-rematch N) -> match -> winner ->
# rematch -> match. Prints every change of the two status files and the character / stage lines of the game logs.
#   .\tools\e2e_match.ps1 [-Rematches 1] [-Seconds 900]
param([int]$Rematches = 1, [int]$Seconds = 900)
. "$PSScriptRoot\t6_env.ps1"
$exe = "$root\launcher\build\Tekken6 Native Online.exe"
$srv = Start-Process $py -ArgumentList "$root\server\t6_lobby_server.py", '--tcp', '7700', '--udp', '7701', '--db', "$env:TEMP\t6_e2e.db" -WindowStyle Hidden -PassThru -RedirectStandardOutput "$root\runs\e2e_server.log"
Start-Sleep 2
Remove-Item "$root\runs\online_1\status.txt", "$root\runs\online_2\status.txt" -ErrorAction SilentlyContinue
$env:T6_LAUNCHER_NOFOCUS = '1'
$l1 = Start-Process $exe -ArgumentList '--auto', 'quick', '--auto-rematch', $Rematches -WorkingDirectory $root -PassThru
# staggered boot: the launcher starts its game right away, the second one only when the first game is parked
$tb = Get-Date
while (((Get-Date) - $tb).TotalSeconds -lt 240 -and -not ((Get-Content "$root\runs\online_1\status.txt" -ErrorAction SilentlyContinue) -match 'state ready')) { Start-Sleep -Milliseconds 500 }
$l2 = Start-Process $exe -ArgumentList '--instance', '2', '--auto', 'quick', '--auto-rematch', $Rematches -WorkingDirectory $root -PassThru
$guarded = @{}; $vis = @{}; $last = @{ 1 = ''; 2 = '' }; $t0 = Get-Date; $winners = 0; $games = 0
function St($i) { try { (Get-Content "$root\runs\online_$i\status.txt" -ErrorAction Stop) -join ' | ' } catch { '' } }
while (((Get-Date) - $t0).TotalSeconds -lt $Seconds) {
    foreach ($g in Get-Process tekken6 -ErrorAction SilentlyContinue) {
        if (-not $guarded[$g.Id]) {
            $guarded[$g.Id] = $true; $games++
            Start-Process powershell -WindowStyle Hidden -ArgumentList '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "$root\tools\window_guard.ps1", '-ProcessId', $g.Id, '-X', 0, '-Y', 0, '-Title', 'tekken' | Out-Null
            "{0,4:n0} s: game process {1} started" -f ((Get-Date) - $t0).TotalSeconds, $g.Id
        }
    }
    # the game's window: hidden while parked, shown with the match
    foreach ($g in Get-Process tekken6 -ErrorAction SilentlyContinue) {
        $v = $g.MainWindowHandle -ne 0
        if ($vis[$g.Id] -ne $v) { $vis[$g.Id] = $v; "{0,4:n0} s: game {1} window {2}" -f ((Get-Date) - $t0).TotalSeconds, $g.Id, $(if ($v) { 'visible' } else { 'hidden' }) }
    }
    foreach ($i in 1, 2) {
        $s = St $i
        if ($s -and $s -ne $last[$i]) { if ($s -match 'winner' -and $last[$i] -notmatch 'winner') { $winners++ }; $last[$i] = $s; "{0,4:n0} s: [{1}] {2}" -f ((Get-Date) - $t0).TotalSeconds, $i, $s }
    }
    if ($winners -ge 2 * ($Rematches + 1)) { Start-Sleep 8; break }
    Start-Sleep -Milliseconds 500
}
"games started: $games, winner lines: $winners"
Get-Content "$root\runs\e2e_server.log" -ErrorAction SilentlyContinue | Select-String 'state hash' | ForEach-Object { "server: $($_.Line)" }
foreach ($i in 1, 2) { Select-String -Path "$root\runs\online_$i\tekken6.log" -Pattern 'player setup|stage' | Select-Object -First 6 | ForEach-Object { "[$i] " + $_.Line.Substring([Math]::Min(40, $_.Line.Length)) } }
foreach ($p in $l1, $l2, $srv) { try { Stop-Process -Id $p.Id -Force -ErrorAction Stop } catch {} }
Start-Sleep 1
Get-Process tekken6 -ErrorAction SilentlyContinue | Stop-Process -Force
