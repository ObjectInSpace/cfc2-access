# Does the resolver identify the table's LANGUAGE, and pick the blob matching
# the player's setting?
#
# ⛔⛔ WHY THIS FILE EXISTS AT ALL. tools/test_gmd_language.c was ORPHANED: no
# runner referenced it, so it was never compiled and never run by the suite
# sweep. "All 16 runners exit 0" therefore said nothing whatsoever about
# language behaviour -- and the bug that test was written for (the mod speaking
# French while the game displayed English) is one of the worst this project has
# shipped.
#
# ⭐ That is a sharper form of "a suite that cannot build looks like one that
# passes": this one built fine and was simply never invoked. A test nobody runs
# is a test nobody has.
#
# Covers: index-0 self-identification for all 14 shipped tables (including the
# nine non-Latin ones that previously returned NULL), the Spain/Mexico
# tiebreaker at index 29, and the refusals -- an unrecognised table and an
# unreadable tiebreak must both report UNKNOWN rather than guessing.
# Cases 7a-7e (2026-10-04): the game's own language index [app+0x40] outranks
# config.ini, maps in the game's 0..13 order, and falls back to config.ini --
# never a default -- when it is out of range or the app object is absent.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$gcc = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\gcc.exe"
if (-not (Test-Path $gcc)) { throw "GCC not found at $gcc" }

$out = "$root\build\test"
New-Item -ItemType Directory -Force -Path $out | Out-Null

& $gcc -O2 -Wall -Wextra -Wclobbered -o "$out\test_gmd_language.exe" `
    "$root\tools\test_gmd_language.c" `
    "$root\src\gmd_find.c" "$root\src\memsafe.c" "$root\src\log.c" `
    -I "$root\src" -luser32
if ($LASTEXITCODE -ne 0) { throw "test build failed" }

Push-Location $out
try { & "$out\test_gmd_language.exe"; $code = $LASTEXITCODE } finally { Pop-Location }
exit $code
