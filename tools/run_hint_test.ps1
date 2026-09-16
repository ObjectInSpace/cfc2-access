# The HINTS VIEW path, end to end (Fighter Awards -> DECIDE).
#
# ⛔⛔ WHY ITS OWN RUNNER. The bug this feature fixes was NOT a missing
# capability -- gt already captured the text and prompt_identify could already
# resolve it. Nothing connected them, so the user sat in silence with the hint
# on screen. Unit cases on either half would both have passed. This drives
# menu_speak_check_hint() itself and reads what it SPOKE out of the log.
#
# ⭐ No resolver stub, unlike run_modal_test.ps1: prompt_id.c carries the real
# generated table, so identification here is the shipping identification.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$gcc = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\gcc.exe"
if (-not (Test-Path $gcc)) { throw "GCC not found at $gcc" }
$out = "$root\build\test"
New-Item -ItemType Directory -Force $out | Out-Null
$mh = Get-ChildItem "$root\libs\minhook\src" -Recurse -Filter *.c | ForEach-Object { $_.FullName }

& $gcc -O2 -Wall -o "$out\t_hint.exe" `
    "$root\tools\t_hint_probe.c" `
    "$root\src\gui_text.c" "$root\src\menu_speak.c" "$root\src\gui_markup.c" `
    "$root\src\move_list.c" `
    "$root\src\gmd_find.c" "$root\src\prompt_id.c" "$root\src\rom_table.c" `
    "$root\src\log.c" "$root\src\speech.c" "$root\src\menu_cursor.c" `
    "$root\src\memsafe.c" `
    @mh -I "$root\libs\minhook\include" -luser32
if ($LASTEXITCODE -ne 0) { throw "test build failed" }

Push-Location $out
try { & "$out\t_hint.exe"; $code = $LASTEXITCODE } finally { Pop-Location }
exit $code
