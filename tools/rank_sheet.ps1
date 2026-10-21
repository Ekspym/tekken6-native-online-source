# Which value of the HUD plate's rank byte shows which rank: for every value one short match of two launchers (guests,
# local server) with T6_PLATE_RANK=<n> (src/t6_watch.cpp: rank byte of side 2's plate), a full-size screenshot of
# game 1's window in round 1 -> runs\ranks\rank_<n>.png. The second launcher starts when the first game is parked.
#   .\tools\rank_sheet.ps1 -Values 0,5,10,15        (about 70 s per value)
param([int[]]$Values = @(0, 5, 10, 15, 20, 25, 30, 35, 40), [int]$Wait = 6)
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public class WinShotR { public delegate bool P(IntPtr h, IntPtr l);
[DllImport("user32.dll")] public static extern bool EnumWindows(P p, IntPtr l);
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
[DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
[DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint f);
[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
public struct RECT { public int L, T, R, B; }
public static IntPtr Find(uint pid){ IntPtr r=IntPtr.Zero; int best=0; EnumWindows((h,l)=>{ uint p; GetWindowThreadProcessId(h,out p); if(p==pid && IsWindowVisible(h)){ RECT c; GetWindowRect(h,out c); int a=(c.R-c.L)*(c.B-c.T); if(a>best){best=a;r=h;} } return true; }, IntPtr.Zero); return r; } }
"@ -ErrorAction SilentlyContinue
try { [WinShotR]::SetProcessDPIAware() | Out-Null } catch {}
. "$PSScriptRoot\t6_env.ps1"
$exe = "$root\launcher\build\Tekken6 Native Online.exe"
$out = "$root\runs\ranks"; New-Item -ItemType Directory -Force $out | Out-Null
function St($i) { try { (Get-Content "$root\runs\online_$i\status.txt" -ErrorAction Stop) -join ' | ' } catch { '' } }
function Shot($procId, $file) {
    $h = [WinShotR]::Find([uint32]$procId); if ($h -eq [IntPtr]::Zero) { return $false }
    $r = New-Object WinShotR+RECT; [WinShotR]::GetWindowRect($h, [ref]$r) | Out-Null
    $w = $r.R - $r.L; $hg = $r.B - $r.T; if ($w -lt 50) { return $false }
    $bmp = New-Object System.Drawing.Bitmap $w, $hg; $g = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc(); [WinShotR]::PrintWindow($h, $hdc, 2) | Out-Null; $g.ReleaseHdc($hdc); $g.Dispose()
    $bmp.Save($file, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose(); return $true
}
function Stop-All($procs) {
    foreach ($p in $procs) { if ($p) { try { Stop-Process -Id $p.Id -Force -ErrorAction Stop } catch {} } }
    Start-Sleep 1
    Get-Process tekken6 -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep 1
}
$env:T6_ROUNDS = '1'; $env:T6_ROUND_TIME = '20'; $env:T6_LAUNCHER_NOFOCUS = '1'
foreach ($n in $Values) {
    $env:T6_PLATE_RANK = "$n"
    Remove-Item "$root\runs\online_1\status.txt", "$root\runs\online_2\status.txt" -ErrorAction SilentlyContinue
    $srv = Start-Process $py -ArgumentList "$root\server\t6_lobby_server.py", '--tcp', '7700', '--udp', '7701', '--db', "$env:TEMP\t6_ranks.db", '--guests' -WindowStyle Hidden -PassThru
    Start-Sleep 2
    $l1 = Start-Process $exe -ArgumentList '--auto', 'quick' -WorkingDirectory $root -PassThru
    $tb = Get-Date
    while (((Get-Date) - $tb).TotalSeconds -lt 120 -and -not ((St 1) -match 'state ready')) { Start-Sleep -Milliseconds 500 }
    $l2 = Start-Process $exe -ArgumentList '--instance', '2', '--auto', 'quick' -WorkingDirectory $root -PassThru
    $guarded = @{}; $t0 = Get-Date; $done = $false; $fightAt = $null
    while (((Get-Date) - $t0).TotalSeconds -lt 150 -and -not $done) {
        foreach ($g in Get-Process tekken6 -ErrorAction SilentlyContinue) {
            if (-not $guarded[$g.Id]) {
                $guarded[$g.Id] = $true
                Start-Process powershell -WindowStyle Hidden -ArgumentList '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "$root\tools\window_guard.ps1", '-ProcessId', $g.Id, '-X', 0, '-Y', 0, '-Title', 'tekken' | Out-Null
            }
        }
        if (-not $fightAt -and (St 1) -match 'state fight') { $fightAt = Get-Date }
        if ($fightAt -and ((Get-Date) - $fightAt).TotalSeconds -ge $Wait) {
            $g1 = Get-CimInstance Win32_Process -Filter "Name='tekken6.exe'" | Where-Object { $_.CommandLine -match 'online_1' } | Select-Object -First 1
            $ok = $g1 -and (Shot $g1.ProcessId ("$out\rank_{0:00}.png" -f $n))
            "rank $n : shot $ok ($(St 1))"
            $done = $true
        }
        Start-Sleep -Milliseconds 250
    }
    if (-not $done) { "rank $n : NO FIGHT ($(St 1) / $(St 2))" }
    Stop-All @($l1, $l2, $srv)
}
Remove-Item Env:T6_PLATE_RANK
