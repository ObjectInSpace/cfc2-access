# Build and run the ROM-table self-test. Offline: no game launch.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$gcc = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\gcc.exe"
if (-not (Test-Path $gcc)) { throw "GCC not found at $gcc" }
$out = "$root\build\test"
New-Item -ItemType Directory -Force -Path $out | Out-Null
& $gcc -O2 -Wall -Wextra -o "$out\test_rom_table.exe" `
    "$root\tools\test_rom_table.c" "$root\src\rom_table.c" `
    "$root\src\memsafe.c" "$root\src\log.c" -luser32
if ($LASTEXITCODE -ne 0) { throw "test build failed" }
Push-Location $out
try { & "$out\test_rom_table.exe"; $code = $LASTEXITCODE } finally { Pop-Location }
exit $code
