param([string]$Mode = "probe")
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class RD3 {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId2(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, [MarshalAs(UnmanagedType.LPWStr)] StringBuilder s, int n);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindowExW(IntPtr parent, IntPtr after, string cls, string win);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extra);
  [DllImport("user32.dll")] public static extern bool IsHungAppWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, uint msg, IntPtr wp, IntPtr lp);
  [DllImport("user32.dll")] public static extern IntPtr GetCapture();
  [StructLayout(LayoutKind.Sequential)]
  public struct GUITHREADINFO {
    public int cbSize; public uint flags; public IntPtr hwndActive; public IntPtr hwndFocus;
    public IntPtr hwndCapture; public IntPtr hwndMenuOwner; public IntPtr hwndMoveSize;
    public IntPtr hwndCaret; public RECT rcCaret;
  }
  [DllImport("user32.dll")] public static extern bool GetGUIThreadInfo(uint idThread, ref GUITHREADINFO info);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId3(IntPtr h, out uint pid);
  public struct RECT { public int L, T, R, B; }
}
"@
function Get-MainHwnd {
  $target = (Get-Process RightDial -ErrorAction SilentlyContinue | Select-Object -First 1).Id
  if (-not $target) { return [IntPtr]::Zero }
  $script:main = [IntPtr]::Zero
  $cb = {
    param($h, $l)
    $pid2 = 0
    [RD3]::GetWindowThreadProcessId($h, [ref]$pid2) | Out-Null
    if ($pid2 -eq $target) {
      $c = New-Object System.Text.StringBuilder 128
      [RD3]::GetClassNameW($h, $c, 128) | Out-Null
      if ($c.ToString() -eq "RightDialSettings") { $script:main = $h; return $false }
    }
    return $true
  }
  [RD3]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
  return $script:main
}
function Show-Capture {
  param([IntPtr]$h, [string]$label)
  $tid = [RD3]::GetWindowThreadProcessId($h, [ref]([uint32]0))
  $ti = New-Object RD3+GUITHREADINFO
  $ti.cbSize = [System.Runtime.InteropServices.Marshal]::SizeOf($ti)
  [RD3]::GetGUIThreadInfo($tid, [ref]$ti) | Out-Null
  Write-Host ($label + " capture: " + $ti.hwndCapture)
}
$main = Get-MainHwnd
if ($main -eq [IntPtr]::Zero) { Write-Host "settings window not found"; exit 1 }
Show-Capture $main "BEFORE"
$p1 = [RD3]::FindWindowExW($main, [IntPtr]::Zero, "Static", $null)
$lb1 = [RD3]::FindWindowExW($p1, [IntPtr]::Zero, "ListBox", $null)
$lb2 = [RD3]::FindWindowExW($p1, $lb1, "ListBox", $null)
if ($lb2 -eq [IntPtr]::Zero) { Write-Host "slots list not found"; exit 1 }
$r = New-Object RD3+RECT
[RD3]::GetWindowRect($lb2, [ref]$r) | Out-Null
$ih = [RD3]::SendMessageW($lb2, 0x01A1, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32()
if ($ih -le 0) { $ih = 18 }
$cx = [int](($r.L + $r.R) / 2)
# realistic drag: down on item 0, ~25 small moves to item 4, some outside the list, up
$y0 = $r.T + [int]($ih * 0.5)
$y1 = $r.T + [int]($ih * 4.5)
[RD3]::SetCursorPos($cx, $y0) | Out-Null
Start-Sleep -Milliseconds 200
[RD3]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
Start-Sleep -Milliseconds 100
for ($i = 1; $i -le 25; $i++) {
  $yy = $y0 + [int](($y1 - $y0) * $i / 25)
  $xx = $cx + (Get-Random -Minimum -60 -Maximum 200)
  [RD3]::SetCursorPos($xx, $yy) | Out-Null
  Start-Sleep -Milliseconds 25
}
Start-Sleep -Milliseconds 120
[RD3]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
Start-Sleep -Milliseconds 500
Show-Capture $main "AFTER "
$hung = [RD3]::IsHungAppWindow($main)
Write-Host "hung: $hung"
