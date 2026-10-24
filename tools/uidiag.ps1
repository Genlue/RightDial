param()
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class RDDiag {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr h, EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, [MarshalAs(UnmanagedType.LPWStr)] StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern IntPtr SendMessageTimeoutW(IntPtr h, uint m, IntPtr w, IntPtr l, uint flags, uint timeout, out IntPtr result);
  public struct RECT { public int L, T, R, B; }
}
"@
$procs = Get-Process RightDial -ErrorAction SilentlyContinue
Write-Host ("instances: " + @($procs).Count)
foreach ($p in @($procs)) { Write-Host ("pid " + $p.Id + " responding: " + $p.Responding) }
$target = (@($procs) | Select-Object -First 1).Id
$script:settings = [IntPtr]::Zero
$cb1 = {
  param($h, $l)
  $pid2 = [uint32]0
  [RDDiag]::GetWindowThreadProcessId($h, [ref]$pid2) | Out-Null
  if ($pid2 -eq $target) {
    $c = New-Object System.Text.StringBuilder 128
    [RDDiag]::GetClassNameW($h, $c, 128) | Out-Null
    if ($c.ToString() -eq "RightDialSettings") { $script:settings = $h; return $false }
  }
  return $true
}
[RDDiag]::EnumWindows($cb1, [IntPtr]::Zero) | Out-Null
Write-Host ("settings hwnd: " + $script:settings)
if ($script:settings -ne [IntPtr]::Zero) {
  $res = [IntPtr]::Zero
  $ok = [RDDiag]::SendMessageTimeoutW($script:settings, 0x0000, [IntPtr]::Zero, [IntPtr]::Zero, 2, 1500, [ref]$res)
  Write-Host ("WM_NULL respond: " + $ok)
  $cb2 = {
    param($h, $l)
    $c = New-Object System.Text.StringBuilder 128
    [RDDiag]::GetClassNameW($h, $c, 128) | Out-Null
    $r = New-Object RDDiag+RECT
    [RDDiag]::GetWindowRect($h, [ref]$r) | Out-Null
    Write-Host ("  child: " + $c.ToString() + " hwnd=" + $h + " visible=" + [RDDiag]::IsWindowVisible($h) + " rect=" + $r.L + "," + $r.T + "," + $r.R + "," + $r.B)
    return $true
  }
  [RDDiag]::EnumChildWindows($script:settings, $cb2, [IntPtr]::Zero) | Out-Null
}
