$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$gcc = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\gcc.exe"
New-Item -ItemType Directory -Force -Path "$root\build\test" | Out-Null

# The suite drives the mixer through audio_render_block_for_test(), so no
# audio device is opened and nothing is audible. Every case asserts on
# SAMPLES; four mechanisms were each sabotaged and seen to fail the case
# that tests them (see the comments in tools/test_audio.c).
& $gcc -O2 -Wall -Wextra -Werror -o "$root\build\test\test_audio.exe" `
    "$root\tools\test_audio.c" "$root\src\audio.c" "$root\src\log.c" -lwinmm
if ($LASTEXITCODE -ne 0) { throw "audio test build failed" }

& "$root\build\test\test_audio.exe"
if ($LASTEXITCODE -ne 0) { throw "audio test FAILED" }
