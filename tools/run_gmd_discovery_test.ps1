$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$gcc = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\gcc.exe"
New-Item -ItemType Directory -Force -Path "$root\build\test" | Out-Null
& $gcc -O2 -Wall -Wextra -Wclobbered -Werror -Wno-error=clobbered -o "$root\build\test\test_gmd_discovery.exe" `
    "$root\tools\test_gmd_discovery.c" "$root\src\memsafe.c" -I "$root\src" -lpsapi
if ($LASTEXITCODE -ne 0) { throw 'Discovery test build failed' }
& "$root\build\test\test_gmd_discovery.exe"
if ($LASTEXITCODE -ne 0) { throw 'Discovery test failed' }
