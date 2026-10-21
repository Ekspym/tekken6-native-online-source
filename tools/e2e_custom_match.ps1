# End-to-end test of custom looks in an online match (2026-10-15): local lobby server (guests), launchers --instance 2
# and 3 (--auto quick). Instance 2 plays LARS with a custom look (its custom_looks set in launcher_native_2.ini for the
# test), instance 3 LEE without one. In the fight: a picture of both game windows (runs\e2e_custom_match\) - both must
# show the custom look on LARS (the launcher -> server -> 'start' custom1/custom2 -> both games inject the record).
#   .\tools\e2e_custom_match.ps1 [-Record <160 hex>] [-Seconds 300]
param([string]$Record = '00020000000000000016002A002B003700000060000000000000000000DC000000DD00000000000000000000020201000000000000000000000000000000000000000000000000000000000000000000', [int]$Seconds = 300)
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public class ShM { public delegate bool P(IntPtr h, IntPtr l);
[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
[DllImport("user32.dll")] public static extern bool EnumWindows(P p, IntPtr l);
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
[DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
[DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint f);
[StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
public static IntPtr Find(uint pid) { IntPtr f = IntPtr.Zero; EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p); RECT r; GetWindowRect(h, out r); if (p == pid && IsWindowVisible(h) && r.R - r.L > 300) { f = h; return false; } return true; }, IntPtr.Zero); return f; } }
"@
[ShM]::SetProcessDPIAware() | Out-Null
. "$PSScriptRoot\t6_env.ps1"; $py = "$root\toolchain\python-embed\python.exe"
$exe = "$root\launcher\build\Tekken6 Native Online.exe"; $out = "$root\runs\e2e_custom_match"
New-Item -ItemType Directory -Force $out | Out-Null; Remove-Item "$out\*.png" -ErrorAction SilentlyContinue
foreach ($i in 2, 3) { Copy-Item "$root\launcher_native_$i.ini" "$out\launcher_native_$i.ini.bak" -Force }
# instance 2: LARS (index 41) P1 costume with the custom look
$ini2 = Get-Content "$root\launcher_native_2.ini" | Where-Object { $_ -notmatch '^custom_looks=' -and $_ -notmatch '^costumes=' }
$ini2 = @($ini2[0], ('custom_looks=@ByteArray({\"41_0\":\"' + $Record + '\"})')) + $ini2[1..($ini2.Count - 1)]
Set-Content "$root\launcher_native_2.ini" $ini2 -Encoding ascii
$ini3 = Get-Content "$root\launcher_native_3.ini" | Where-Object { $_ -notmatch '^custom_looks=' }
Set-Content "$root\launcher_native_3.ini" $ini3 -Encoding ascii
$db = "$out\t6_e2e_custom.db"; Remove-Item $db -ErrorAction SilentlyContinue
$srv = Start-Process $py -ArgumentList '-I', "$root\server\t6_lobby_server.py", '--tcp', '7792', '--udp', '7793', '--db', $db, '--guests', '--allow-same-ip', '--region-wait', '2' -WindowStyle Hidden -PassThru -RedirectStandardOutput "$out\server.log" -RedirectStandardError "$out\server_err.log"
Start-Sleep 2
Remove-Item "$root\runs\online_2\status.txt", "$root\runs\online_3\status.txt" -ErrorAction SilentlyContinue
$env:T6_LOBBY_SERVER = '127.0.0.1:7792'; $env:T6_LAUNCHER_NOFOCUS = '1'; $env:T6_AUTO_ACCEPT = '1'; $env:T6_LAUNCHER_LOG = "$out\lnch_"
Remove-Item "$out\lnch_*" -ErrorAction SilentlyContinue
$env:T6_TEST_CHAR = '41'
$l2 = Start-Process $exe -ArgumentList '--instance', '2', '--auto', 'quick' -WorkingDirectory $root -PassThru
$tb = Get-Date
while (((Get-Date) - $tb).TotalSeconds -lt 240 -and -not ((Get-Content "$root\runs\online_2\status.txt" -ErrorAction SilentlyContinue) -match 'state ready')) { Start-Sleep -Milliseconds 500 }
"game 2 parked after $([int]((Get-Date) - $tb).TotalSeconds) s"
$env:T6_TEST_CHAR = '16'
$l3 = Start-Process $exe -ArgumentList '--instance', '3', '--auto', 'quick' -WorkingDirectory $root -PassThru
$guarded = @{}; $t0 = Get-Date; $fightAt = $null; $shot = $false
function St($i) { try { (Get-Content "$root\runs\online_$i\status.txt" -ErrorAction Stop) -join ' | ' } catch { '' } }
while (((Get-Date) - $t0).TotalSeconds -lt $Seconds -and -not $shot) {
    foreach ($g in Get-CimInstance Win32_Process -Filter "Name='tekken6.exe'" | Where-Object { $_.CommandLine -match 'online_2|online_3' }) {
        if (-not $guarded[$g.ProcessId]) {
            $guarded[$g.ProcessId] = $true
            Start-Process powershell -WindowStyle Hidden -ArgumentList '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "$root\tools\window_guard.ps1", '-ProcessId', $g.ProcessId, '-X', 0, '-Y', 0, '-Title', 'tekken' | Out-Null
        }
    }
    if ((St 2) -match 'state fight' -and (St 3) -match 'state fight') {
        if (-not $fightAt) { $fightAt = Get-Date; "{0,4:n0} s: both in the fight" -f ((Get-Date) - $t0).TotalSeconds }
        if (((Get-Date) - $fightAt).TotalSeconds -gt 6) {
            foreach ($g in Get-CimInstance Win32_Process -Filter "Name='tekken6.exe'" | Where-Object { $_.CommandLine -match 'online_2|online_3' }) {
                $which = if ($g.CommandLine -match 'online_2') { 'game2' } else { 'game3' }
                $h = [ShM]::Find([uint32]$g.ProcessId)
                if ($h -ne [IntPtr]::Zero) {
                    $r = New-Object ShM+RECT; [ShM]::GetWindowRect($h, [ref]$r) | Out-Null
                    $bmp = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T); $gr = [System.Drawing.Graphics]::FromImage($bmp)
                    $hdc = $gr.GetHdc(); [ShM]::PrintWindow($h, $hdc, 2) | Out-Null; $gr.ReleaseHdc($hdc); $gr.Dispose()
                    $o = New-Object System.Drawing.Bitmap 800, ([int](800 * $bmp.Height / $bmp.Width)); $g2 = [System.Drawing.Graphics]::FromImage($o); $g2.DrawImage($bmp, 0, 0, $o.Width, $o.Height); $g2.Dispose()
                    $o.Save("$out\$which.png"); $o.Dispose(); $bmp.Dispose(); "picture $which"
                }
            }
            $shot = $true
        }
    }
    Start-Sleep -Milliseconds 500
}
"final [2] $(St 2)"; "final [3] $(St 3)"
foreach ($i in 2, 3) {
    $log = "$root\runs\online_$i\tekken6.log"
    "game $i custom: " + ((Select-String -Path $log -Pattern 'T6 custom: side' -ErrorAction SilentlyContinue | Select-Object -Last 2 | ForEach-Object { $_.Line -replace '^.*T6 custom: ', '' }) -join ' / ')
}
foreach ($l in $l2, $l3) { if (-not $l.HasExited) { Stop-Process -Id $l.Id -Force } }
Get-CimInstance Win32_Process -Filter "Name='tekken6.exe'" | Where-Object { $_.CommandLine -match 'online_2|online_3' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }
Stop-Process -Id $srv.Id -Force
foreach ($i in 2, 3) { Copy-Item "$out\launcher_native_$i.ini.bak" "$root\launcher_native_$i.ini" -Force }
