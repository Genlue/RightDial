param(
  [string]$WindowClass = "RightDialSettings",
  [string]$ChildClass = "SysTabControl32",
  [double]$Fx = 0.115,
  [double]$Fy = 0.05
)
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public class WinClick {
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindowW(string cls, string win);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindowExW(IntPtr parent, IntPtr after, string cls, string win);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extra);
  public struct RECT { public int L, T, R, B; }
}
"@
$h = (Get-Process RightDial -ErrorAction SilentlyContinue |
      Where-Object { $_.MainWindowHandle -ne 0 } |
      Select-Object -First 1).MainWindowHandle
$h = [IntPtr]$h
if ($h -eq [IntPtr]::Zero) { Write-Host "window not found"; exit 1 }
$target = $h
if ($ChildClass -ne "") {
  $c = [WinClick]::FindWindowExW($h, [IntPtr]::Zero, $ChildClass, $null)
  if ($c -ne [IntPtr]::Zero) { $target = $c }
}
$r = New-Object WinClick+RECT
[WinClick]::GetWindowRect($target, [ref]$r) | Out-Null
$x = [int]($r.L + ($r.R - $r.L) * $Fx)
$y = [int]($r.T + ($r.B - $r.T) * $Fy)
Write-Host "click at $x,$y (rect $($r.L),$($r.T)-$($r.R),$($r.B))"
[WinClick]::SetCursorPos($x, $y) | Out-Null
Start-Sleep -Milliseconds 150
[WinClick]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)  # LEFTDOWN
Start-Sleep -Milliseconds 60
[WinClick]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)  # LEFTUP
Write-Host "clicked"
