# Which character each code of the player setup hook (T6_CHAR_P1/P2) gives: one practice instance per odd code, the
# name plate of the CPU side (top right) cut out of a screenshot -> runs\char_codes\<code>.png + sheet.png
#   tools\char_codes.ps1 [-Codes 1,3,5] [-Parallel 4] [-At 24]
param([int[]]$Codes = (0..43 | ForEach-Object { 2 * $_ + 1 }), [int]$Parallel = 4, [int]$At = 24)
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public class WinShotC { public delegate bool P(IntPtr h, IntPtr l);
[DllImport("user32.dll")] public static extern bool EnumWindows(P p, IntPtr l);
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
[DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
[DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint f);
[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
public struct RECT { public int L, T, R, B; }
public static IntPtr Find(uint pid){ IntPtr r=IntPtr.Zero; int best=0; EnumWindows((h,l)=>{ uint p; GetWindowThreadProcessId(h,out p); if(p==pid && IsWindowVisible(h)){ RECT c; GetWindowRect(h,out c); int a=(c.R-c.L)*(c.B-c.T); if(a>best){best=a;r=h;} } return true; }, IntPtr.Zero); return r; } }
"@ -ErrorAction SilentlyContinue
try { [WinShotC]::SetProcessDPIAware() | Out-Null } catch {}
. "$PSScriptRoot\t6_env.ps1"
$out = "$root\runs\char_codes"
New-Item -ItemType Directory -Force $out | Out-Null
for ($i = 0; $i -lt $Codes.Count; $i += $Parallel) {
    $batch = $Codes[$i..([Math]::Min($i + $Parallel, $Codes.Count) - 1)]
    $procs = @{}
    foreach ($c in $batch) {
        $run = "$root\runs\cc_$c"
        Remove-Item -Recurse -Force $run -ErrorAction SilentlyContinue
        New-Item -ItemType Directory -Force $run | Out-Null
        foreach ($v in 'T6_NET', 'T6_NET_HOLD', 'T6_REC', 'T6_HASH', 'T6_SNAPTEST', 'T6_AUTO_CONFIRM', 'T6_NET_STATUS') { Remove-Item "Env:$v" -ErrorAction SilentlyContinue }
        $env:T6_PRACTICE = '1'; $env:T6_PLAY = "$root\tools\menu_vs.bin"; $env:T6_PLAY_UNTIL = '1630'
        $env:T6_FIX_SEED = '1'; $env:T6_VIRTUAL_TB = '1'; $env:T6_CHAR_P1 = "$c"; $env:T6_CHAR_P2 = "$c"; $env:REX_GPU_VSYNC = '1'
        $a = @("--game_data_root=$root\game", "--user_data_root=$run\user", "--cache_root=$run\cache", "--log_file=$run\tekken6.log",
               '--fullscreen=false', '--async_shader_compilation=true', '--render_target_path_d3d12=rov', '--input_backend=mmjoystick')
        Copy-Item -Recurse "$root\user" "$run\user"
        $procs[$c] = Start-Process -FilePath "$root\build\tekken6.exe" -ArgumentList $a -WorkingDirectory "$root\build" -PassThru
    }
    $t0 = Get-Date
    while (((Get-Date) - $t0).TotalSeconds -lt $At) { Start-Sleep -Milliseconds 500 }
    foreach ($c in $batch) {
        $p = $procs[$c]
        if (-not $p.HasExited) {
            $h = [WinShotC]::Find([uint32]$p.Id)
            if ($h -ne [IntPtr]::Zero) {
                $r = New-Object WinShotC+RECT; [WinShotC]::GetWindowRect($h, [ref]$r) | Out-Null
                $w = $r.R - $r.L; $hg = $r.B - $r.T
                $bmp = New-Object System.Drawing.Bitmap $w, $hg
                $g = [System.Drawing.Graphics]::FromImage($bmp); $hdc = $g.GetHdc()
                [WinShotC]::PrintWindow($h, $hdc, 2) | Out-Null
                $g.ReleaseHdc($hdc); $g.Dispose()
                # CPU name plate: top right (client 1280x720 below a ~38 px title bar)
                $crop = $bmp.Clone((New-Object System.Drawing.Rectangle ([int]($w * 0.64)), ([int]($hg * 0.175)), ([int]($w * 0.24)), ([int]($hg * 0.045))), $bmp.PixelFormat)
                $crop.Save("$out\$c.png"); $crop.Dispose(); $bmp.Dispose()
            }
            Stop-Process -Id $p.Id -Force
        }
    }
}
# sheet: code + name plate per row
$files = $Codes | Where-Object { Test-Path "$out\$_.png" }
$sheet = New-Object System.Drawing.Bitmap 420, (34 * $files.Count)
$g = [System.Drawing.Graphics]::FromImage($sheet); $g.Clear([System.Drawing.Color]::Black)
$font = New-Object System.Drawing.Font 'Arial', 14
$y = 0
foreach ($c in $files) {
    $img = [System.Drawing.Image]::FromFile("$out\$c.png")
    $g.DrawString("$c", $font, [System.Drawing.Brushes]::Yellow, 4, $y + 6)
    $g.DrawImage($img, 60, $y)
    $img.Dispose(); $y += 34
}
$g.Dispose(); $sheet.Save("$out\sheet.png"); $sheet.Dispose()
"sheet: $out\sheet.png ($($files.Count) codes)"
