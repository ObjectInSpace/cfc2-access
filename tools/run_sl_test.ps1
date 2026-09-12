# Build and run the speak-line conversion self-test.
#
# ⭐⭐ This exists because a wchar_t[256] SILENCED THE ENTIRE MOVE LIST while
# every other suite stayed green: the panel was built correctly and discarded at
# the last step. Nothing offline covered menu_speak_line(), so nothing could
# fail. See tools/test_speak_line.c.
#
# Purely offline: Win32 string conversion only, no game launch, no game memory.
#
# ⚠ Run this through PowerShell directly -- via some shells a crashing suite
# has reported exit 0 with no output on this project.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$gcc = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\gcc.exe"
if (-not (Test-Path $gcc)) { throw "GCC not found at $gcc" }

$out = "$root\build\test"
New-Item -ItemType Directory -Force -Path $out | Out-Null

& $gcc -O2 -Wall -Wextra -Wclobbered -o "$out\test_speak_line.exe" `
    "$root\tools\test_speak_line.c"
if ($LASTEXITCODE -ne 0) { throw "test build failed" }

Push-Location $out
try {
    & "$out\test_speak_line.exe"
    $code = $LASTEXITCODE
} finally {
    Pop-Location
}
exit $code
