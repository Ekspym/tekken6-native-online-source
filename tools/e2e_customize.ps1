# End-to-end test of the launcher's "Customize look" (2026-10-15): local lobby server, launcher --instance 2 on the
# profile page presses Customize look once its parked game stands ready; the game's part of the player is scripted
# (T6_CUSTOMIZE_TEST_SEQ: 1P model -> Purchase -> Head -> buy the first item -> back out of Customize). Pictures of the
# Customize game every 2 s in runs\e2e_customize\; at the end the launcher's custom_looks.
#   .\tools\e2e_customize.ps1 [-Char 16] [-Seconds 300]
param([int]$Char = 16, [int]$Seconds = 300, [string]$Seq = '0:120,1000:3,0:90,1000:3,0:90,1000:3,0:120,1000:3,0:120,1000:3,0:90,8:3,0:30,1000:3,0:180,1000:3,0:120,2000:3,0:90,2000:3,0:90,2000:3,0:90,2000:3,0:90,2000:3,0:90,2000:3,0:90,2000:3,0:90')
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public class ShE { public delegate bool P(IntPtr h, IntPtr l);
[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
[DllImport("user32.dll")] public static extern bool EnumWindows(P p, IntPtr l);
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
[DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
[DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint f);
[StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
public static IntPtr Find(uint pid) { IntPtr f = IntPtr.Zero; EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p); RECT r; GetWindowRect(h, out r); if (p == pid && IsWindowVisible(h) && r.R - r.L > 300) { f = h; return false; } return true; }, IntPtr.Zero); return f; } }
"@
[ShE]::SetProcessDPIAware() | Out-Null
. "$PSScriptRoot\t6_env.ps1"; $py = "$root\toolchain\python-embed\python.exe"
$exe = "$root\launcher\build\Tekken6 Native Online.exe"; $out = "$root\runs\e2e_customize"
New-Item -ItemType Directory -Force $out | Out-Null; Remove-Item "$out\shot_*.png" -ErrorAction SilentlyContinue
$ini = "$root\launcher_native_2.ini"; Copy-Item $ini "$out\launcher_native_2.ini.bak" -Force
$db = "$out\t6_e2e_customize.db"; Remove-Item $db -ErrorAction SilentlyContinue
$srv = Start-Process $py -ArgumentList '-I', "$root\server\t6_lobby_server.py", '--tcp', '7790', '--udp', '7791', '--db', $db, '--guests', '--allow-same-ip' -WindowStyle Hidden -PassThru
Start-Sleep 2
$env:T6_LOBBY_SERVER = '127.0.0.1:7790'; $env:T6_LAUNCHER_NOFOCUS = '1'; $env:T6_TEST_CHAR = "$Char"; $env:T6_CUSTOMIZE_TEST_SEQ = $Seq
$l = Start-Process $exe -ArgumentList '--instance', '2', '--page', 'profile', '--start-customize' -WorkingDirectory $root -PassThru
$guarded = @{}; $t0 = Get-Date; $n = 0; $next = 0; $seen = $false
while (((Get-Date) - $t0).TotalSeconds -lt $Seconds) {
    foreach ($g in Get-CimInstance Win32_Process -Filter "Name='tekken6.exe'" | Where-Object { $_.CommandLine -match 'customize_2|online_2' }) {
        if (-not $guarded[$g.ProcessId]) {
            $guarded[$g.ProcessId] = $true
            Start-Process powershell -WindowStyle Hidden -ArgumentList '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "$root\tools\window_guard.ps1", '-ProcessId', $g.ProcessId, '-X', 0, '-Y', 0, '-Title', 'tekken' | Out-Null
            "{0,4:n0} s: game {1} ({2})" -f ((Get-Date) - $t0).TotalSeconds, $g.ProcessId, $(if ($g.CommandLine -match 'customize_2') { 'customize' } else { 'parked online' })
        }
    }
    $c = Get-CimInstance Win32_Process -Filter "Name='tekken6.exe'" | Where-Object { $_.CommandLine -match 'customize_2' } | Select-Object -First 1
    if ($c) { $seen = $true } elseif ($seen) { "{0,4:n0} s: the Customize game ended" -f ((Get-Date) - $t0).TotalSeconds; break }
    if ($c -and ((Get-Date) - $t0).TotalSeconds -ge $next) {
        $next = ((Get-Date) - $t0).TotalSeconds + 2
        $h = [ShE]::Find([uint32]$c.ProcessId)
        if ($h -ne [IntPtr]::Zero) {
            $r = New-Object ShE+RECT; [ShE]::GetWindowRect($h, [ref]$r) | Out-Null
            $bmp = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T); $gr = [System.Drawing.Graphics]::FromImage($bmp)
            $hdc = $gr.GetHdc(); [ShE]::PrintWindow($h, $hdc, 2) | Out-Null; $gr.ReleaseHdc($hdc); $gr.Dispose()
            $o = New-Object System.Drawing.Bitmap 640, ([int](640 * $bmp.Height / $bmp.Width)); $g2 = [System.Drawing.Graphics]::FromImage($o); $g2.DrawImage($bmp, 0, 0, $o.Width, $o.Height); $g2.Dispose()
            $o.Save(("$out\shot_{0:00}.png" -f $n)); $o.Dispose(); $bmp.Dispose(); $n++
        }
    }
    Start-Sleep -Milliseconds 500
}
Start-Sleep 3
"custom_looks: " + ((Get-Content $ini | Select-String 'custom_looks') -join ' ')
Get-Content "$root\runs\customize_2\custom_out.txt" -ErrorAction SilentlyContinue
if (-not $l.HasExited) { Stop-Process -Id $l.Id -Force }
Get-CimInstance Win32_Process -Filter "Name='tekken6.exe'" | Where-Object { $_.CommandLine -match 'customize_2|online_2' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }
Stop-Process -Id $srv.Id -Force
Copy-Item "$out\launcher_native_2.ini.bak" $ini -Force
"shots $n"
