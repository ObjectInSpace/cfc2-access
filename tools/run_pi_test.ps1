# Build and run the prompt-identification self-test.
#
# Purely offline: string comparison against a generated table, no game launch.
# Regenerate the table with tools/gen_prompt_table.py when the corpus changes.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$gcc = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\gcc.exe"
if (-not (Test-Path $gcc)) { throw "GCC not found at $gcc" }

$out = "$root\build\test"
New-Item -ItemType Directory -Force -Path $out | Out-Null

& $gcc -O2 -Wall -Wextra -o "$out\test_prompt_id.exe" `
    "$root\tools\test_prompt_id.c" "$root\src\prompt_id.c"
if ($LASTEXITCODE -ne 0) { throw "test build failed" }

Push-Location $out
try { & "$out\test_prompt_id.exe"; $code = $LASTEXITCODE } finally { Pop-Location }
exit $code
