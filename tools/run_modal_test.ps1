# The MODAL PROMPT path, end to end.
#
# ⛔⛔ WHY THIS EXISTS AS ITS OWN RUNNER. The unit cases in run_ms_test.ps1 test
# the flattener and the matcher SEPARATELY, and they all passed while I had no
# evidence the CHAIN worked -- I claimed the feature was done on the strength of
# "it compiles and the units pass". The user's reply was "you did nothing", and
# the honest answer was that I had not run it.
#
# This drives the real shipping path: a text object is set with NO cursor
# movement (which is what the quit dialogs do), and the test requires
# gt_latest_text -> menu_speak_is_modal_prompt -> menu_speak_flatten_prompt to
# produce the exact line the user should hear.
#
# ⚠ The resolver is stubbed because there is no GMD table in a test process.
# That is the ONLY substitution; every other link is the shipping code.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$gcc = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\gcc.exe"
if (-not (Test-Path $gcc)) { throw "GCC not found at $gcc" }
$out = "$root\build\test"
New-Item -ItemType Directory -Force $out | Out-Null
$mh = Get-ChildItem "$root\libs\minhook\src" -Recurse -Filter *.c | ForEach-Object { $_.FullName }

& $gcc -O2 -Wall -o "$out\t_modal.exe" `
    "$root\tools\t_modal_probe.c" `
    "$root\src\gui_text.c" "$root\src\menu_speak.c" "$root\src\gui_markup.c" `
    "$root\src\text_channel.c" "$root\src\mod_text.c" `
    "$root\src\move_list.c" `
    "$root\src\gmd_find.c" "$root\src\prompt_id.c" "$root\src\rom_table.c" `
    "$root\src\log.c" "$root\src\speech.c" "$root\src\menu_cursor.c" `
    "$root\src\memsafe.c" `
    @mh -I "$root\libs\minhook\include" -luser32
if ($LASTEXITCODE -ne 0) { throw "test build failed" }

Push-Location $out
try { & "$out\t_modal.exe"; $code = $LASTEXITCODE } finally { Pop-Location }
exit $code
