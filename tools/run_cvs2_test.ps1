$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$gcc = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\gcc.exe"
New-Item -ItemType Directory -Force -Path "$root\build\test" | Out-Null
& $gcc -O2 -Wall -Wextra -Werror -o "$root\build\test\test_cvs2_select.exe" "$root\tools\test_cvs2_select.c" "$root\src\cvs2_select.c" "$root\src\text_channel.c" "$root\src\mod_text.c" `
    "$root\src\selection_channel.c" "$root\src\game_selection.c" "$root\src\game_session.c" `
    "$root\src\cvs2_frame.c" "$root\src\match_audio.c" "$root\src\transition_channel.c"
if ($LASTEXITCODE -ne 0) { throw 'CVS2 test build failed' }
Push-Location $root
try {
    & "$root\build\test\test_cvs2_select.exe"
    if ($LASTEXITCODE -ne 0) { throw 'CVS2 replay test failed' }
} finally { Pop-Location }
