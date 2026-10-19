$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$gcc = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\gcc.exe"
New-Item -ItemType Directory -Force -Path "$root\build\test" | Out-Null
# Project Justice immediate-draw text decoding against the game's own literals.
& $gcc -O2 -Wall -Wextra -Werror -o "$root\build\test\test_mjus_text.exe" `
    "$root\tools\test_mjus_text.c" "$root\src\mjus_text.c" "$root\src\game_text.c" "$root\src\text_channel.c" "$root\src\mod_text.c" `
    "$root\src\text_capture.c" "$root\src\memsafe.c"
if ($LASTEXITCODE -ne 0) { throw 'Project Justice text test build failed' }
Push-Location $root
try {
    & "$root\build\test\test_mjus_text.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Project Justice text test FAILED' }
} finally { Pop-Location }
