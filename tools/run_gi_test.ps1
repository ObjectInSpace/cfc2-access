# Build and run the GMD string-index self-test. Offline: no game launch.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$gcc = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\gcc.exe"
if (-not (Test-Path $gcc)) { throw "GCC not found at $gcc" }
$out = "$root\build\test"
New-Item -ItemType Directory -Force -Path $out | Out-Null
& $gcc -O2 -Wall -Wextra -o "$out\test_gmd_index.exe" `
    "$root\tools\test_gmd_index.c" "$root\src\gmd_find.c" `
    "$root\src\log.c" "$root\src\memsafe.c" -luser32
if ($LASTEXITCODE -ne 0) { throw "test build failed" }
Push-Location $out
try { & "$out\test_gmd_index.exe"; $code = $LASTEXITCODE } finally { Pop-Location }
exit $code
