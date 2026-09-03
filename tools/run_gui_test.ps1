# Build and run the offline GUI-finder self-test.
#
# Extracts three real layout name pools from the shipped menu.arc, plants each
# in a test process, and requires the finder to name the correct screen --
# including a substring trap (`result` contains lobby_2's anchor inside a
# texture path) and a negative control that must report absence.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$gcc = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\gcc.exe"
$tmp = Join-Path $env:TEMP 'cfc2_guitest'
New-Item -ItemType Directory -Force -Path $tmp | Out-Null

# 1. Dump the pools.
$pools = @{}
foreach ($lay in @('main_menu_2', 'option_2', 'result')) {
    $p = Join-Path $tmp "$lay.pool"
    python "$root\tools\dump_gui_pool.py" --layout $lay --out $p
    if ($LASTEXITCODE -ne 0) { throw "pool dump failed for $lay" }
    $pools[$lay] = $p
}

# 2. Build the harness against the real finder sources.
$exe = Join-Path $tmp 'guitest.exe'
& $gcc -O1 -Wall -Wextra -o $exe `
    "$root\tools\test_gui_find.c" "$root\src\gui_find.c" "$root\src\memsafe.c" `
    -I "$root\src" -lpsapi
if ($LASTEXITCODE -ne 0) { throw "compile failed" }

# 3. Run it.
& $exe $pools['main_menu_2'] $pools['option_2'] $pools['result']
exit $LASTEXITCODE
