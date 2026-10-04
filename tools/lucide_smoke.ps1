# Lucide icon-library picker smoke test:
#  1. fresh RightDial + settings window
#  2. select slot 0, open the picker via the "图标库" button
#  3. screenshot picker, search "folder", screenshot
#  4. pick a grid cell, apply (IDOK), screenshot settings
#  5. verify config.json got builtin:lucide:<name>
param([string]$OutDir = "bin")
$ErrorActionPreference = "Continue"
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class RDS {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr h, EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, [MarshalAs(UnmanagedType.LPWStr)] StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, [MarshalAs(UnmanagedType.LPWStr)] StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern int GetDlgCtrlID(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extra);
  [DllImport("user32.dll")] public static extern IntPtr PostMessageW(IntPtr h, uint m, IntPtr wp, IntPtr lp);
  [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr wp, IntPtr lp);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr h);
  public struct RECT { public int L, T, R, B; }
}
"@

function Find-ChildByCtlId([IntPtr]$parent, [int]$ctlId) {
  $found = [IntPtr]::Zero
  $cb = {
    param($h, $l)
    if ([RDS]::GetDlgCtrlID($h) -eq $ctlId) { $script:found = $h; return $false }
    return $true
  }
  $script:found = [IntPtr]::Zero
  [RDS]::EnumChildWindows($parent, $cb, [IntPtr]::Zero) | Out-Null
  return $script:found
}

function Click-At([int]$x, [int]$y) {
  [RDS]::SetCursorPos($x, $y) | Out-Null
  Start-Sleep -Milliseconds 120
  [RDS]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
  Start-Sleep -Milliseconds 60
  [RDS]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
  Start-Sleep -Milliseconds 400
}

function Shot-Window([IntPtr]$h, [string]$out) {
  [RDS]::SetForegroundWindow($h) | Out-Null
  Start-Sleep -Milliseconds 350
  $r = New-Object RDS+RECT
  [RDS]::GetWindowRect($h, [ref]$r) | Out-Null
  $f = 1.25   # dpi 120: virtual -> physical, same as tools/shot.ps1
  $pw = [int](($r.R - $r.L) * $f); $ph = [int](($r.B - $r.T) * $f)
  $b = New-Object System.Drawing.Bitmap($pw, $ph)
  $g = [System.Drawing.Graphics]::FromImage($b)
  $g.CopyFromScreen([int]($r.L * $f), [int]($r.T * $f), 0, 0, $b.Size)
  $g.Dispose(); $b.Save($out, [System.Drawing.Imaging.ImageFormat]::Png); $b.Dispose()
  Write-Host "saved $out ($pw x $ph)"
}

# ---- 1. fresh instance + settings ----
taskkill /F /IM RightDial.exe 2>$null | Out-Null
Start-Sleep -Milliseconds 500
Start-Process -FilePath "bin\RightDial.exe" -ArgumentList "/settings"
$main = [IntPtr]::Zero
$pid2 = 0
for ($i = 0; $i -lt 50 -and $main -eq [IntPtr]::Zero; $i++) {
  Start-Sleep -Milliseconds 200
  $p = Get-Process RightDial -ErrorAction SilentlyContinue | Select-Object -First 1
  if (-not $p) { continue }
  $cb = {
    param($h, $l)
    $wpid = 0
    [RDS]::GetWindowThreadProcessId($h, [ref]$wpid) | Out-Null
    if ($wpid -eq $p.Id -and [RDS]::IsWindowVisible($h)) {
      $c = New-Object System.Text.StringBuilder 128
      [RDS]::GetClassNameW($h, $c, 128) | Out-Null
      if ($c.ToString() -eq "RightDialSettings") { $script:main = $h; return $false }
    }
    return $true
  }
  [RDS]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
}
if ($main -eq [IntPtr]::Zero) { Write-Host "FAIL: settings window not found"; exit 1 }
Write-Host "settings window found"
Start-Sleep -Milliseconds 800
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public class RSW { [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h); [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd); }
"@
if ([RSW]::IsIconic($main)) { Write-Host "window iconic - restoring"; [RSW]::ShowWindow($main, 9) | Out-Null; Start-Sleep -Milliseconds 600 }
$r0 = New-Object RDS+RECT
[RDS]::GetWindowRect($main, [ref]$r0) | Out-Null
Write-Host ("main rect {0},{1} - {2},{3}" -f $r0.L, $r0.T, $r0.R, $r0.B)

# ---- 2. select slot 0 in the slots listbox, then open the picker ----
$lb = Find-ChildByCtlId $main 2006        # IDC_P1_SLOTS
if ($lb -eq [IntPtr]::Zero) { Write-Host "FAIL: slots listbox not found"; exit 1 }
$lr = New-Object RDS+RECT
[RDS]::GetWindowRect($lb, [ref]$lr) | Out-Null
[RDS]::SetForegroundWindow($main) | Out-Null
Start-Sleep -Milliseconds 300
Click-At ($lr.L + 40) ($lr.T + 14)
$sel = [int][RDS]::SendMessageW($lb, 0x0188, [IntPtr]::Zero, [IntPtr]::Zero)   # LB_GETCURSEL
Write-Host "listbox selection after click: $sel"
$lib = Find-ChildByCtlId $main 2022       # IDC_P1_ICONLIB
if ($lib -eq [IntPtr]::Zero) { Write-Host "FAIL: icon-lib button not found"; exit 1 }
$br = New-Object RDS+RECT
[RDS]::GetWindowRect($lib, [ref]$br) | Out-Null
Write-Host ("lib button rect {0},{1} - {2},{3} enabled={4}" -f $br.L, $br.T, $br.R, $br.B, [RDS]::IsWindowEnabled($lib))
Click-At ([int](($br.L + $br.R) / 2)) ([int](($br.T + $br.B) / 2))
Start-Sleep -Milliseconds 600
$p = Get-Process RightDial -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $p) { Write-Host "PROCESS DIED after picker click (crash in dialog code?)"; exit 1 }
$dlgNow = [IntPtr]::Zero
$cb = {
  param($h, $l)
  $wpid = 0
  [RDS]::GetWindowThreadProcessId($h, [ref]$wpid) | Out-Null
  if ($wpid -eq $p.Id) {
    $c = New-Object System.Text.StringBuilder 128
    [RDS]::GetClassNameW($h, $c, 128) | Out-Null
    if ($c.ToString() -eq "#32770" -and [RDS]::IsWindowVisible($h)) { $script:dlgNow = $h; return $false }
  }
  return $true
}
[RDS]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
if ($dlgNow -eq [IntPtr]::Zero) {
  Write-Host "no dialog after physical click - retrying with BM_CLICK"
  [RDS]::SendMessageW($lib, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
  Start-Sleep -Milliseconds 800
  $p = Get-Process RightDial -ErrorAction SilentlyContinue | Select-Object -First 1
  if (-not $p) { Write-Host "PROCESS DIED after BM_CLICK (crash in dialog code?)"; exit 1 }
}
$all = @()
$cb = {
  param($h, $l)
  $wpid = 0
  [RDS]::GetWindowThreadProcessId($h, [ref]$wpid) | Out-Null
  if ($wpid -eq $p.Id) {
    $c = New-Object System.Text.StringBuilder 128
    [RDS]::GetClassNameW($h, $c, 128) | Out-Null
    $script:all += ("{0}:{1}" -f $c.ToString(), [RDS]::IsWindowVisible($h))
  }
  return $true
}
[RDS]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
Write-Host ("windows after click: " + ($all -join ", "))

# ---- 3. find the picker dialog ----
$p = Get-Process RightDial -ErrorAction SilentlyContinue | Select-Object -First 1
$dlg = [IntPtr]::Zero
for ($i = 0; $i -lt 25 -and $dlg -eq [IntPtr]::Zero; $i++) {
  Start-Sleep -Milliseconds 200
  $cb = {
    param($h, $l)
    $wpid = 0
    [RDS]::GetWindowThreadProcessId($h, [ref]$wpid) | Out-Null
    if ($wpid -eq $p.Id -and [RDS]::IsWindowVisible($h)) {
      $c = New-Object System.Text.StringBuilder 128
      [RDS]::GetClassNameW($h, $c, 128) | Out-Null
      if ($c.ToString() -eq "#32770") { $script:dlg = $h; return $false }
    }
    return $true
  }
  [RDS]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
}
if ($dlg -eq [IntPtr]::Zero) { Write-Host "FAIL: picker dialog not found"; exit 1 }
$t = New-Object System.Text.StringBuilder 256
[RDS]::GetWindowTextW($dlg, $t, 256) | Out-Null
Write-Host "picker dialog: $($t)"
Start-Sleep -Milliseconds 700
Shot-Window $dlg "$OutDir\lucide_picker.png"

# ---- 4. search "folder" (WM_SETTEXT triggers EN_CHANGE -> filter) ----
$edit = Find-ChildByCtlId $dlg 1001
$ptr = [System.Runtime.InteropServices.Marshal]::StringToHGlobalUni("folder")
[RDS]::SendMessageW($edit, 0x000C, [IntPtr]::Zero, $ptr) | Out-Null   # WM_SETTEXT
[System.Runtime.InteropServices.Marshal]::FreeHGlobal($ptr)
Start-Sleep -Milliseconds 900
$sb = New-Object System.Text.StringBuilder 128
$stCount = Find-ChildByCtlId $dlg 1002
[RDS]::GetWindowTextW($stCount, $sb, 128) | Out-Null
Write-Host "count text after search: $($sb)"
Shot-Window $dlg "$OutDir\lucide_picker_search.png"

# ---- 5. pick grid cell row1 col2 (posted mouse messages), apply ----
$grid = Find-ChildByCtlId $dlg 1004
$lp = [IntPtr](((105) -band 0xFFFF) -bor ((40) -shl 16))
[RDS]::PostMessageW($grid, 0x0201, [IntPtr]1, $lp) | Out-Null    # WM_LBUTTONDOWN
[RDS]::PostMessageW($grid, 0x0202, [IntPtr]0, $lp) | Out-Null    # WM_LBUTTONUP
Start-Sleep -Milliseconds 500
$sb2 = New-Object System.Text.StringBuilder 128
$stName = Find-ChildByCtlId $dlg 1003
[RDS]::GetWindowTextW($stName, $sb2, 128) | Out-Null
Write-Host "selection text after grid click: $($sb2)"
Shot-Window $dlg "$OutDir\lucide_picker_selected.png"
[RDS]::PostMessageW($dlg, 0x0111, [IntPtr]1, [IntPtr]::Zero) | Out-Null   # WM_COMMAND IDOK
Start-Sleep -Milliseconds 900

# ---- 6. settings window after apply + config check ----
Shot-Window $main "$OutDir\lucide_settings_after.png"
$cfg = "$env:APPDATA\RightDial\config.json"
if (Test-Path $cfg) {
  $json = Get-Content $cfg -Raw -Encoding UTF8
  if ($json -match 'builtin:lucide:[a-z0-9\-]+') { Write-Host "CONFIG OK: $($Matches[0])" }
  else { Write-Host "CONFIG: no builtin:lucide found (apply may have failed)" }
} else { Write-Host "CONFIG: $cfg missing" }
taskkill /F /IM RightDial.exe 2>$null | Out-Null
Write-Host "done"
