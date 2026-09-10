# Prove the classified discovery sweep matches the original, and measure it.
#
# ⭐ Both halves matter. The equivalence test says the rewrite did not change
# WHICH screens can be named; the benchmark says it is fast enough to run on
# the poll thread. The rewrite exists because an unbounded sweep stalled a live
# run for 37 SECONDS.
#
# ⚠ Needs build/strings.bin -- regenerate with tools/dump_string_blob.py. It is
# derived from the game's own msg.arc and is gitignored.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$gcc = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\gcc.exe"
if (-not (Test-Path $gcc)) { throw "GCC not found at $gcc" }

$out = "$root\build\test"
New-Item -ItemType Directory -Force -Path $out | Out-Null

$blob = "$root\build\strings.bin"
if (-not (Test-Path $blob)) {
    Write-Host "build/strings.bin missing -- generating it"
    python "$root\tools\dump_string_blob.py" $blob
}

$src = @("$root\src\gmd_find.c", "$root\src\memsafe.c", "$root\src\log.c")

& $gcc -O2 -w -o "$out\test_discovery_equiv.exe" `
    "$root\tools\test_discovery_equiv.c" @src -I "$root\src" -luser32
if ($LASTEXITCODE -ne 0) { throw "equivalence test build failed" }
& "$out\test_discovery_equiv.exe" $blob
if ($LASTEXITCODE -ne 0) { throw "the rewritten sweep does NOT match the original" }

& $gcc -O2 -w -o "$out\bench_discovery.exe" `
    "$root\tools\bench_discovery.c" @src -I "$root\src" -luser32
if ($LASTEXITCODE -ne 0) { throw "benchmark build failed" }
& "$out\bench_discovery.exe" $blob
exit $LASTEXITCODE
