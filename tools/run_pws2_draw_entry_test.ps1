$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$gcc = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\gcc.exe"
New-Item -ItemType Directory -Force -Path "$root\build\test" | Out-Null
& $gcc -O2 -Wall -Wextra -Werror -c "$root\src\pws2_draw_entry.c" `
    -o "$root\build\test\pws2_draw_entry_production.o"
if ($LASTEXITCODE -ne 0) { throw 'PWS2 production call-site filter compile failed' }
& $gcc -O2 -Wall -Wextra -Werror -DPWS2_DRAW_ENTRY_TEST -o "$root\build\test\test_pws2_draw_entry.exe" `
    "$root\tools\test_pws2_draw_entry.c" "$root\tools\test_pws2_draw_entry.S" `
    "$root\src\pws2_draw_entry.c" "$root\src\pws2_draw_entry.S"
if ($LASTEXITCODE -ne 0) { throw 'PWS2 draw entry ABI test build failed' }
& "$root\build\test\test_pws2_draw_entry.exe"
if ($LASTEXITCODE -ne 0) { throw 'PWS2 draw entry ABI test failed' }
