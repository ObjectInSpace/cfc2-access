$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$gcc = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\gcc.exe"
New-Item -ItemType Directory -Force -Path "$root\build\test" | Out-Null
# CVS1 text-record decoding against the producers' own literals.
& $gcc -O2 -Wall -Wextra -Werror -o "$root\build\test\test_cvs1_text.exe" `
    "$root\tools\test_cvs1_text.c" "$root\src\cvs1_text.c" "$root\src\text_channel.c" `
    "$root\src\text_capture.c" "$root\src\memsafe.c"
if ($LASTEXITCODE -ne 0) { throw 'CVS1 text test build failed' }
Push-Location $root
try {
    & "$root\build\test\test_cvs1_text.exe"
    if ($LASTEXITCODE -ne 0) { throw 'CVS1 text test FAILED' }
} finally { Pop-Location }
