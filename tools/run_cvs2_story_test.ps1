$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$gcc = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\gcc.exe"
New-Item -ItemType Directory -Force -Path "$root\build\test" | Out-Null
& $gcc -O2 -Wall -Wextra -Werror -o "$root\build\test\test_cvs2_story.exe" "$root\tools\test_cvs2_story.c" "$root\src\mod_text.c"
if ($LASTEXITCODE -ne 0) { throw 'Story test build failed' }
Push-Location $root
try {
    & "$root\build\test\test_cvs2_story.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Story replay test failed' }
} finally { Pop-Location }
