$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$gcc = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\gcc.exe"
New-Item -ItemType Directory -Force -Path "$root\build\test" | Out-Null

# Shared transition announcements through the real text_channel/tc_say, with
# speech mocked. Sabotages: python tools\sabotage_transition.py
& $gcc -O2 -Wall -Wextra -Werror -o "$root\build\test\test_transition.exe" `
    "$root\tools\test_transition.c" "$root\src\transition_channel.c" "$root\src\text_channel.c"
if ($LASTEXITCODE -ne 0) { throw 'transition test build failed' }
& "$root\build\test\test_transition.exe"
if ($LASTEXITCODE -ne 0) { throw 'transition test FAILED' }
