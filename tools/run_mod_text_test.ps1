$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$gcc = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\gcc.exe"
New-Item -ItemType Directory -Force -Path "$root\build\test" | Out-Null
# The mod's own words in the collection's 14 languages (src/mod_text.c).
& $gcc -O2 -Wall -Wextra -Werror -o "$root\build\test\test_mod_text.exe" "$root\tools\test_mod_text.c"
if ($LASTEXITCODE -ne 0) { throw 'mod_text test build failed' }
Push-Location $root
try {
    & "$root\build\test\test_mod_text.exe"
    if ($LASTEXITCODE -ne 0) { throw 'mod_text test FAILED' }
} finally { Pop-Location }
