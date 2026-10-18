# Keeps the game window of a test instance visible on screen 1 but never active (user: on screen 1, never in the
# foreground; it must stay visible). Runs hidden next to the instance until it exits: places the window, makes it non-activating with a taskbar button and whenever
# it gets the foreground anyway, gives the foreground back to the previous window.
#   started by online_start.ps1: window_guard.ps1 -ProcessId <pid> -X <x> -Y <y>
param([int]$ProcessId, [int]$X, [int]$Y, [int]$W = 960, [int]$H = 540, [string]$Title = 'TEKKEN')

Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices;
public class WinGuard { public delegate bool P(IntPtr h, IntPtr l);
[DllImport("user32.dll")] public static extern bool EnumWindows(P p, IntPtr l);
[DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
[DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
[DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr a, int x, int y, int cx, int cy, uint f);
[DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
[DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
[DllImport("user32.dll", EntryPoint="GetWindowLongPtrW")] public static extern IntPtr GetLong(IntPtr h, int i);
[DllImport("user32.dll", EntryPoint="SetWindowLongPtrW")] public static extern IntPtr SetLong(IntPtr h, int i, IntPtr v);
public static uint Pid(IntPtr h){ uint p; GetWindowThreadProcessId(h, out p); return p; }
public static IntPtr Find(uint pid, string part){ IntPtr r=IntPtr.Zero; EnumWindows((h,l)=>{ if(Pid(h)==pid && IsWindowVisible(h)){ var s=new StringBuilder(512); GetWindowText(h,s,512); if(s.ToString().IndexOf(part, StringComparison.OrdinalIgnoreCase)>=0){ r=h; return false; } } return true; }, IntPtr.Zero); return r; }
// Foreground back to a window of another process (Alt press lifts the foreground lock of a background process)
public static void Give(IntPtr h){ keybd_event(0x12,0,0,UIntPtr.Zero); SetForegroundWindow(h); keybd_event(0x12,0,2,UIntPtr.Zero); } }
"@

$HWND_BOTTOM = [IntPtr]1
$SWP_NOACTIVATE = 0x0010; $SWP_SHOWWINDOW = 0x0040; $SWP_NOMOVE = 0x0002; $SWP_NOSIZE = 0x0001
$pid32 = [uint32]$ProcessId
$prev = [IntPtr]::Zero      # last foreground window of another process
$game = [IntPtr]::Zero
while (Get-Process -Id $ProcessId -ErrorAction SilentlyContinue) {
    $fg = [WinGuard]::GetForegroundWindow()
    if ($fg -ne [IntPtr]::Zero -and [WinGuard]::Pid($fg) -ne $pid32) { $prev = $fg }
    if ($game -eq [IntPtr]::Zero) {
        $game = [WinGuard]::Find($pid32, $Title)
        if ($game -ne [IntPtr]::Zero) {
            $ex = [WinGuard]::GetLong($game, -20).ToInt64() -bor 0x08040000   # WS_EX_NOACTIVATE + WS_EX_APPWINDOW (taskbar button)
            [WinGuard]::SetLong($game, -20, [IntPtr]$ex) | Out-Null
            [WinGuard]::SetWindowPos($game, [IntPtr]::Zero, $X, $Y, $W, $H, $SWP_NOACTIVATE -bor $SWP_SHOWWINDOW) | Out-Null
        }
    }
    # Any window of the instance in the foreground (game window, dialogs): foreground back to the previous window (the game stays visible)
    if ($fg -ne [IntPtr]::Zero -and [WinGuard]::Pid($fg) -eq $pid32) {
        if ($prev -ne [IntPtr]::Zero) { [WinGuard]::Give($prev) }
    }
    Start-Sleep -Milliseconds 100
}
