Get-Process RightDial -ErrorAction SilentlyContinue | ForEach-Object {
  "{0}  pid={1}  workingset={2}MB  private={3}MB" -f $_.ProcessName, $_.Id,
    [math]::Round($_.WorkingSet64/1MB,2), [math]::Round($_.PrivateMemorySize64/1MB,2)
}
if (-not (Get-Process RightDial -ErrorAction SilentlyContinue)) { "NOT RUNNING" }
