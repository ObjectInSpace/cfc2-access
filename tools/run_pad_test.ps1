# Build and run the offline self-test for the pad-state finder.
#
# Run this after ANY change to src/pad_find.c. The scanner's silence in-game
# is only meaningful if a known-good planted pad is reliably found here.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$gcc = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\gcc.exe"
if (-not (Test-Path $gcc)) { throw "GCC not found at $gcc" }

New-Item -ItemType Directory -Force -Path "$root\build" | Out-Null
$exe = "$root\build\test_pad_find.exe"

& $gcc -O1 -g -Wall -Wextra `
    -o $exe `
    "$root\tools\test_pad_find.c" "$root\src\pad_find.c" "$root\src\memsafe.c" `
    -luser32
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

& $exe
if ($LASTEXITCODE -ne 0) { throw "SELF-TEST FAILED" }
Write-Host "pad_find self-test OK"
