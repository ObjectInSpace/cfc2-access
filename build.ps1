# Build and optionally deploy CFC2 Access.
# Usage:
#   .\build.ps1            # build only
#   .\build.ps1 -Deploy    # build + copy into the game folder
#   .\build.ps1 -Package 0.3.0   # build + build\cfc2-access-v0.3.0.zip
param([switch]$Deploy, [string]$Package)

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
    "$root\src\cvs2_select.c" "$root\src\cvs2_hit.c" "$root\src\cvs2_frame.c" "$root\src\cvs2_story.c" "$root\src\cvs2_text.c" `
    "$root\src\text_channel.c" "$root\src\text_capture.c" "$root\src\audio.c" `
    "$root\src\collection_probe.c" `
    "$root\src\selection_channel.c" "$root\src\game_selection.c" "$root\src\game_session.c" `
    "$root\src\match_audio.c" "$root\src\transition_channel.c" `
    @minhookSrc `
    -I "$root\libs\minhook\include" `
    -luser32 -lpsapi -lwinmm -s -static
if ($LASTEXITCODE -ne 0) { throw "Build failed" }
Write-Host "Built build\dinput8.dll"

# ⛔⛔ v0.3.0 shipped importing libgcc_s_seh-1.dll: players got "the executable
# cannot start because libgcc_s_seh-1.dll is missing". It worked here only
# because the WinLibs bin dir is on this machine's PATH. -static links the GCC
# runtime in; this check refuses any import that is not part of Windows itself.
$objdump = Join-Path (Split-Path $gcc) 'objdump.exe'
$imports = & $objdump -p "$root\build\dinput8.dll" |
    Select-String 'DLL Name:\s*(\S+)' | ForEach-Object { $_.Matches[0].Groups[1].Value }
$system = '^(kernel32|user32|winmm|psapi|advapi32|ole32|ntdll|api-ms-win-.*)\.dll$'
$foreign = @($imports | Where-Object { $_ -notmatch $system })
if ($foreign.Count) { throw "dinput8.dll imports non-system DLL(s): $($foreign -join ', ')" }
Write-Host "Imports OK: $($imports -join ', ')"

# nvdaControllerClient.dll is REQUIRED for NVDA output: UniversalSpeech
# loads it by that exact (unsuffixed) name and silently falls back to SAPI5
# if it is absent. ZDSRAPI.dll likewise backs the ZDSR reader.
# ⚠ Deploy and Package share this ONE list. Both earlier release zips were
# packed by hand and each was missing something a player needed.
$payload = @('UniversalSpeech.dll',
             'nvdaControllerClient.dll',
             'ZDSRAPI.dll',
             'UniversalSpeech-LICENSE.txt',
             'UniversalSpeech-LICENSE-notice.txt')

if ($Deploy) {
    if (-not (Test-Path $gameDir)) { throw "Game dir not found: $gameDir" }
    Copy-Item "$root\build\dinput8.dll" $gameDir -Force
    foreach ($name in $payload) { Copy-Item "$root\libs\$name" $gameDir -Force }
    Write-Host "Deployed to $gameDir"
}

if ($Package) {
    # A release must be reproducible from a commit, so refuse a dirty tree.
    $dirty = git -C $root status --porcelain
    if ($dirty) { throw "Refusing to package: working tree has uncommitted changes" }
    $commit = git -C $root rev-parse --short HEAD
    $name = "cfc2-access-v$Package"
    $stage = "$root\build\$name"
    $zip = "$root\build\$name.zip"
    if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
    if (Test-Path $zip) { Remove-Item -Force $zip }
    New-Item -ItemType Directory -Path $stage | Out-Null
    Copy-Item "$root\build\dinput8.dll" $stage
    foreach ($f in $payload) { Copy-Item "$root\libs\$f" $stage }
    Copy-Item "$root\README.md" $stage
    Set-Content -Path "$stage\VERSION.txt" -Value "CFC2 Access v$Package, commit $commit"
    Compress-Archive -Path $stage -DestinationPath $zip
    Remove-Item -Recurse -Force $stage
    Write-Host "Packaged $zip (commit $commit)"
}
