$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$gcc = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\gcc.exe"
New-Item -ItemType Directory -Force -Path "$root\build\test" | Out-Null
# Replays the Project Justice story capture's fights (tools/make_mjus_fixtures.py)
# through the shipping frame decoder.
& $gcc -O2 -Wall -Wextra -Werror -o "$root\build\test\test_mjus_frame.exe" `
    "$root\tools\test_mjus_frame.c" "$root\src\mjus_frame.c" "$root\src\match_audio.c" `
    "$root\src\audio.c" "$root\src\log.c" -lwinmm
if ($LASTEXITCODE -ne 0) { throw 'Project Justice frame test build failed' }
Push-Location $root
try {
    & "$root\build\test\test_mjus_frame.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Project Justice frame test FAILED' }
} finally { Pop-Location }
