$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$gcc = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\gcc.exe"
New-Item -ItemType Directory -Force -Path "$root\build\test" | Out-Null
# The positional ping scheduler with the mixer stubbed, then the mixer's ping
# voices rendered offline.
& $gcc -O2 -Wall -Wextra -Werror -o "$root\build\test\test_pickup_cues.exe" `
    "$root\tools\test_pickup_cues.c" "$root\src\pickup_cues.c"
if ($LASTEXITCODE -ne 0) { throw 'pickup cue test build failed' }
& $gcc -O2 -Wall -Wextra -Werror -o "$root\build\test\test_audio_ping.exe" `
    "$root\tools\test_audio_ping.c" "$root\src\audio.c" "$root\src\log.c" -lwinmm
if ($LASTEXITCODE -ne 0) { throw 'audio ping test build failed' }
Push-Location $root
try {
    & "$root\build\test\test_pickup_cues.exe"
    if ($LASTEXITCODE -ne 0) { throw 'pickup cue test FAILED' }
    & "$root\build\test\test_audio_ping.exe"
    if ($LASTEXITCODE -ne 0) { throw 'audio ping test FAILED' }
} finally { Pop-Location }
