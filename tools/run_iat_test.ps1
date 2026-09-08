# Build and run the offline self-test for the runtime IAT probe.
#
# Run this after ANY change to src/iat_probe.c. The probe's in-game NEGATIVE
# ("the shell does not import GetAsyncKeyState") is what the next step will be
# built on, and a negative is only worth acting on if the detector is known to
# report a positive when one is there -- and an ABORT when it cannot read.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$gcc = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\gcc.exe"
if (-not (Test-Path $gcc)) { throw "GCC not found at $gcc" }

New-Item -ItemType Directory -Force -Path "$root\build" | Out-Null
$exe = "$root\build\test_iat_probe.exe"

& $gcc -O1 -g -Wall -Wextra `
    -o $exe `
    "$root\tools\test_iat_probe.c" "$root\src\iat_probe.c" `
    "$root\src\memsafe.c" "$root\src\image.c" "$root\src\log.c" `
    -luser32 -lpsapi
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

& $exe
if ($LASTEXITCODE -ne 0) { throw "SELF-TEST FAILED" }
Write-Host "iat_probe self-test OK"
