param([string]$Out = "bin\final_tab2.png", [int]$Tab = 2)
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class RD8 {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, [MarshalAs(UnmanagedType.LPWStr)] StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
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
  [RD8]::GetWindowThreadProcessId($h, [ref]$pid2) | Out-Null
  if ($pid2 -eq $target) {
    $c = New-Object System.Text.StringBuilder 128
    [RD8]::GetClassNameW($h, $c, 128) | Out-Null
    if ($c.ToString() -eq "RightDialSettings") { $script:main = $h; return $false }
  }
  return $true
}
[RD8]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
if ($script:main -eq [IntPtr]::Zero) { Write-Host "no settings window"; exit 1 }
[RD8]::SetForegroundWindow($script:main) | Out-Null
Start-Sleep -Milliseconds 300
$r = New-Object RD8+RECT
[RD8]::GetWindowRect($script:main, [ref]$r) | Out-Null
Write-Host "rect $($r.L),$($r.T) size $($r.R-$r.L)x$($r.B-$r.T)"
if ($Tab -ge 2) {
  # tab strip header: first tab ~x+24..+170, second ~+180..+330 (virtual px)
  $tx = $r.L + 250
  $ty = $r.T + 58
  [RD8]::SetCursorPos($tx, $ty) | Out-Null
  Start-Sleep -Milliseconds 200
  [RD8]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
  Start-Sleep -Milliseconds 60
  [RD8]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
  Start-Sleep -Milliseconds 700
  Write-Host "tab $Tab clicked at $tx,$ty"
}
# physical capture: window virtual size * 1.25 (dpi 120)
$f = 1.25
$pw = [int](($r.R - $r.L) * $f)
$ph = [int](($r.B - $r.T) * $f)
$px = [int]($r.L * $f)
$py = [int]($r.T * $f)
$b = New-Object System.Drawing.Bitmap($pw, $ph)
$g = [System.Drawing.Graphics]::FromImage($b)
$g.CopyFromScreen($px, $py, 0, 0, $b.Size)
$g.Dispose()
$b.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$b.Dispose()
Write-Host "saved $Out ($pw x $ph)"
