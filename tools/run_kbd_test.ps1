# The keyboard reader: derived key EDGES, and the install guard.
#
# ⛔⛔ WHY THIS FILE EXISTS. tools/test_kbd_edges.c and tools/test_kbd_guard.c
# were both ORPHANED -- no runner referenced either, so neither was compiled or
# run by the suite sweep. CLAUDE.md even tells the reader to run
# `build\test\test_kbd_guard.exe` "built by the command in tools/", and no such
# command existed anywhere.
#
# ⭐ Both tests encode measured in-game failures, which is what makes their
# absence from the sweep serious rather than untidy:
#   - test_kbd_edges: the engine's own Trg bank is set for ~16 ms while the mod
#     polls at 100 ms, so five of six arrow presses were MISSED. Edges are
#     derived by differencing successive `cur` reads instead, and the sparse
#     case (down and back up between two polls) must be reported unobservable
#     rather than invented.
#   - test_kbd_guard: kbd_install() writes a detour at a hard-coded address, so
#     the dangerous failure is patching something that is NOT the target. The
#     test requires a refusal here (no game is mapped) AND checks WHICH guard
#     refused -- because MinHook independently refuses an unmapped address, so
#     asserting only "returned FALSE" passed with all three identity guards
#     deleted.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$gcc = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\gcc.exe"
if (-not (Test-Path $gcc)) { throw "GCC not found at $gcc" }

$out = "$root\build\test"
New-Item -ItemType Directory -Force -Path $out | Out-Null

$minhookSrc = @(
    "$root\libs\minhook\src\hook.c",
    "$root\libs\minhook\src\buffer.c",
    "$root\libs\minhook\src\trampoline.c",
    "$root\libs\minhook\src\hde\hde64.c"
)

$fail = 0

foreach ($t in @('test_kbd_edges', 'test_kbd_guard')) {
    & $gcc -O2 -Wall -Wextra -Wclobbered -o "$out\$t.exe" `
        "$root\tools\$t.c" `
        "$root\src\kbd_state.c" "$root\src\memsafe.c" "$root\src\log.c" `
        @minhookSrc `
        -I "$root\src" -I "$root\libs\minhook\include" -luser32
    if ($LASTEXITCODE -ne 0) { throw "$t build failed" }

    Write-Output "==== $t ===="
    Push-Location $out
    try { & "$out\$t.exe"; $code = $LASTEXITCODE } finally { Pop-Location }
    if ($code -ne 0) { $fail = 1 }
}

exit $fail
