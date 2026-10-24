# Capture the settings window (physical pixels, DPI-aware).
# Usage: powershell -File tools\uishot.ps1 -Out bin\ui_p1.png [-Nav 1..3]
#   -Nav clicks sidebar item N (1=槽位与动作 2=轮盘外观 3=行为与规则) before shooting.
param([string]$Out = "bin\ui_p1.png", [int]$Nav = 0)
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class RDShot {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, [MarshalAs(UnmanagedType.LPWStr)] StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint flags, int dx, int dy, uint data, UIntPtr extra);
  [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
  [DllImport("shcore.dll")] public static extern int SetProcessDpiAwareness(int value);
  public struct RECT { public int L, T, R, B; }
}
"@
try { [RDShot]::SetProcessDpiAwareness(2) | Out-Null } catch { }

$target = (Get-Process RightDial -ErrorAction SilentlyContinue | Select-Object -First 1).Id
if (-not $target) { Write-Host "not running"; exit 1 }
$script:main = [IntPtr]::Zero
$cb = {
  param($h, $l)
  $pid2 = [uint32]0
  [RDShot]::GetWindowThreadProcessId($h, [ref]$pid2) | Out-Null
  if ($pid2 -eq $target) {
    $c = New-Object System.Text.StringBuilder 128
    [RDShot]::GetClassNameW($h, $c, 128) | Out-Null
    if ($c.ToString() -eq "RightDialSettings") { $script:main = $h; return $false }
  }
  return $true
}
[RDShot]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
if ($script:main -eq [IntPtr]::Zero) { Write-Host "no settings window"; exit 1 }
[RDShot]::SetForegroundWindow($script:main) | Out-Null
Start-Sleep -Milliseconds 400
$r = New-Object RDShot+RECT
[RDShot]::GetWindowRect($script:main, [ref]$r) | Out-Null
Write-Host "rect $($r.L),$($r.T) size $($r.R-$r.L)x$($r.B-$r.T)"

if ($Nav -ge 1 -and $Nav -le 3) {
  # sidebar item N: logical center approx (100, 78 + (N-1)*40 + 17)
  $dpi = [RDShot]::GetDpiForWindow($script:main)
  $scale = $dpi / 96.0
  $cx = $r.L + [int](100 * $scale)
  $cy = $r.T + [int]((78 + ($Nav - 1) * 40 + 17) * $scale)
  [RDShot]::SetCursorPos($cx, $cy) | Out-Null
  Start-Sleep -Milliseconds 150
  [RDShot]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
  Start-Sleep -Milliseconds 60
  [RDShot]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
  Start-Sleep -Milliseconds 700
  Write-Host "nav $Nav clicked at $cx,$cy (dpi $dpi)"
}

$b = New-Object System.Drawing.Bitmap(($r.R - $r.L), ($r.B - $r.T))
$g = [System.Drawing.Graphics]::FromImage($b)
$g.CopyFromScreen($r.L, $r.T, 0, 0, $b.Size)
$g.Dispose()
$b.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$b.Dispose()
Write-Host "saved $Out"
