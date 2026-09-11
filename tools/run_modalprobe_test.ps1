# gt_modal_probe(): does it find the shared parent and REPORT what changed?
#
# ⛔ Built because three tests in one session passed while the feature did
# nothing. This plants two "Yes"/"No" objects sharing a +0x088 parent -- the
# real dialog's shape, measured -- changes ONE dword in that parent, and
# requires the probe to report THAT offset. The assertion reads the probe's own
# log output; nothing in the harness re-derives the answer.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$gcc = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\gcc.exe"
if (-not (Test-Path $gcc)) { throw "GCC not found at $gcc" }
$out = "$root\build\test"
New-Item -ItemType Directory -Force $out | Out-Null
$mh = Get-ChildItem "$root\libs\minhook\src" -Recurse -Filter *.c | ForEach-Object { $_.FullName }

& $gcc -O2 -Wall -o "$out\t_modalprobe2.exe" `
    "$root\tools\t_modalprobe2.c" "$root\src\gui_text.c" `
    "$root\src\log.c" "$root\src\memsafe.c" `
    @mh -I "$root\libs\minhook\include" -luser32
if ($LASTEXITCODE -ne 0) { throw "test build failed" }

Push-Location $out
try { & "$out\t_modalprobe2.exe"; $code = $LASTEXITCODE } finally { Pop-Location }
exit $code
