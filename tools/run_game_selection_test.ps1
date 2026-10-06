$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$gcc = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\gcc.exe"
New-Item -ItemType Directory -Force -Path "$root\build\test" | Out-Null
& $gcc -O2 -Wall -Wextra -Werror -o "$root\build\test\test_game_selection.exe" `
    "$root\tools\test_game_selection.c" "$root\src\game_selection.c" "$root\src\game_session.c"
if ($LASTEXITCODE -ne 0) { throw 'Shared selection test build failed' }
& "$root\build\test\test_game_selection.exe"
if ($LASTEXITCODE -ne 0) { throw 'Shared selection test failed' }
