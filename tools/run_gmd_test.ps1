# Build and run the offline heap-scanner self-test.
#
# Extracts the real menu_eng GMD from the shipped msg.arc, plants it in a test
# process's memory, and requires the scanner to find and verify it -- plus a
# negative control that must report absence once the image is wiped.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$gcc = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\gcc.exe"
$tmp = Join-Path $env:TEMP 'cfc2_gmdtest'
New-Item -ItemType Directory -Force -Path $tmp | Out-Null

# 1. Dump the real menu_eng blob.
$blob = Join-Path $tmp 'menu_eng.bin'
python "$root\tools\dump_menu_eng.py" --out $blob
if ($LASTEXITCODE -ne 0) { throw "dump failed" }

# 2. Build the harness against the real scanner sources.
$exe = Join-Path $tmp 'gmdtest.exe'
& $gcc -O1 -Wall -Wextra -o $exe `
    "$root\tools\test_gmd_find.c" "$root\src\gmd_find.c" "$root\src\memsafe.c" `
    -I "$root\src" -lpsapi
if ($LASTEXITCODE -ne 0) { throw "compile failed" }

# 3. Run it.
& $exe $blob
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# 4. Regression: the same scan against a NON-ENGLISH image.
#
# A live Italian run reported text_verified=no on a table that was in fact
# correct and label-verified, because the expectation table held English only.
# English-only planting cannot catch that -- it passes either way.
Write-Host ''
Write-Host '==== locale regression: Italian image ===='
$itaBlob = Join-Path $tmp 'menu_ita.bin'
python "$root\tools\dump_menu_eng.py" --out $itaBlob --lang ita
if ($LASTEXITCODE -ne 0) { throw "italian dump failed" }
& $exe $itaBlob
exit $LASTEXITCODE
