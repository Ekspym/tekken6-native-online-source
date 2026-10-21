# Screenshots of the stages by id: for every id a local match through the launchers (server with T6_STAGES=<id>, one round of
# 20 s), a screenshot of game 1 a few seconds into the fight -> runs\stage_<id>.png, then everything is closed.
#   .\tools\stage_map.ps1 -Ids 0,1,2
param([int[]]$Ids = @(0, 1, 2))
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices;
public class WinShot2 { public delegate bool P(IntPtr h, IntPtr l);
[DllImport("user32.dll")] public static extern bool EnumWindows(P p, IntPtr l);
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
[DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
[DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint f);
public struct RECT { public int L, T, R, B; }
public static IntPtr Find(uint pid){ IntPtr r=IntPtr.Zero; int best=0; EnumWindows((h,l)=>{ uint p; GetWindowThreadProcessId(h,out p); if(p==pid && IsWindowVisible(h)){ RECT c; GetWindowRect(h,out c); int a=(c.R-c.L)*(c.B-c.T); if(a>best){best=a;r=h;} } return true; }, IntPtr.Zero); return r; } }
"@ -ErrorAction SilentlyContinue
. "$PSScriptRoot\t6_env.ps1"
$exe = "$root\launcher\build\Tekken6 Native Online.exe"
function St($i) { try { (Get-Content "$root\runs\online_$i\status.txt" -ErrorAction Stop) -join ' | ' } catch { '' } }
function Shot($procId, $file) {
    $h = [WinShot2]::Find([uint32]$procId); if ($h -eq [IntPtr]::Zero) { return $false }
    $r = New-Object WinShot2+RECT; [WinShot2]::GetWindowRect($h, [ref]$r) | Out-Null
    $w = $r.R - $r.L; $hg = $r.B - $r.T; if ($w -lt 50) { return $false }
    $bmp = New-Object System.Drawing.Bitmap $w, $hg; $g = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc(); [WinShot2]::PrintWindow($h, $hdc, 2) | Out-Null; $g.ReleaseHdc($hdc); $g.Dispose()
    $out = New-Object System.Drawing.Bitmap 480, 270; $g2 = [System.Drawing.Graphics]::FromImage($out); $g2.DrawImage($bmp, 0, 0, 480, 270); $g2.Dispose()
    $out.Save($file, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose(); $out.Dispose(); return $true
}
$env:T6_ROUNDS = '1'; $env:T6_ROUND_TIME = '20'
foreach ($id in $Ids) {
    $env:T6_STAGES = "$id"
    $srv = Start-Process $py -ArgumentList "$root\server\t6_lobby_server.py", '--tcp', '7700', '--udp', '7701', '--db', "$env:TEMP\t6_e2e.db" -WindowStyle Hidden -PassThru
    Start-Sleep 2
    Remove-Item "$root\runs\online_1\status.txt", "$root\runs\online_2\status.txt" -ErrorAction SilentlyContinue
    $l1 = Start-Process $exe -ArgumentList '--auto', 'quick' -WorkingDirectory $root -PassThru
    Start-Sleep 2
    $l2 = Start-Process $exe -ArgumentList '--instance', '2', '--auto', 'quick' -WorkingDirectory $root -PassThru
    $guarded = @{}; $t0 = Get-Date; $res = 'no fight'
    while (((Get-Date) - $t0).TotalSeconds -lt 130) {
        foreach ($g in Get-Process tekken6 -ErrorAction SilentlyContinue) {
            if (-not $guarded[$g.Id]) { $guarded[$g.Id] = $true
                Start-Process powershell -WindowStyle Hidden -ArgumentList '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "$root\tools\window_guard.ps1", '-ProcessId', $g.Id, '-X', 0, '-Y', 0, '-Title', 'tekken' | Out-Null }
        }
        if ((St 1) -match 'state fight') {
            Start-Sleep 4
            $g1 = Get-CimInstance Win32_Process -Filter "Name='tekken6.exe'" | Where-Object { $_.CommandLine -match 'online_1' } | Select-Object -First 1
            $res = if ($g1 -and (Shot $g1.ProcessId "$root\runs\stage_$id.png")) { 'shot' } else { 'no window' }
            break
        }
        if (-not (Get-Process tekken6 -ErrorAction SilentlyContinue) -and ((Get-Date) - $t0).TotalSeconds -gt 25) { $res = 'games ended'; break }
        Start-Sleep -Milliseconds 500
    }
    "stage ${id}: $res after $([int]((Get-Date) - $t0).TotalSeconds) s; status: $(St 1)"
    Get-Process tekken6 -ErrorAction SilentlyContinue | Stop-Process -Force
    foreach ($p in $l1, $l2, $srv) { try { Stop-Process -Id $p.Id -Force -ErrorAction Stop } catch {} }
    Start-Sleep 3
}
