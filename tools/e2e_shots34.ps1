# (copy of e2e_shots.ps1 on launcher instances 3 / 4: instance 1 holds the developer's own login)
# End-to-end test of what the player SEES: local server + two launchers (--auto quick --auto-rematch N), screenshots of
# game 1's window whenever it is visible (first 10 s after it appears, and around the result) -> runs\shots\*.png and a
# contact sheet runs\shots_sheet.png. No visible shot may show the character / stage select or the result menu.
#   .\tools\e2e_shots.ps1 [-Rematches 1] [-Seconds 300]
param([int]$Rematches = 1, [int]$Seconds = 300, [int]$Leaves = 0)  # -Leaves n: leave the match n times (the game parks, the next quick match follows)
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices;
public class WinShot2 { public delegate bool P(IntPtr h, IntPtr l);
[DllImport("user32.dll")] public static extern bool EnumWindows(P p, IntPtr l);
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
[DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
[DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint f);
[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
public struct RECT { public int L, T, R, B; }
public static IntPtr Find(uint pid){ IntPtr r=IntPtr.Zero; int best=0; EnumWindows((h,l)=>{ uint p; GetWindowThreadProcessId(h,out p); if(p==pid && IsWindowVisible(h)){ RECT c; GetWindowRect(h,out c); int a=(c.R-c.L)*(c.B-c.T); if(a>best){best=a;r=h;} } return true; }, IntPtr.Zero); return r; } }
"@ -ErrorAction SilentlyContinue
try { [WinShot2]::SetProcessDPIAware() | Out-Null } catch {}  # whole window in the shots (they were cut on the right)
. "$PSScriptRoot\t6_env.ps1"
$exe = if ($env:T6_E2E_EXE) { $env:T6_E2E_EXE } else { "$root\launcher\build\Tekken6 Native Online.exe" }
function St($i) { try { (Get-Content "$root\runs\online_$($i + 2)\status.txt" -ErrorAction Stop) -join ' | ' } catch { '' } }
function Shot($procId, $file) {
    $h = [WinShot2]::Find([uint32]$procId); if ($h -eq [IntPtr]::Zero) { return $false }
    $r = New-Object WinShot2+RECT; [WinShot2]::GetWindowRect($h, [ref]$r) | Out-Null
    $w = $r.R - $r.L; $hg = $r.B - $r.T; if ($w -lt 50) { return $false }
    $bmp = New-Object System.Drawing.Bitmap $w, $hg; $g = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc(); [WinShot2]::PrintWindow($h, $hdc, 2) | Out-Null; $g.ReleaseHdc($hdc); $g.Dispose()
    $out = New-Object System.Drawing.Bitmap 480, 270; $g2 = [System.Drawing.Graphics]::FromImage($out); $g2.DrawImage($bmp, 0, 0, 480, 270); $g2.Dispose()
    $out.Save($file, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose(); $out.Dispose(); return $true
}
$shots = "$root\runs\shots"; Remove-Item -Recurse -Force $shots -ErrorAction SilentlyContinue; New-Item -ItemType Directory -Force $shots | Out-Null
$env:T6_ROUNDS = '3'; $env:T6_ROUND_TIME = '20'; $env:T6_LAUNCHER_NOFOCUS = '1'; $env:T6_LOBBY_SERVER = '127.0.0.1:7700'; $env:T6_AUTO_ACCEPT = '1'  # (the launcher's default is the VPS; matches are accepted at once)
$srv = Start-Process $py -ArgumentList "$root\server\t6_lobby_server.py", '--tcp', '7700', '--udp', '7701', '--db', "$env:TEMP\t6_e2e.db", '--guests' -WindowStyle Hidden -PassThru
Start-Sleep 2
Remove-Item "$root\runs\online_3\status.txt", "$root\runs\online_4\status.txt" -ErrorAction SilentlyContinue
$l1 = Start-Process $exe -ArgumentList '--instance', '3', '--auto', 'quick', '--auto-rematch', $Rematches, '--auto-leave', $Leaves -WorkingDirectory $root -PassThru
$tb = Get-Date
while (((Get-Date) - $tb).TotalSeconds -lt 240 -and -not ((St 1) -match 'state ready')) { Start-Sleep -Milliseconds 500 }
$l2 = Start-Process $exe -ArgumentList '--instance', '4', '--auto', 'quick', '--auto-rematch', $Rematches, '--auto-leave', $Leaves -WorkingDirectory $root -PassThru
$guarded = @{}; $last = @{ 1 = ''; 2 = '' }; $t0 = Get-Date; $winners = 0; $n = 0; $visSince = $null; $endAt = $null
while (((Get-Date) - $t0).TotalSeconds -lt $Seconds) {
    $now = ((Get-Date) - $t0).TotalSeconds
    foreach ($g in Get-Process tekken6 -ErrorAction SilentlyContinue) {
        if (-not $guarded[$g.Id]) {
            $guarded[$g.Id] = $true
            Start-Process powershell -WindowStyle Hidden -ArgumentList '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "$root\tools\window_guard.ps1", '-ProcessId', $g.Id, '-X', 0, '-Y', 0, '-Title', 'tekken' | Out-Null
        }
    }
    foreach ($i in 1, 2) {
        $s = St $i
        if ($s -and $s -ne $last[$i]) { if ($s -match 'winner' -and $last[$i] -notmatch 'winner') { $winners++ }; $last[$i] = $s; "{0,6:n1} s: [{1}] {2}" -f $now, $i, $s }
    }
    $g1 = Get-CimInstance Win32_Process -Filter "Name='tekken6.exe'" | Where-Object { $_.CommandLine -match 'online_3' } | Select-Object -First 1
    if ($g1) {
        $vis = [WinShot2]::Find([uint32]$g1.ProcessId) -ne [IntPtr]::Zero
        if ($vis -and -not $visSince) { $visSince = Get-Date; "{0,6:n1} s: game 1 window VISIBLE ({1})" -f $now, $last[1] }
        if (-not $vis -and $visSince) { $visSince = $null; "{0,6:n1} s: game 1 window hidden ({1})" -f $now, $last[1] }
        if ($vis -and ((((Get-Date) - $visSince).TotalSeconds -lt 10) -or $last[1] -match 'result|menu|winner')) {
            $st = if ($last[1] -match 'state (\w+) \| mode (\d+)') { "$($Matches[1])$($Matches[2])" } elseif ($last[1] -match 'state (\w+)') { $Matches[1] } else { 'none' }
            if (Shot $g1.ProcessId ("$shots\{0:000}_{1:0000}_{2}.png" -f $n, [int]($now * 10), $st)) { $n++ }
        }
    }
    if ($winners -ge 2 * ($Rematches + $Leaves + 1) -and -not $endAt) { $endAt = (Get-Date).AddSeconds(14) }
    if ($endAt -and (Get-Date) -gt $endAt) { break }
    Start-Sleep -Milliseconds 250
}
"shots: $n, winner lines: $winners"
foreach ($p in $l1, $l2, $srv) { try { Stop-Process -Id $p.Id -Force -ErrorAction Stop } catch {} }
Start-Sleep 1
Get-Process tekken6 -ErrorAction SilentlyContinue | Stop-Process -Force
& $py -I "$root\tools\shots_sheet.py" $shots "$root\runs\shots_sheet"
