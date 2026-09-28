param([string]$Mode = "click")
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class RD4 {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, [MarshalAs(UnmanagedType.LPWStr)] StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extra);
  public struct RECT { public int L, T, R, B; }
}
"@
$target = (Get-Process RightDial -ErrorAction SilentlyContinue | Select-Object -First 1).Id
if (-not $target) { Write-Host "not running"; exit 1 }
$script:main = [IntPtr]::Zero
$cb = {
  param($h, $l)
  $pid2 = 0
  [RD4]::GetWindowThreadProcessId($h, [ref]$pid2) | Out-Null
  if ($pid2 -eq $target) {
    $c = New-Object System.Text.StringBuilder 128
    [RD4]::GetClassNameW($h, $c, 128) | Out-Null
    if ($c.ToString() -eq "RightDialSettings") { $script:main = $h; return $false }
  }
  return $true
}
[RD4]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
if ($script:main -eq [IntPtr]::Zero) { Write-Host "settings window not found"; exit 1 }
$r = New-Object RD4+RECT
[RD4]::GetWindowRect($script:main, [ref]$r) | Out-Null
# tab strip: second tab sits around 30-45% of window width, ~6% height
$x = [int]($r.L + ($r.R - $r.L) * 0.40)
$y = [int]($r.T + ($r.B - $r.T) * 0.062)
[RD4]::SetCursorPos($x, $y) | Out-Null
Start-Sleep -Milliseconds 200
[RD4]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
Start-Sleep -Milliseconds 60
[RD4]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
Write-Host "clicked tab at $x,$y"
Start-Sleep -Milliseconds 600
