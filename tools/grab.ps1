# Capture one CVS2 state snapshot. Finds the PID itself.
# Usage:  .\tools\grab.ps1 hp-full
param([Parameter(Mandatory=$true)][string]$Label)

$p = Get-Process -ErrorAction SilentlyContinue |
     Where-Object { $_.ProcessName -match 'CAPCOM|CFC2|fighting' } |
     Select-Object -First 1
if (-not $p) {
  Write-Host "Game process not found. Is CFC2 running?" -ForegroundColor Red
  exit 1
}

# Never collide with an existing capture: the tool refuses to overwrite.
$out = "build/cvs2-state/$Label"
$n = 1
while ((Test-Path "$out.bin") -or (Test-Path "$out.json")) {
  $out = "build/cvs2-state/$Label-$n"; $n++
}

Write-Host "pid $($p.Id) -> $out" -ForegroundColor Cyan
python tools/cvs2_state_snapshot.py $p.Id --out $out
if ($LASTEXITCODE -ne 0) {
  Write-Host "CAPTURE FAILED (see message above). Nothing was written." -ForegroundColor Yellow
  exit $LASTEXITCODE
}
Write-Host "OK: $out.bin" -ForegroundColor Green
