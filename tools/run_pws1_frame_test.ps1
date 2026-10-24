$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$gcc = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\gcc.exe"
New-Item -ItemType Directory -Force -Path "$root\build\test" | Out-Null
# Replays the live Power Stone fight (tools/make_pws1_fixtures.py) through the
# shipping frame and pickup decoders.
& $gcc -O2 -Wall -Wextra -Werror -o "$root\build\test\test_pws1_frame.exe" `
    "$root\tools\test_pws1_frame.c" "$root\src\pws1_frame.c" "$root\src\match_audio.c" `
    "$root\src\pickup_cues.c" "$root\src\audio.c" "$root\src\log.c" -lwinmm
if ($LASTEXITCODE -ne 0) { throw 'PWS1 frame test build failed' }
Push-Location $root
try {
    & "$root\build\test\test_pws1_frame.exe"
    if ($LASTEXITCODE -ne 0) { throw 'PWS1 frame test FAILED' }
} finally { Pop-Location }
