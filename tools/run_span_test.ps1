# What the fault guard covers -- and the one thing it does NOT.
#
# ⛔ Case C asserts that a guarded memcpy across a bad page CRASHES. That is a
# known limitation, pinned so it cannot change silently: every bulk read in this
# project must call mem_readable_run() BEFORE copying, and the guard is only a
# net for the race between that check and the copy.
#
# The crashing case runs in a child process, so this suite reports it rather
# than dying of it.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$gcc = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\gcc.exe"
if (-not (Test-Path $gcc)) { throw "GCC not found at $gcc" }

$out = "$root\build\test"
New-Item -ItemType Directory -Force -Path $out | Out-Null

& $gcc -O2 -Wall -Wextra -Wclobbered -o "$out\test_span_safety.exe" `
    "$root\tools\test_span_safety.c" "$root\src\memsafe.c" "$root\src\log.c" `
    -I "$root\src" -luser32
if ($LASTEXITCODE -ne 0) { throw "test build failed" }

Push-Location $out
try { & "$out\test_span_safety.exe"; $code = $LASTEXITCODE } finally { Pop-Location }
exit $code
