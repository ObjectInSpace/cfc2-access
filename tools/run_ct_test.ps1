# Build and run the caller-trace self-test.
#
# The test needs TWO modules: the test exe (standing in for our dinput8.dll)
# and test_ct_helper.dll (standing in for the game exe). A single-module test
# cannot exercise the self-filter at all -- see the header of tools/test_ct.c.
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

& $gcc -shared -O2 -Wall -Wextra -o "$out\test_ct_helper.dll" `
    "$root\tools\test_ct_helper.c" -luser32
if ($LASTEXITCODE -ne 0) { throw "helper build failed" }

& $gcc -O2 -Wall -Wextra -o "$out\test_ct.exe" `
    "$root\tools\test_ct.c" "$root\src\caller_trace.c" "$root\src\log.c" `
    "$root\src\memsafe.c" `
    @minhookSrc `
    -I "$root\libs\minhook\include" `
    -luser32 -lpsapi
if ($LASTEXITCODE -ne 0) { throw "test build failed" }

Push-Location $out
try { & "$out\test_ct.exe"; $code = $LASTEXITCODE } finally { Pop-Location }
exit $code
