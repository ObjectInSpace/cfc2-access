# Build and run the offline self-test for the differential heap scan.
#
# The load-bearing case is 5: a page that ticks on its own must be REMOVED by
# the control subtraction, even though it also changes across the transition.
# Without that the scanner reports every timer in the process and looks like a
# success while being worthless.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$gcc = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\gcc.exe"
$tmp = Join-Path $env:TEMP 'cfc2_hdtest'
New-Item -ItemType Directory -Force -Path $tmp | Out-Null

$exe = Join-Path $tmp 'hdtest.exe'
& $gcc -O1 -Wall -Wextra -Wclobbered -o $exe `
    "$root\tools\test_heapdiff.c" "$root\src\heapdiff.c" `
    "$root\src\memsafe.c" "$root\src\log.c" "$root\src\speech.c" `
    -I "$root\src" -lpsapi
if ($LASTEXITCODE -ne 0) { throw "compile failed" }

& $exe
exit $LASTEXITCODE
