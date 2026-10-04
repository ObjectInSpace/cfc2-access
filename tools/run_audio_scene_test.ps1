$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$gcc = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\gcc.exe"
New-Item -ItemType Directory -Force -Path "$root\build\test" | Out-Null
& $gcc -O2 -Wall -Wextra -Werror -DAUDIO_SCENE_TEST -o "$root\build\test\test_audio_scene.exe" `
    "$root\tools\test_audio_scene.c" "$root\src\audio.c" "$root\src\log.c" -lwinmm
if ($LASTEXITCODE -ne 0) { throw 'audio scene test build failed' }
& "$root\build\test\test_audio_scene.exe"
if ($LASTEXITCODE -ne 0) { throw 'audio scene test FAILED' }
