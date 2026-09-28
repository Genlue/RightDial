Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class RD7 {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr h, EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, [MarshalAs(UnmanagedType.LPWStr)] StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern int GetDlgCtrlID(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extra);
  [DllImport("user32.dll")] public static extern bool InvalidateRect(IntPtr h, IntPtr rc, bool erase);
  [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, uint msg, IntPtr wp, IntPtr lp);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
  public struct RECT { public int L, T, R, B; }
}
"@
function Find-Settings {
  $target = (Get-Process RightDial -ErrorAction SilentlyContinue | Select-Object -First 1).Id
  if (-not $target) { return [IntPtr]::Zero }
  $script:main = [IntPtr]::Zero
  $cb = {
    param($h, $l)
    $pid2 = 0
    [RD7]::GetWindowThreadProcessId($h, [ref]$pid2) | Out-Null
    if ($pid2 -eq $target) {
      $c = New-Object System.Text.StringBuilder 128
      [RD7]::GetClassNameW($h, $c, 128) | Out-Null
      if ($c.ToString() -eq "RightDialSettings") { $script:main = $h; return $false }
    }
    return $true
  }
  [RD7]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
  return $script:main
}
$main = Find-Settings
if ($main -eq [IntPtr]::Zero) { Write-Host "no settings window"; exit 1 }
[RD7]::BringWindowToTop($main) | Out-Null
[RD7]::SetForegroundWindow($main) | Out-Null
Start-Sleep -Milliseconds 400
# locate view (2029) and tab (2000)
$script:view = [IntPtr]::Zero; $script:tab = [IntPtr]::Zero
$ccb = {
  param($h, $l)
  $id = [RD7]::GetDlgCtrlID($h)
  if ($id -eq 2029) { $script:view = $h }
  if ($id -eq 2000) { $script:tab = $h }
  return $true
}
[RD7]::EnumChildWindows($main, $ccb, [IntPtr]::Zero) | Out-Null
if ($script:view -eq [IntPtr]::Zero) { Write-Host "view not found"; exit 1 }
$dpi = [RD7]::GetDpiForWindow($script:view)
$f = $dpi / 96.0
Write-Host "dpi=$dpi factor=$f"
$vr = New-Object RD7+RECT
[RD7]::GetWindowRect($script:view, [ref]$vr) | Out-Null
# virtual -> physical
$px = [int]$vr.L; $py = [int]$vr.T   # input: virtual coords
$pw = [int]($vr.R - $vr.L); $ph = [int]($vr.B - $vr.T)
Write-Host "view virtual $($vr.L),$($vr.T) size $($vr.R-$vr.L)x$($vr.B-$vr.T) -> physical $px,$py ${pw}x$ph"
function Shot([string]$out, [int]$x, [int]$y, [int]$w, [int]$h) {
  $b = New-Object System.Drawing.Bitmap($w, $h)
  $g = [System.Drawing.Graphics]::FromImage($b)
  $g.CopyFromScreen($x, $y, 0, 0, $b.Size)
  $g.Dispose()
  $b.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
  $b.Dispose()
}
[RD7]::InvalidateRect($script:view, [IntPtr]::Zero, $true) | Out-Null
Start-Sleep -Milliseconds 400
Shot "bin\view_before.png" $px $py $pw $ph
# wheel geometry in view-physical space
$cx = [int]($pw / 2); $cy = [int]($ph / 2)
# fit: scale = 1.5; d = 300*1.5=450; avail = min(pw,ph)-12; if d>avail scale *= avail/450
$avail = [Math]::Min($pw, $ph) - 8
$sc = 1.0
$d = 300.0
if ($d -gt $avail) { $sc = $sc * $avail / $d }
$rO = 150.0 * $sc / 2.0
$rI = 150.0 * 0.18 * $sc / 2.0
$iconR = 0.62 * $rO
# sectorCount=4, rotationDeg=45 -> sector0 east, sector2 west
$ang0 = 0.0; $ang2 = 180.0
$s0x = [int]($cx + $iconR * [Math]::Cos(0.0))
$s0y = [int]($cy + $iconR * [Math]::Sin(0.0))
$s2x = [int]($cx + $iconR * [Math]::Cos([Math]::PI))
$s2y = [int]($cy + $iconR * [Math]::Sin([Math]::PI))
Write-Host "drag from view-local ($s0x,$s0y) to ($s2x,$s2y)"
[RD7]::SetCursorPos($px + $s0x, $py + $s0y) | Out-Null
Start-Sleep -Milliseconds 250
[RD7]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
Start-Sleep -Milliseconds 120
for ($i = 1; $i -le 10; $i++) {
  $xx = $s0x + [int](($s2x - $s0x) * $i / 10)
  $yy = $s0y + [int](($s2y - $s0y) * $i / 10)
  [RD7]::SetCursorPos($px + $xx, $py + $yy) | Out-Null
  Start-Sleep -Milliseconds 40
}
Start-Sleep -Milliseconds 200
[RD7]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
Start-Sleep -Milliseconds 600
Shot "bin\view_after.png" $px $py $pw $ph
# tab responsiveness: TCM_GETCURSEL before/after a real click on tab 2
$sel1 = [RD7]::SendMessageW($script:tab, 0x130B, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32()
$tr = New-Object RD7+RECT
[RD7]::GetWindowRect($script:tab, [ref]$tr) | Out-Null
$tx = [int]($tr.L + ($tr.R - $tr.L) * 0.40)
$ty = [int]($tr.T + ($tr.B - $tr.T) * 0.5)
[RD7]::SetCursorPos($tx, $ty) | Out-Null
Start-Sleep -Milliseconds 200
[RD7]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
Start-Sleep -Milliseconds 60
[RD7]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
Start-Sleep -Milliseconds 500
$sel2 = [RD7]::SendMessageW($script:tab, 0x130B, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32()
Write-Host "tab selection before=$sel1 after=$sel2 (changed: $($sel1 -ne $sel2))"
