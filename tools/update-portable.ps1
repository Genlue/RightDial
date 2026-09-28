$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$dist = 'C:\Users\25659\Desktop\Project\RightDial\dist\RightDial'
$zip  = 'C:\Users\25659\Desktop\Project\RightDial\bin\RightDial-1.0.2-portable.zip'
# rebuild the whole archive from the dist folder: atomic, no duplicate-entry
# residue from incremental updates, spec-compliant '/' entry names
if (Test-Path $zip) { Remove-Item -LiteralPath $zip -Force }
[System.IO.Compression.ZipFile]::CreateFromDirectory($dist, $zip, [System.IO.Compression.CompressionLevel]::Optimal, $true)
$z = [System.IO.Compression.ZipFile]::OpenRead($zip)
try {
    Write-Host ("entries: {0}" -f $z.Entries.Count)
    foreach ($e in $z.Entries) { Write-Host ("{0}  {1}" -f $e.FullName, $e.Length) }
} finally { $z.Dispose() }
