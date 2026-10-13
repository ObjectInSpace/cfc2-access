$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$gcc = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\gcc.exe"
New-Item -ItemType Directory -Force -Path "$root\build\test" | Out-Null
# Replays the ten labelled CVS1 match snapshots (tools/make_cvs1_frame_fixture.py)
# through the shipping frame decoder.
& $gcc -O2 -Wall -Wextra -Werror -o "$root\build\test\test_cvs1_frame.exe" `
    "$root\tools\test_cvs1_frame.c" "$root\src\cvs1_frame.c" "$root\src\match_audio.c" `
    "$root\src\audio.c" "$root\src\log.c" -lwinmm
if ($LASTEXITCODE -ne 0) { throw 'CVS1 frame test build failed' }
Push-Location $root
try {
    & "$root\build\test\test_cvs1_frame.exe"
    if ($LASTEXITCODE -ne 0) { throw 'CVS1 frame test FAILED' }
} finally { Pop-Location }
