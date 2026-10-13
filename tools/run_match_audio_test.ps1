$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$gcc = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\gcc.exe"
New-Item -ItemType Directory -Force -Path "$root\build\test" | Out-Null

# The shared match-audio controller against the REAL mixer, rendered through
# audio_render_block_for_test(). No device is opened; every case asserts on
# samples. Sabotages are listed at the top of tools/test_match_audio.c.
& $gcc -O2 -Wall -Wextra -Werror -DAUDIO_OWNER_TEST -o "$root\build\test\test_match_audio.exe" `
    "$root\tools\test_match_audio.c" "$root\src\match_audio.c" "$root\src\audio.c" `
    "$root\src\log.c" -lwinmm
if ($LASTEXITCODE -ne 0) { throw 'match audio test build failed' }
& "$root\build\test\test_match_audio.exe"
if ($LASTEXITCODE -ne 0) { throw 'match audio test FAILED' }
