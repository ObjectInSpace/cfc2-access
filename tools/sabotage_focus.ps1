# Sabotage sweep for the focus channel in src/gui_text.c (items 3 + 4).
#
# ⭐⭐ THE ONE THAT MATTERS IS THE STALE CAPTION. Speaking a description that
# belongs to a DIFFERENT row is indistinguishable from a correct one to a blind
# user, and it is the failure this whole channel is shaped to prevent. If the
# move-sequence stamp can be removed and the suite still passes, the suite is
# not protecting the user.
#
# ⛔ Restore is a git checkout, never a string round-trip (see sabotage_gm.ps1).
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$src = "$root\src\gui_text.c"
$gcc = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\gcc.exe"

$dirty = & git -C $root status --porcelain -- $src
if ($dirty) { throw "src/gui_text.c has uncommitted changes -- commit first." }
function Restore-Src { & git -C $root checkout -- $src }

$orig = (Get-Content -Raw $src) -replace "`r`n", "`n"

$sabs = @(
  # ⛔⛔ THE BIG ONE: ignore the move stamp and hand back the newest string.
  # That is "the most recent text", i.e. the timing join the rethink note
  # forbids -- and it speaks the previous row's caption for the current row.
  @{ n = '1. stale caption: ignore the move stamp'
     o = '            if (g_focus[i].move_seq != seq)
                break;              /* records are appended in order */'
     w = '            if (0)
                break;' },

  # A hint strip spoken as a row caption = constant noise on every movement.
  @{ n = '2. hint strips returned as captions'
     o = "            if (strchr(g_focus[i].text, '<') == NULL) {"
     w = '            if (1) {' },

  # Records must be appended in order for "newest of this move" to mean
  # anything; searching forward returns the OLDEST instead.
  @{ n = '3. oldest of the move wins, not newest'
     o = '        for (LONG i = g_focus_count - 1; i >= 0; i--) {'
     w = '        for (LONG i = 0; i < g_focus_count; i++) {' },

  # The recorder must actually stamp the current sequence.
  @{ n = '4. records stamped with a constant'
     o = '                            g_focus[n].move_seq = g_move_seq;'
     w = '                            g_focus[n].move_seq = 0;' },

  # Liveness: without the notification count, "no captions" cannot be told
  # from "the cursor side never called us".
  @{ n = '5. move notifications never counted'
     o = '    InterlockedIncrement(&g_move_notifications);'
     w = '    ;' }
)

$allCaught = $true
foreach ($s in $sabs) {
    $o = $s.o -replace "`r`n", "`n"
    if (-not $orig.Contains($o)) {
        Write-Host ("SETUP FAILED  {0}  (pattern not found)" -f $s.n) -ForegroundColor Magenta
        Restore-Src
        throw "sabotage pattern did not match; aborting rather than mutating further."
    }
    $idx = $orig.IndexOf($o)
    $mut = $orig.Substring(0, $idx) + ($s.w -replace "`r`n", "`n") + $orig.Substring($idx + $o.Length)
    Set-Content -NoNewline -Path $src -Value $mut

    $res = & "$root\tools\run_gt_test.ps1" 2>&1
    $code = $LASTEXITCODE
    $cases = ($res | Where-Object { $_ -match '^\s*FAIL' } |
              ForEach-Object { ($_ -replace '^\s*FAIL\s+', '') -split ':' | Select-Object -First 1 }) -join ', '
    if ($code -ne 0) {
        Write-Host ("{0,-42} FAILS (good)  {1}" -f $s.n, $cases) -ForegroundColor Green
    } else {
        Write-Host ("{0,-42} PASSES -- HOLE IN SUITE" -f $s.n) -ForegroundColor Red
        $allCaught = $false
    }
    Restore-Src
}

Restore-Src
& "$root\tools\run_gt_test.ps1" | Out-Null
$clean = $LASTEXITCODE
Write-Host ""
Write-Host ("restored: clean suite exit={0}   every sabotage caught: {1}" -f $clean, $allCaught)
if (-not $allCaught -or $clean -ne 0) { exit 1 }
