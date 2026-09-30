$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$gcc = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\gcc.exe"
New-Item -ItemType Directory -Force -Path "$root\build\test" | Out-Null
& python "$root\tools\collection_probe_sites.py" --fixtures "$root\build\test\collection_probe_fixtures.inc"
if ($LASTEXITCODE -ne 0) { throw 'Collection probe fixture generation failed' }
& $gcc -O2 -Wall -Wextra -Wclobbered -Werror -o "$root\build\test\test_collection_probe.exe" `
    "$root\tools\test_collection_probe.c" "$root\src\memsafe.c"
if ($LASTEXITCODE -ne 0) { throw 'Collection probe test build failed' }
& "$root\build\test\test_collection_probe.exe"
if ($LASTEXITCODE -ne 0) { throw 'Collection probe test failed' }
