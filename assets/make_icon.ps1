# Generates assets\rightdial.ico (multi-size, PNG-compressed ICO) by drawing a radial wheel.
Add-Type -AssemblyName System.Drawing

$script:self = $MyInvocation.MyCommand.Path
$outIco = Join-Path $PSScriptRoot "rightdial.ico"
$sizes = @(16,24,32,48,64,128,256)
$entries = @()

function New-WheelBitmap([int]$s) {
  $bmp = New-Object System.Drawing.Bitmap($s, $s)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
  $g.Clear([System.Drawing.Color]::Transparent)
  $cx = $s / 2.0
  $rO = $s * 0.48
  # dark base disc
  $bg = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255,30,36,48))
  $g.FillEllipse($bg, [float]($cx-$rO), [float]($cx-$rO), [float]($rO*2), [float]($rO*2))
  $bg.Dispose()
  # 8 colored sectors
  $cols = @(@(86,124,196),@(196,138,86),@(96,176,132),@(196,96,120),@(150,110,196),@(96,180,180),@(196,168,90),@(140,152,172))
  $inner = $rO * 0.74
  $rectI = New-Object System.Drawing.Rectangle ([int]($cx-$inner)), ([int]($cx-$inner)), ([int]($inner*2)), ([int]($inner*2))
  for ($i = 0; $i -lt 8; $i++) {
    $c = $cols[$i]
    $b = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(230,$c[0],$c[1],$c[2]))
    $g.FillPie($b, $rectI, [float](-90 + $i*45 + 3), [float]39)
    $b.Dispose()
  }
  # separator ring + center dot
  $pen = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(120,255,255,255)), ([float]([Math]::Max(1.0, $s*0.02)))
  $g.DrawEllipse($pen, [float]($cx-$rO*0.97), [float]($cx-$rO*0.97), [float]($rO*1.94), [float]($rO*1.94))
  $pen.Dispose()
  $cbr = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255,226,232,242))
  $rc = $s * 0.085
  $g.FillEllipse($cbr, [float]($cx-$rc), [float]($cx-$rc), [float]($rc*2), [float]($rc*2))
  $cbr.Dispose()
  $g.Dispose()
  return $bmp
}

foreach ($s in $sizes) {
  $bmp = New-WheelBitmap $s
  $ms = New-Object System.IO.MemoryStream
  $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
  $script:entries += ,@($s, $ms.ToArray())
  $ms.Dispose()
  $bmp.Dispose()
}

$ico = New-Object System.IO.MemoryStream
$w = New-Object System.IO.BinaryWriter($ico)
$w.Write([uint16]0); $w.Write([uint16]1); $w.Write([uint16]$entries.Count)
$offset = 6 + 16 * $entries.Count
foreach ($e in $entries) {
  $sz = [Math]::Min(255, $e[0])
  $w.Write([byte]$sz); $w.Write([byte]$sz)
  $w.Write([byte]0); $w.Write([byte]0)
  $w.Write([uint16]1); $w.Write([uint16]32)
  $w.Write([uint32]$e[1].Length)
  $w.Write([uint32]$offset)
  $offset += $e[1].Length
}
foreach ($e in $entries) { $w.Write($e[1]) }
$w.Flush()
[System.IO.File]::WriteAllBytes($outIco, $ico.ToArray())
$w.Dispose(); $ico.Dispose()
Write-Host "OK -> $outIco"
