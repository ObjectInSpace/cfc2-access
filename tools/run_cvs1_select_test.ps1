$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$gcc = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\gcc.exe"
New-Item -ItemType Directory -Force -Path "$root\build\test" | Out-Null
# Replays the retained live CVS1 capture (tools/make_cvs1_select_fixture.py)
# through the shipping decoder, the generated name table and the shared channel.
& $gcc -O2 -Wall -Wextra -Werror -o "$root\build\test\test_cvs1_select.exe" `
    "$root\tools\test_cvs1_select.c" "$root\src\cvs1_select.c" "$root\src\selection_channel.c" `
    "$root\src\game_selection.c" "$root\src\game_session.c" "$root\src\text_channel.c" "$root\src\mod_text.c"
if ($LASTEXITCODE -ne 0) { throw 'CVS1 select test build failed' }
Push-Location $root
try {
    & "$root\build\test\test_cvs1_select.exe"
    if ($LASTEXITCODE -ne 0) { throw 'CVS1 select test FAILED' }
} finally { Pop-Location }
