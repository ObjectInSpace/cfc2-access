$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$gcc = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\gcc.exe"
New-Item -ItemType Directory -Force -Path "$root\build\test" | Out-Null
# The generated story table must match data/mjus_story/.
& python "$root\tools\make_mjus_story.py" --check
if ($LASTEXITCODE -ne 0) { throw 'mjus_story_text.h is stale' }
# Project Justice story dialogue: the on-screen line and its transcription.
& $gcc -O2 -Wall -Wextra -Werror -o "$root\build\test\test_mjus_story.exe" `
    "$root\tools\test_mjus_story.c" "$root\src\mjus_story.c" "$root\src\text_channel.c" `
    "$root\src\text_capture.c" "$root\src\memsafe.c" "$root\src\mod_text.c"
if ($LASTEXITCODE -ne 0) { throw 'Project Justice story test build failed' }
Push-Location $root
try {
    & "$root\build\test\test_mjus_story.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Project Justice story test FAILED' }
} finally { Pop-Location }
