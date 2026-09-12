# The MOVE LIST reader: does it name the focused move, and REFUSE everywhere a
# wrong name could be spoken?
#
# ⛔⛔ WHY THE REFUSALS DOMINATE. The reader follows a chain of pointers and
# indices read out of GAME memory: screen -> game index -> per-game table ->
# 12-byte record -> GMD index -> text. The per-table record COUNT is not
# derived, so a wild move id cannot be checked against its table's real length.
# An unchecked id would resolve to some unrelated GMD string and the mod would
# speak a confidently wrong move name -- for a blind user, indistinguishable
# from a correct one. Eight of the eleven cases are refusals.
#
# ⭐ It drives the SHIPPING lookup: a planted message blob means
# gmd_text_by_index() does the real walk rather than a harness copy.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$gcc = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\gcc.exe"
if (-not (Test-Path $gcc)) { throw "GCC not found at $gcc" }

$out = "$root\build\test"
New-Item -ItemType Directory -Force -Path $out | Out-Null

& $gcc -O2 -Wall -Wextra -Wclobbered -o "$out\test_move_list.exe" `
    "$root\tools\test_move_list.c" `
    "$root\src\move_list.c" "$root\src\gmd_find.c" `
    "$root\src\memsafe.c" "$root\src\log.c" `
    -I "$root\src" -luser32
if ($LASTEXITCODE -ne 0) { throw "test build failed" }

Push-Location $out
try { & "$out\test_move_list.exe"; $code = $LASTEXITCODE } finally { Pop-Location }
exit $code
