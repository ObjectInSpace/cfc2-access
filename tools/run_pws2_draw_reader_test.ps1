$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$gcc = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\gcc.exe"
New-Item -ItemType Directory -Force -Path "$root\build\test" | Out-Null
& $gcc -O2 -Wall -Wextra -Werror -o "$root\build\test\test_pws2_draw_reader.exe" `
    "$root\tools\test_pws2_draw_reader.c" "$root\src\pws2_draw_reader.c" "$root\src\memsafe.c"
if ($LASTEXITCODE -ne 0) { throw 'PWS2 native reader build failed' }
& "$root\build\test\test_pws2_draw_reader.exe"
if ($LASTEXITCODE -ne 0) { throw 'PWS2 native reader test failed' }
& $gcc -O2 -Wall -Wextra -Werror -o "$root\build\test\test_pws2_draw_batches.exe" `
    "$root\tools\test_pws2_draw_batches.c" "$root\src\pws2_draw_batches.c"
if ($LASTEXITCODE -ne 0) { throw 'PWS2 batch test build failed' }
& "$root\build\test\test_pws2_draw_batches.exe"
if ($LASTEXITCODE -ne 0) { throw 'PWS2 batch test failed' }
