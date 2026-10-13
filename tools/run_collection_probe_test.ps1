$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$gcc = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\gcc.exe"
New-Item -ItemType Directory -Force -Path "$root\build\test" | Out-Null
& python "$root\tools\collection_probe_sites.py" --fixtures "$root\build\test\collection_probe_fixtures.inc"
if ($LASTEXITCODE -ne 0) { throw 'Collection probe fixture generation failed' }
& $gcc -O2 -Wall -Wextra -Wclobbered -Werror -static -o "$root\build\test\test_collection_probe.exe" `
    "$root\tools\test_collection_probe.c" "$root\src\memsafe.c"
if ($LASTEXITCODE -ne 0) { throw 'Collection probe test build failed' }
& "$root\build\test\test_collection_probe.exe"
if ($LASTEXITCODE -ne 0) { throw 'Collection probe test failed' }
# The harness ends by serializing a schema-3 synthetic application/Training
# pair and a failed retirement wait. Exercise the actual C/Python boundary.
$capture = Get-ChildItem "$root\build\test" -Filter 'cfc2_boundary_*.csv' |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $capture) { throw 'Missing synthetic lifetime capture' }
& python "$root\tools\collection_probe_report.py" $capture.FullName --out "$root\build\test\lifetime-synthetic-report.json"
if ($LASTEXITCODE -ne 0) { throw 'Synthetic lifetime serialization failed' }
$report = Get-Content "$root\build\test\lifetime-synthetic-report.json" -Raw | ConvertFrom-Json
if (-not $report.pairing_reliable -or $report.event_count -ne 6 -or
    $report.wait_outcomes.Count -ne 1 -or $report.wait_outcomes[0].outcome -ne 'failed' -or
    $report.wait_outcomes[0].last_error -ne 6) { throw 'Synthetic lifetime outcomes differ' }
