$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$root = Split-Path $PSScriptRoot -Parent
$dist = Join-Path $root 'dist\RightDial'
$zip  = Join-Path $root 'bin\RightDial-1.0.3-portable.zip'

# Always package the freshly built exe. dist\RightDial\RightDial.exe is
# gitignored and never refreshed by build.cmd, so without this step the
# portable zip silently ships whatever stale binary was left there.
$srcExe = Join-Path $root 'bin\RightDial.exe'
if (-not (Test-Path $srcExe)) { throw "missing $srcExe - run build.cmd first" }
$dstExe = Join-Path $dist 'RightDial.exe'
Copy-Item -LiteralPath $srcExe -Destination $dstExe -Force
$vi = [System.Diagnostics.FileVersionInfo]::GetVersionInfo($dstExe)
Write-Host ("packaging RightDial.exe {0} ({1} bytes)" -f $vi.FileVersion, (Get-Item $dstExe).Length)

# rebuild the whole archive from the dist folder: atomic, no duplicate-entry
# residue from incremental updates, spec-compliant '/' entry names
if (Test-Path $zip) { Remove-Item -LiteralPath $zip -Force }
[System.IO.Compression.ZipFile]::CreateFromDirectory($dist, $zip, [System.IO.Compression.CompressionLevel]::Optimal, $true)
$z = [System.IO.Compression.ZipFile]::OpenRead($zip)
try {
    Write-Host ("entries: {0}" -f $z.Entries.Count)
    foreach ($e in $z.Entries) { Write-Host ("{0}  {1}" -f $e.FullName, $e.Length) }
} finally { $z.Dispose() }
