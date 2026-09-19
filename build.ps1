# Build and optionally deploy CFC2 Access.
# Usage:
#   .\build.ps1            # build only
#   .\build.ps1 -Deploy    # build + copy into the game folder
param([switch]$Deploy)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$gameDir = 'C:\Program Files (x86)\Steam\steamapps\common\CAPCOM FIGHTING COLLECTION 2'
$gcc = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\gcc.exe"

if (-not (Test-Path $gcc)) { throw "GCC not found at $gcc" }

$minhookSrc = @(
    "$root\libs\minhook\src\hook.c",
    "$root\libs\minhook\src\buffer.c",
    "$root\libs\minhook\src\trampoline.c",
    "$root\libs\minhook\src\hde\hde64.c"
)

New-Item -ItemType Directory -Force -Path "$root\build" | Out-Null

# ⭐⭐ -Wclobbered EARNS ITS PLACE. This project uses setjmp/longjmp for its
# fault guard, and a non-volatile local modified between setjmp and the fault is
# INDETERMINATE afterwards. That exact omission crashed the process once (the
# "validated span" fast path in gmd_find.c), and was caught a second time before
# it ever ran only because a TEST runner happened to use this flag -- the
# shipping build had no such protection. It compiles clean under it today.
& $gcc -shared -O2 -Wall -Wextra -Wclobbered -municode `
    -o "$root\build\dinput8.dll" `
    "$root\src\dinput8_proxy.c" "$root\src\mod.c" "$root\src\speech.c" `
    "$root\src\log.c" "$root\src\image.c" "$root\src\gmd_find.c" `
    "$root\src\pad_watch.c" "$root\src\memsafe.c" "$root\src\kbd_state.c" `
    "$root\src\menu_cursor.c" "$root\src\menu_speak.c" `
    "$root\src\rom_table.c" "$root\src\gui_text.c" `
    "$root\src\gui_markup.c" "$root\src\prompt_id.c" `
    "$root\src\move_list.c" "$root\src\emu_ram.c" "$root\src\emu_cursor.c" `
    "$root\src\cvs2_select.c" "$root\src\cvs2_hit.c" "$root\src\cvs2_frame.c" "$root\src\audio.c" `
    @minhookSrc `
    -I "$root\libs\minhook\include" `
    -luser32 -lpsapi -lwinmm -s
if ($LASTEXITCODE -ne 0) { throw "Build failed" }
Write-Host "Built build\dinput8.dll"

if ($Deploy) {
    if (-not (Test-Path $gameDir)) { throw "Game dir not found: $gameDir" }
    Copy-Item "$root\build\dinput8.dll" $gameDir -Force
    # nvdaControllerClient.dll is REQUIRED for NVDA output: UniversalSpeech
    # loads it by that exact (unsuffixed) name and silently falls back to SAPI5
    # if it is absent. ZDSRAPI.dll likewise backs the ZDSR reader.
    foreach ($name in @('UniversalSpeech.dll',
                        'nvdaControllerClient.dll',
                        'ZDSRAPI.dll',
                        'UniversalSpeech-LICENSE.txt',
                        'UniversalSpeech-LICENSE-notice.txt')) {
        Copy-Item "$root\libs\$name" $gameDir -Force
    }
    Write-Host "Deployed to $gameDir"
}
