# Build and run the menu-speech self-test. Offline: no game launch.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$gcc = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\gcc.exe"
if (-not (Test-Path $gcc)) { throw "GCC not found at $gcc" }
$out = "$root\build\test"
New-Item -ItemType Directory -Force -Path $out | Out-Null
# ⚠ menu_speak now appends the focused row's live caption, so this suite needs
# gui_text (the caption source), gui_markup (markup -> speech) and prompt_id
# (which label produced the string) linked in. MinHook comes with gui_text.
# A shared-header change that leaves a runner UN-BUILDABLE looks exactly like a
# passing suite (memory: a-suite-that-cannot-build).
$minhookSrc = @(
    "$root\libs\minhook\src\hook.c",
    "$root\libs\minhook\src\buffer.c",
    "$root\libs\minhook\src\trampoline.c",
    "$root\libs\minhook\src\hde\hde64.c"
)
& $gcc -O2 -Wall -Wextra -o "$out\test_menu_speak.exe" `
    "$root\tools\test_menu_speak.c" "$root\src\menu_speak.c" `
    "$root\src\gmd_find.c" "$root\src\speech.c" "$root\src\log.c" `
    "$root\src\rom_table.c" "$root\src\move_list.c" `
    "$root\src\gui_text.c" "$root\src\gui_markup.c" "$root\src\prompt_id.c" `
    @minhookSrc `
    -I "$root\libs\minhook\include" `
    "$root\src\memsafe.c" -luser32
if ($LASTEXITCODE -ne 0) { throw "test build failed" }
Push-Location $out
try { & "$out\test_menu_speak.exe"; $code = $LASTEXITCODE } finally { Pop-Location }
exit $code
