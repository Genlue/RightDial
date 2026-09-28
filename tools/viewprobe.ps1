Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class RD6 {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr h, EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, [MarshalAs(UnmanagedType.LPWStr)] StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern int GetDlgCtrlID(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, uint msg, IntPtr wp, IntPtr lp);
  public struct RECT { public int L, T, R, B; }
}
"@
$target = (Get-Process RightDial -ErrorAction SilentlyContinue | Select-Object -First 1).Id
if (-not $target) { Write-Host "not running"; exit 1 }
$script:main = [IntPtr]::Zero
$cb = {
  param($h, $l)
  $pid2 = 0
  [RD6]::GetWindowThreadProcessId($h, [ref]$pid2) | Out-Null
  if ($pid2 -eq $target) {
    $c = New-Object System.Text.StringBuilder 128
    [RD6]::GetClassNameW($h, $c, 128) | Out-Null
    if ($c.ToString() -eq "RightDialSettings") { $script:main = $h; return $false }
  }
  return $true
}
[RD6]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
if ($script:main -eq [IntPtr]::Zero) { Write-Host "no settings window"; exit 1 }
# find child with ctrl id 2029 anywhere under main
$script:view = [IntPtr]::Zero
$ccb = {
  param($h, $l)
  if ([RD6]::GetDlgCtrlID($h) -eq 2029) { $script:view = $h; return $false }
  return $true
}
[RD6]::EnumChildWindows($script:main, $ccb, [IntPtr]::Zero) | Out-Null
if ($script:view -eq [IntPtr]::Zero) { Write-Host "view control NOT FOUND (id 2029)"; exit 1 }
$cn = New-Object System.Text.StringBuilder 64
[RD6]::GetClassNameW($script:view, $cn, 64) | Out-Null
$r = New-Object RD6+RECT
[RD6]::GetWindowRect($script:view, [ref]$r) | Out-Null
Write-Host "view found: class=$($cn) rect=$($r.L),$($r.T)-$($r.R),$($r.B)"
$cr = New-Object RD6+RECT
[RD6]::GetClientRect($script:view, [ref]$cr) | Out-Null
$w = $cr.R; $h = $cr.B
Write-Host "client ${w}x${h}"
$bmp = New-Object System.Drawing.Bitmap($w, $h)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
# WM_PRINTCLIENT (0x0313), PRF_CLIENT | PRF_ERASEBKGND
[RD6]::SendMessageW($script:view, 0x0313, [IntPtr]$hdc, [IntPtr](0x0002 -bor 0x0008)) | Out-Null
$g.ReleaseHdc($hdc)
$g.Dispose()
$bmp.Save("bin\viewprint.png", [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Host "saved bin/viewprint.png"
