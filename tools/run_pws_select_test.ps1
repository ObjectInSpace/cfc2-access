$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$gcc = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\gcc.exe"
New-Item -ItemType Directory -Force -Path "$root\build\test" | Out-Null
& $gcc -O2 -Wall -Wextra -Werror -static -o "$root\build\test\test_pws_select.exe" `
    "$root\tools\test_pws_select.c" "$root\src\pws_select_decode.c" `
    "$root\src\game_selection.c" "$root\src\game_session.c"
if ($LASTEXITCODE -ne 0) { throw 'Power Stone selection test build failed' }
& "$root\build\test\test_pws_select.exe"
if ($LASTEXITCODE -ne 0) { throw 'Power Stone selection test failed' }
