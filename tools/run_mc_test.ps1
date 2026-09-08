# Build and run the menu-cursor self-test.
#
# Purely offline: it drives the shipping detour body with synthetic arguments,
# so no game launch is spent on it. See tools/test_menu_cursor.c.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$gcc = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\gcc.exe"
if (-not (Test-Path $gcc)) { throw "GCC not found at $gcc" }

$out = "$root\build\test"
New-Item -ItemType Directory -Force -Path $out | Out-Null

$minhookSrc = @(
    "$root\libs\minhook\src\hook.c",
    "$root\libs\minhook\src\buffer.c",
    "$root\libs\minhook\src\trampoline.c",
    "$root\libs\minhook\src\hde\hde64.c"
)

& $gcc -O2 -Wall -Wextra -Wclobbered -o "$out\test_menu_cursor.exe" `
    "$root\tools\test_menu_cursor.c" "$root\src\menu_cursor.c" `
    "$root\src\log.c" "$root\src\memsafe.c" `
    @minhookSrc `
    -I "$root\libs\minhook\include" `
    -luser32
if ($LASTEXITCODE -ne 0) { throw "test build failed" }

Push-Location $out
try { & "$out\test_menu_cursor.exe"; $code = $LASTEXITCODE } finally { Pop-Location }
exit $code
