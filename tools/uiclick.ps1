# Click a sidebar nav item by posting messages straight to the nav child window
# (immune to DPI coordinate drift), then capture the settings window.
# Usage: powershell -File tools\uiclick.ps1 -Out bin\ui_dark_p2.png -Nav 2
param([string]$Out = "bin\ui_nav.png", [int]$Nav = 2)
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class RDClick {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr h, EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, [MarshalAs(UnmanagedType.LPWStr)] StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
  [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
  [DllImport("shcore.dll")] public static extern int SetProcessDpiAwareness(int value);
  public struct RECT { public int L, T, R, B; }
}
"@
try { [RDClick]::SetProcessDpiAwareness(2) | Out-Null } catch { }

$target = (Get-Process RightDial -ErrorAction SilentlyContinue | Select-Object -First 1).Id
if (-not $target) { Write-Host "not running"; exit 1 }
$script:settings = [IntPtr]::Zero
$cb = {
  param($h, $l)
  $pid2 = [uint32]0
  [RDClick]::GetWindowThreadProcessId($h, [ref]$pid2) | Out-Null
  if ($pid2 -eq $target) {
    $c = New-Object System.Text.StringBuilder 128
    [RDClick]::GetClassNameW($h, $c, 128) | Out-Null
    if ($c.ToString() -eq "RightDialSettings") { $script:settings = $h; return $false }
  }
  return $true
}
[RDClick]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
if ($script:settings -eq [IntPtr]::Zero) { Write-Host "no settings window"; exit 1 }

$script:navHwnd = [IntPtr]::Zero
$cb2 = {
  param($h, $l)
  $c = New-Object System.Text.StringBuilder 128
  [RDClick]::GetClassNameW($h, $c, 128) | Out-Null
  if ($c.ToString() -eq "RightDialNav") { $script:navHwnd = $h; return $false }
  return $true
}
[RDClick]::EnumChildWindows($script:settings, $cb2, [IntPtr]::Zero) | Out-Null
if ($script:navHwnd -eq [IntPtr]::Zero) { Write-Host "no nav window"; exit 1 }

# nav item N occupies logical y = 78 + (N-1)*40 .. +34; convert with the
# window DPI (nav client coords are physical for this DPI-aware process)
$dpi = [RDClick]::GetDpiForWindow($script:navHwnd)
$scale = $dpi / 96.0
$cy = [int](((78 + ($Nav - 1) * 40) + 17) * $scale)
$cx = [int](100 * $scale)
$lp = (($cy -shl 16) -bor $cx)
[RDClick]::SendMessageW($script:navHwnd, 0x0201, [IntPtr]1, [IntPtr]$lp) | Out-Null   # WM_LBUTTONDOWN
[RDClick]::SendMessageW($script:navHwnd, 0x0202, [IntPtr]0, [IntPtr]$lp) | Out-Null   # WM_LBUTTONUP
Write-Host "posted click to nav item $Nav at client $cx,$cy (dpi $dpi)"

if ([RDClick]::IsIconic($script:settings)) { [RDClick]::ShowWindow($script:settings, 9) | Out-Null }  # SW_RESTORE
[RDClick]::SetForegroundWindow($script:settings) | Out-Null
Start-Sleep -Milliseconds 700
$r = New-Object RDClick+RECT
[RDClick]::GetWindowRect($script:settings, [ref]$r) | Out-Null
$b = New-Object System.Drawing.Bitmap(($r.R - $r.L), ($r.B - $r.T))
$g = [System.Drawing.Graphics]::FromImage($b)
$g.CopyFromScreen($r.L, $r.T, 0, 0, $b.Size)
$g.Dispose()
$b.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$b.Dispose()
Write-Host "saved $Out ($($r.R-$r.L)x$($r.B-$r.T) at $($r.L),$($r.T))"
