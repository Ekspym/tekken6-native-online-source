# Two launchers (no server needed) start their games; both park at the hold point; prints the two status files
# (statehash / heaphash) and leaves runs\online_N\hold_pages.txt for a comparison. Everything is closed afterwards.
. "$PSScriptRoot\t6_env.ps1"; $exe = "$root\launcher\build\Tekken6 Native Online.exe"; $env:T6_LAUNCHER_NOFOCUS = '1'
foreach ($i in 1, 2) { foreach ($f in 'status.txt', 'hold_pages.txt') { $p = "$root\runs\online_$i\$f"; if (Test-Path $p) { Remove-Item -LiteralPath $p } } }
function WaitReady($i) {
    $t = Get-Date
    while (((Get-Date) - $t).TotalSeconds -lt 200 -and -not ((Get-Content "$root\runs\online_$i\status.txt" -ErrorAction SilentlyContinue) -match 'state ready')) { Start-Sleep -Milliseconds 500 }
    Start-Sleep 2
}
$l1 = Start-Process $exe -WorkingDirectory $root -PassThru; WaitReady 1
$l2 = Start-Process $exe -ArgumentList '--instance', '2' -WorkingDirectory $root -PassThru; WaitReady 2
foreach ($i in 1, 2) { "[$i] " + ((Get-Content "$root\runs\online_$i\status.txt" -ErrorAction SilentlyContinue) -join ' | ') }
foreach ($p in $l1, $l2) { try { Stop-Process -Id $p.Id -Force -ErrorAction Stop } catch {} }
Start-Sleep 1
Get-Process tekken6 -ErrorAction SilentlyContinue | Stop-Process -Force
foreach ($i in 1, 2) { Get-Item "$root\runs\online_$i\hold_pages.txt" -ErrorAction SilentlyContinue | ForEach-Object { "$($_.FullName) $($_.Length)" } }
