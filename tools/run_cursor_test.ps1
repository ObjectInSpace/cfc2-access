# Build and run the in-game cursor probe self-test. Offline: no game launch.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$gcc = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\gcc.exe"
if (-not (Test-Path $gcc)) { throw "GCC not found at $gcc" }
$out = "$root\build\test"
New-Item -ItemType Directory -Force -Path $out | Out-Null
# -Wclobbered earns its place: emu_cursor.c reads foreign memory under the
# setjmp/longjmp fault guard, which is exactly where a non-volatile local
# modified between setjmp and the fault becomes indeterminate.
& $gcc -O2 -Wall -Wextra -Wclobbered -o "$out\test_emu_cursor.exe" `
    "$root\tools\test_emu_cursor.c" "$root\src\emu_cursor.c" `
    "$root\src\emu_ram.c" "$root\src\memsafe.c" "$root\src\log.c" -luser32
if ($LASTEXITCODE -ne 0) { throw "test build failed" }
Push-Location $out
try { & "$out\test_emu_cursor.exe"; $code = $LASTEXITCODE } finally { Pop-Location }
exit $code
