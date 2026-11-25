# Regression probe for remote-control input (UU远程 / ToDesk / RDP ...).
#
# Remote tools drive the controlled machine by injecting input (SendInput), so
# every event they produce carries LLMHF_INJECTED / LLKHF_INJECTED. This probe
# replays exactly that path:
#   1. injected right-drag  -> the wheel must open
#   2. injected Esc         -> the wheel must cancel
#   3. pixels of the wheel  -> must be present in a screen capture, because a
#      remote viewer receives the desktop through Desktop Duplication, which
#      drops windows marked WDA_EXCLUDEFROMCAPTURE
#
# Usage:
#   powershell -File tools\remoteprobe.ps1                 # remote path, wheel must be capturable
#   powershell -File tools\remoteprobe.ps1 -Local          # local path (/testwheel), wheel must stay out of captures
param([string]$Exe = "bin\RightDial.exe", [switch]$Local)

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class RDProbe {
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int x, y; }
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [StructLayout(LayoutKind.Sequential)] public struct BITMAPINFOHEADER {
    public uint biSize; public int biWidth, biHeight; public ushort biPlanes, biBitCount;
    public uint biCompression, biSizeImage; public int biXPelsPerMeter, biYPelsPerMeter;
    public uint biClrUsed, biClrImportant;
  }
  [StructLayout(LayoutKind.Sequential)] public struct MOUSEINPUT {
    public int dx, dy; public uint mouseData, dwFlags, time; public IntPtr dwExtraInfo;
  }
  [StructLayout(LayoutKind.Sequential)] public struct KEYBDINPUT {
    public ushort wVk, wScan; public uint dwFlags, time; public IntPtr dwExtraInfo;
  }
  [StructLayout(LayoutKind.Explicit)] public struct INPUTUNION {
    [FieldOffset(0)] public MOUSEINPUT mi;
    [FieldOffset(0)] public KEYBDINPUT ki;
  }
  [StructLayout(LayoutKind.Sequential)] public struct INPUT {
    public uint type; public INPUTUNION u;
  }
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern uint GetWindowDisplayAffinity(IntPtr h, out uint a);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern uint SendInput(uint n, INPUT[] i, int size);
  [DllImport("user32.dll")] public static extern IntPtr GetDC(IntPtr h);
  [DllImport("user32.dll")] public static extern int ReleaseDC(IntPtr h, IntPtr dc);
  [DllImport("gdi32.dll")] public static extern IntPtr CreateCompatibleDC(IntPtr dc);
  [DllImport("gdi32.dll")] public static extern IntPtr CreateCompatibleBitmap(IntPtr dc, int w, int h);
  [DllImport("gdi32.dll")] public static extern IntPtr SelectObject(IntPtr dc, IntPtr o);
  [DllImport("gdi32.dll")] public static extern bool BitBlt(IntPtr d, int x, int y, int w, int h, IntPtr s, int sx, int sy, uint rop);
  [DllImport("gdi32.dll")] public static extern bool DeleteObject(IntPtr o);
  [DllImport("gdi32.dll")] public static extern bool DeleteDC(IntPtr dc);
  [DllImport("gdi32.dll")] public static extern int GetDIBits(IntPtr dc, IntPtr bmp, uint start, uint lines, byte[] bits, ref BITMAPINFOHEADER bi, uint usage);
  [DllImport("shcore.dll")] public static extern int SetProcessDpiAwareness(int value);
  public delegate bool EnumProc(IntPtr h, IntPtr l);

  public const uint RDOWN = 0x0008, RUP = 0x0010, MOVE = 0x0001;
  public static void Mouse(uint flags, int dx, int dy) {
    INPUT[] a = new INPUT[1];
    a[0].type = 0;
    a[0].u.mi.dwFlags = flags; a[0].u.mi.dx = dx; a[0].u.mi.dy = dy;
    SendInput(1, a, Marshal.SizeOf(typeof(INPUT)));
  }
  public static void Key(ushort vk, bool up) {
    INPUT[] a = new INPUT[1];
    a[0].type = 1;
    a[0].u.ki.wVk = vk;
    a[0].u.ki.dwFlags = up ? 2u : 0u;    // KEYEVENTF_KEYUP
    SendInput(1, a, Marshal.SizeOf(typeof(INPUT)));
  }
  public static IntPtr FindByClass(string cls) {
    IntPtr found = IntPtr.Zero;
    EnumWindows(delegate(IntPtr h, IntPtr l) {
      StringBuilder sb = new StringBuilder(128);
      GetClassNameW(h, sb, 128);
      if (sb.ToString() == cls) { found = h; return false; }
      return true;
    }, IntPtr.Zero);
    return found;
  }
  public static byte[] Grab(int x, int y, int w, int h) {
    IntPtr screen = GetDC(IntPtr.Zero);
    IntPtr mem = CreateCompatibleDC(screen);
    IntPtr bmp = CreateCompatibleBitmap(screen, w, h);
    IntPtr old = SelectObject(mem, bmp);
    BitBlt(mem, 0, 0, w, h, screen, x, y, 0x40CC0020); // SRCCOPY | CAPTUREBLT
    BITMAPINFOHEADER bi = new BITMAPINFOHEADER();
    bi.biSize = (uint)Marshal.SizeOf(typeof(BITMAPINFOHEADER));
    bi.biWidth = w; bi.biHeight = -h; bi.biPlanes = 1; bi.biBitCount = 32; bi.biCompression = 0;
    byte[] px = new byte[w * h * 4];
    GetDIBits(mem, bmp, 0, (uint)h, px, ref bi, 0);
    SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem); ReleaseDC(IntPtr.Zero, screen);
    return px;
  }
  public static double DiffPct(byte[] a, byte[] b) {
    int n = 0;
    for (int i = 0; i + 3 < a.Length; i += 4) {
      int d0 = Math.Abs(a[i] - b[i]), d1 = Math.Abs(a[i + 1] - b[i + 1]), d2 = Math.Abs(a[i + 2] - b[i + 2]);
      if (d0 > 16 || d1 > 16 || d2 > 16) n++;
    }
    return 100.0 * n / (a.Length / 4.0);
  }
}
"@

$cls = "RightDialWheel"
$fail = 0

# window rects and the screen DC are physical pixels: without awareness the
# captures below would sample the wrong region on a scaled display
try { [RDProbe]::SetProcessDpiAwareness(2) | Out-Null } catch { }

if (-not (Get-Process RightDial -ErrorAction SilentlyContinue)) {
  Start-Process -FilePath (Resolve-Path $Exe)
  Start-Sleep -Milliseconds 1200
}
$proc = Get-Process RightDial -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $proc) { Write-Host "[!] RightDial 未运行"; exit 1 }
Write-Host "[i] RightDial pid=$($proc.Id)"

Add-Type -AssemblyName System.Windows.Forms
$wa = [System.Windows.Forms.Screen]::PrimaryScreen.WorkingArea
[RDProbe]::SetCursorPos($wa.Left + [int]($wa.Width / 2), $wa.Top + [int]($wa.Height / 2)) | Out-Null
Start-Sleep -Milliseconds 150

$opened = $false
if ($Local) {
  # /testwheel goes through the normal "physical input" path
  & (Resolve-Path $Exe) /testwheel | Out-Null
  Start-Sleep -Milliseconds 700
  $wheel = [RDProbe]::FindByClass($cls)
  $opened = ($wheel -ne [IntPtr]::Zero) -and [RDProbe]::IsWindowVisible($wheel)
  if (-not $opened) { Write-Host "[FAIL] /testwheel 未能显示轮盘"; $fail++ }
} else {
  [RDProbe]::Mouse([RDProbe]::RDOWN, 0, 0)
  for ($i = 1; $i -le 6; $i++) { [RDProbe]::Mouse([RDProbe]::MOVE, 12, -4); Start-Sleep -Milliseconds 25 }
  Start-Sleep -Milliseconds 250
  $wheel = [RDProbe]::FindByClass($cls)
  $opened = ($wheel -ne [IntPtr]::Zero) -and [RDProbe]::IsWindowVisible($wheel)
  if ($opened) { Write-Host "[PASS] 注入输入（远程）右键拖动能唤出轮盘" }
  else {
    Write-Host "[FAIL] 注入输入（远程）右键拖不动：轮盘没有出现"
    $fail++
    [RDProbe]::Mouse([RDProbe]::RUP, 0, 0)
    Start-Sleep -Milliseconds 250
    [RDProbe]::Key(0x1B, $false); [RDProbe]::Key(0x1B, $true)
    Start-Sleep -Milliseconds 200
  }
}

if ($opened) {
  $r = New-Object RDProbe+RECT
  [RDProbe]::GetWindowRect($wheel, [ref]$r) | Out-Null
  $aff = 0
  [RDProbe]::GetWindowDisplayAffinity($wheel, [ref]$aff) | Out-Null
  $w = $r.R - $r.L; $h = $r.B - $r.T
  $shotWith = [RDProbe]::Grab($r.L, $r.T, $w, $h)

  # injected Esc must cancel, both for a remote session and locally
  [RDProbe]::Key(0x1B, $false); [RDProbe]::Key(0x1B, $true)
  Start-Sleep -Milliseconds 300
  $wheel = [RDProbe]::FindByClass($cls)
  $hidden = ($wheel -eq [IntPtr]::Zero) -or (-not [RDProbe]::IsWindowVisible($wheel))
  if ($hidden) { Write-Host "[PASS] 注入 Esc 能取消轮盘" }
  else {
    Write-Host "[FAIL] 注入 Esc 无法取消轮盘"
    $fail++
    & (Resolve-Path $Exe) /exit | Out-Null
    Start-Sleep -Milliseconds 700
  }

  $shotWithout = [RDProbe]::Grab($r.L, $r.T, $w, $h)
  $diff = [RDProbe]::DiffPct($shotWith, $shotWithout)
  Write-Host ("[i] 显示亲和性=0x{0:X}  截屏差异={1:N1}%" -f $aff, $diff)
  if ($Local) {
    # local use keeps the wheel out of screenshots / recordings
    if ($diff -lt 5.0) { Write-Host "[PASS] 本地使用时轮盘不出现在截屏中" }
    else { Write-Host "[FAIL] 本地使用时轮盘泄漏进了截屏"; $fail++ }
  } else {
    if ($diff -ge 5.0) { Write-Host "[PASS] 轮盘能被截屏/远程画面捕获" }
    else { Write-Host "[FAIL] 轮盘在截屏/远程画面中不可见（WDA_EXCLUDEFROMCAPTURE）"; $fail++ }
  }
}

[RDProbe]::Mouse([RDProbe]::RUP, 0, 0)   # never leave the button held

# --- plain (non-drag) right click must still reach the target window --------
# The hook swallows the down and re-emits the click from the message loop; if
# the re-emitted click were filtered as "injected", right click would die.
if (-not $Local) {
  Add-Type -AssemblyName System.Windows.Forms
  Add-Type -AssemblyName System.Drawing
  # the form only reacts if its message queue is pumped: Start-Sleep alone would
  # block the very thread that has to deliver MouseUp
  function Pump([int]$ms) {
    $t0 = [Environment]::TickCount
    while ([Environment]::TickCount - $t0 -lt $ms) {
      [System.Windows.Forms.Application]::DoEvents()
      Start-Sleep -Milliseconds 10
    }
  }
  $form = New-Object System.Windows.Forms.Form
  $form.Text = "RightDial click target"
  $form.StartPosition = "Manual"
  $form.Location = New-Object System.Drawing.Point(120, 120)
  $form.Width = 320; $form.Height = 220
  $script:gotRight = $false
  $form.Add_MouseUp({ param($s, $e) if ($e.Button -eq [System.Windows.Forms.MouseButtons]::Right) { $script:gotRight = $true } })
  $form.Show(); $form.Activate()
  Pump 400

  $c = $form.PointToScreen((New-Object System.Drawing.Point(160, 110)))
  [RDProbe]::SetCursorPos($c.X, $c.Y) | Out-Null
  Pump 120
  [RDProbe]::Mouse([RDProbe]::RDOWN, 0, 0)
  Pump 60
  [RDProbe]::Mouse([RDProbe]::RUP, 0, 0)
  Pump 400
  if ($script:gotRight) { Write-Host "[PASS] 普通右键单击仍能到达目标窗口（补发点击未被误过滤）" }
  else { Write-Host "[FAIL] 普通右键单击丢失——补发的点击被当作外来注入过滤掉了"; $fail++ }
  [RDProbe]::Key(0x1B, $false); [RDProbe]::Key(0x1B, $true)
  Pump 100
  $form.Close(); $form.Dispose()
}

if ($fail -eq 0) { Write-Host "ALL PASS" } else { Write-Host "$fail 项失败" }
exit $fail
