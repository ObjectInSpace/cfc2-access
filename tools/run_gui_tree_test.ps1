# Build and run the offline self-test for the instantiated GUI tree walk.
#
# Plants a REAL layout name pool plus synthetic node structs holding real
# pointers into it -- the exact shape the live walk keys on, and one that
# cannot occur in the shipped file (the file has no pointers at all).
#
# Run this after ANY change to gui_tree.c. "Found nothing" and "the walker is
# broken" look identical in a live log; here they do not.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$gcc = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\gcc.exe"
$tmp = Join-Path $env:TEMP 'cfc2_gtreetest'
New-Item -ItemType Directory -Force -Path $tmp | Out-Null

# 1. Dump a real pool.
$pool = Join-Path $tmp 'main_menu_2.pool'
python "$root\tools\dump_gui_pool.py" --layout main_menu_2 --out $pool
if ($LASTEXITCODE -ne 0) { throw "pool dump failed" }

# 2. Build against the real sources -- never a copy.
$exe = Join-Path $tmp 'gtreetest.exe'
& $gcc -O1 -Wall -Wextra -Wclobbered -o $exe `
    "$root\tools\test_gui_tree.c" "$root\src\gui_tree.c" `
    "$root\src\gui_find.c" "$root\src\memsafe.c" "$root\src\log.c" `
    -I "$root\src" -lpsapi
if ($LASTEXITCODE -ne 0) { throw "compile failed" }

# 3. Run it.
& $exe $pool
exit $LASTEXITCODE
