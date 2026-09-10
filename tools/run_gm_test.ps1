# Build and run the GUI markup translator self-test.
#
# Purely offline: pure string handling, no game launch and no game memory.
# See tools/test_gui_markup.c.
#
# ⚠ Run this through PowerShell directly -- via some shells a crashing suite
# has reported exit 0 with no output on this project.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$gcc = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\gcc.exe"
if (-not (Test-Path $gcc)) { throw "GCC not found at $gcc" }

$out = "$root\build\test"
New-Item -ItemType Directory -Force -Path $out | Out-Null

& $gcc -O2 -Wall -Wextra -Wclobbered -o "$out\test_gui_markup.exe" `
    "$root\tools\test_gui_markup.c" "$root\src\gui_markup.c"
if ($LASTEXITCODE -ne 0) { throw "test build failed" }

Push-Location $out
try { & "$out\test_gui_markup.exe"; $code = $LASTEXITCODE } finally { Pop-Location }
exit $code
