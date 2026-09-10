# Sabotage sweep for src/prompt_id.c.
#
# ⭐⭐ THE NEGATIVES ARE THE POINT. A matcher that returns a plausible label for
# everything passes a suite made of positives, and that is the exact failure
# this project keeps paying for: a screen row added on plausibility spoke
# another screen's labels AND suppressed the discovery block that would have
# exposed it. So each sabotage here widens or loosens the matcher, and the
# suite must catch it.
#
# ⛔ Restore is a git checkout, never a string round-trip -- see
# tools/sabotage_gm.ps1 for what that cost.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$src = "$root\src\prompt_id.c"
$gcc = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\gcc.exe"
$out = "$root\build\test"
New-Item -ItemType Directory -Force -Path $out | Out-Null

$dirty = & git -C $root status --porcelain -- $src
if ($dirty) { throw "src/prompt_id.c has uncommitted changes -- commit first." }
function Restore-Src { & git -C $root checkout -- $src }

$orig = Get-Content -Raw $src

$sabs = @(
  # ⛔ THE BIG ONE: ambiguity resolved to the representative label. This is the
  # confident wrong answer the whole design exists to prevent.
  @{ n = '1. ambiguous returns a label anyway'
     o = '            out.result = PROMPT_ID_AMBIGUOUS;
            out.label  = NULL;
            out.index  = -1;'
     w = '            out.result = PROMPT_ID_FOUND;
            out.label  = PROMPT_ROWS[i].label;
            out.index  = PROMPT_ROWS[i].index;' },

  # ⛔ Substring matching: a truncated capture (the buffer is 256 bytes, so
  # these are REAL) would match a longer entry and name the wrong screen.
  @{ n = '2. substring match instead of exact'
     o = '    if ((ae - a) != (be - b))
        return FALSE;
    return memcmp(a, b, (size_t)(ae - a)) == 0;'
     w = '    if ((ae - a) < (be - b))
        return FALSE;
    return memcmp(a, b, (size_t)(be - b)) == 0;' },

  # ⚠ 3 IS EXPECTED TO PASS, and that is a finding rather than a hole.
  #
  # The runtime blank guard is REDUNDANT while the generator excludes
  # degenerate strings: no table entry has <3 visible characters, so a blank
  # capture matches nothing regardless. Verified directly -- the suite now
  # asserts that property OF THE TABLE, which is what actually protects the
  # user; the guard is defence in depth for a future regeneration that stops
  # excluding them.
  #
  # ⭐ The sweep is what surfaced this: the guard looked load-bearing and was
  # not, and the property that WAS load-bearing had no assertion at all.
  @{ n = '3. blank guard (REDUNDANT)'
     o = '        if (visible < 3)
            return out;'
     w = '        if (0)
            return out;'
     expectPass = $true },

  # The NONE default must survive: returning the first row for an unmatched
  # string is the same lie as #1.
  @{ n = '4. unmatched falls through to row 0'
     o = '    return out;
}'
     w = '    out.result = PROMPT_ID_FOUND;
    out.label = PROMPT_ROWS[0].label;
    out.index = PROMPT_ROWS[0].index;
    return out;
}' }
)

$allCaught = $true
foreach ($s in $sabs) {
    $o = $s.o -replace "`r`n", "`n"
    $body = $orig -replace "`r`n", "`n"
    if (-not $body.Contains($o)) {
        Write-Host ("SETUP FAILED  {0}  (pattern not found)" -f $s.n) -ForegroundColor Magenta
        Restore-Src
        throw "sabotage pattern did not match; aborting rather than mutating further."
    }
    $idx = $body.IndexOf($o)
    $mut = $body.Substring(0, $idx) + ($s.w -replace "`r`n", "`n") + $body.Substring($idx + $o.Length)
    Set-Content -NoNewline -Path $src -Value $mut

    & $gcc -O2 -w -o "$out\sab_pi.exe" "$root\tools\test_prompt_id.c" $src 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) {
        Write-Host ("{0,-38} FAILS (good)  [build broke]" -f $s.n) -ForegroundColor Green
        Restore-Src
        continue
    }
    $res = & "$out\sab_pi.exe" 2>&1
    $code = $LASTEXITCODE
    $cases = ($res | Where-Object { $_ -match '^FAIL' } |
              ForEach-Object { ($_ -replace '^FAIL\s+', '') -split '\s\s+' | Select-Object -First 1 }) -join ', '
    $expectPass = [bool]$s.expectPass
    if ($code -ne 0) {
        if ($expectPass) {
            Write-Host ("{0,-38} FAILS -- but was marked REDUNDANT; re-check the note" -f $s.n) -ForegroundColor Yellow
            $allCaught = $false
        } else {
            Write-Host ("{0,-38} FAILS (good)  cases: {1}" -f $s.n, $cases) -ForegroundColor Green
        }
    } else {
        if ($expectPass) {
            Write-Host ("{0,-38} passes (expected: redundant guard)" -f $s.n) -ForegroundColor DarkGray
        } else {
            Write-Host ("{0,-38} PASSES -- HOLE IN SUITE" -f $s.n) -ForegroundColor Red
            $allCaught = $false
        }
    }
    Restore-Src
}

Restore-Src
& $gcc -O2 -w -o "$out\sab_pi.exe" "$root\tools\test_prompt_id.c" $src 2>&1 | Out-Null
& "$out\sab_pi.exe" | Out-Null
$clean = $LASTEXITCODE
Write-Host ""
Write-Host ("restored: clean suite exit={0}   every sabotage caught: {1}" -f $clean, $allCaught)
if (-not $allCaught -or $clean -ne 0) { exit 1 }
