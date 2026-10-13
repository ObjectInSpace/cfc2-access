$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$gcc = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\gcc.exe"
New-Item -ItemType Directory -Force -Path "$root\build\test" | Out-Null
& $gcc -static -O2 -Wall -Wextra -Werror -o "$root\build\test\test_selection_channel.exe" `
    "$root\tools\test_selection_channel.c" "$root\src\selection_channel.c" "$root\src\text_channel.c" "$root\src\game_selection.c" "$root\src\game_session.c"
if ($LASTEXITCODE -ne 0) { throw 'Shared selection test build failed' }
& "$root\build\test\test_selection_channel.exe"
if ($LASTEXITCODE -ne 0) { throw 'Shared selection test failed' }
