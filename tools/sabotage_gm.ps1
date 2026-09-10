# Sabotage sweep for src/gui_markup.c.
#
# ⭐⭐ A passing test that has never been seen to FAIL is not evidence. This
# breaks each mechanism in turn and requires the suite to fail -- and records
# WHICH case failed, so a sabotage that trips some unrelated assertion is not
# mistaken for coverage.
#
# ⛔ This project has already shipped two suites that passed with the mechanism
# under test deleted. Both were caught by exactly this sweep.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$src = "$root\src\gui_markup.c"
$gcc = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\gcc.exe"
$out = "$root\build\test"
New-Item -ItemType Directory -Force -Path $out | Out-Null

$orig = Get-Content -Raw $src

$sabs = @(
  @{ n = '1. stutter removal deleted'
     o = '                if (starts_with_word(action, action_len, buttons)) {'
     w = '                if (0) {' },
  # ⭐ "has a space" is NOT "is a combination": single icons with multi-word
  # names ("special move", "Any Attack") were skipping the stutter check and
  # speaking twice. Sabotaging the count back to a word test must fail.
  @{ n = '1b. multi-word name = combination'
     o = 'action_len > 0 && button_count == 1'
     w = "action_len > 0 && strchr(buttons, ' ') == NULL" },
  # ⭐ De-duplication, not deletion: when the icon and the printed label are
  # the same, the LABEL is kept. Stripping the action instead deleted it.
  @{ n = '1c. identical label deleted'
     o = '                        buttons[0] = ''\0'';'
     w = '                        action_len = 0;' },
  @{ n = '2. stutter by EQUALITY only'
     o = '    return (alen == nlen) || isspace((unsigned char)action[nlen]);'
     w = '    return (alen == nlen);' },
  @{ n = '3. pair separator removed'
     o = 'sink_str(&s, "; ");'
     w = 'sink_str(&s, " ");' },
  @{ n = '4. either-of/chord = pair boundary'
     o = "if (tl == 1 && (t[0] == '/' || t[0] == '+') &&"
     w = "if (0 && (t[0] == '/' || t[0] == '+') &&" },
  @{ n = '5. unterminated tag falls to prose'
     o = '            return n;'
     w = '            break;' },
  @{ n = '6. unknown token dropped silently'
     o = '                    out[n].icon = out[n].icon_tmp;'
     w = '                    out[n].icon_tmp[0] = 0; out[n].icon = out[n].icon_tmp;' },
  # ⭐ THE CLAMP is the load-bearing bound, not the early return.
  #
  # The first version of this sabotage disabled `if (s->len >= s->cap - 1)` and
  # the suite PASSED -- because `if (n > room) n = room` two lines below still
  # truncated every write. Two independent guards, and I sabotaged the
  # redundant one, then briefly read the green result as "the test is weak".
  # Both are now sabotaged separately, so each is shown to be doing something.
  @{ n = '7a. length CLAMP removed (overrun)'
     o = '    if (n > room)'
     w = '    if (0)' },
  # ⚠ 7b IS EXPECTED TO PASS, and that is a finding, not a hole.
  #
  # The early return is REDUNDANT: measured with tools/probe_bound.c across
  # cap = 1..8 -- including cap=1, where `room` becomes 0 -- behaviour is
  # byte-identical with and without it. The clamp alone keeps every write in
  # bounds, NUL-terminates, and never touches the canary.
  #
  # ⭐ Recorded rather than papered over. Deleting it would be safe today, but
  # it documents the invariant at the top of the function and costs one
  # compare. What matters is that the suite's "bounded" claim rests on 7a, the
  # guard that is actually load-bearing -- not on this one.
  @{ n = '7b. early-return guard (REDUNDANT)'
     o = '    if (s->len >= s->cap - 1)'
     w = '    if (0)'
     expectPass = $true },
  @{ n = '8. KB_ mechanical rule deleted'
     o = '    if (strncmp(token, "KB_", 3) == 0 && token[3] != ''\0'') {'
     w = '    if (0) {' }
)

# ⛔⛔ RESTORE FROM GIT, NEVER FROM A STRING ROUND-TRIP.
#
# The first version of this script kept the original in a variable and wrote it
# back at the end. A pattern that failed to match left the file MUTATED, a
# later sabotage's replacement then matched the mutation, and the "restored"
# source still carried `if (0) {` in the stutter branch -- a silently disabled
# mechanism in the shipping file, which is far worse than the missing coverage
# it was trying to measure. It was caught only because the final clean run
# exited 1.
#
# So: the file must be committed before this runs, every restore is a git
# checkout, and a failed pattern ABORTS rather than continuing to mutate.
$dirty = & git -C $root status --porcelain -- $src
if ($dirty) {
    throw "src/gui_markup.c has uncommitted changes -- commit first, since every restore here is a git checkout."
}
function Restore-Src { & git -C $root checkout -- $src }

$allCaught = $true
foreach ($s in $sabs) {
    if (-not $orig.Contains($s.o)) {
        Write-Host ("SETUP FAILED  {0}  (pattern not found)" -f $s.n) -ForegroundColor Magenta
        Restore-Src
        throw "sabotage pattern did not match; aborting rather than mutating further."
    }
    $idx = $orig.IndexOf($s.o)
    $mut = $orig.Substring(0, $idx) + $s.w + $orig.Substring($idx + $s.o.Length)
    Set-Content -NoNewline -Path $src -Value $mut

    $exe = "$out\sab_gm.exe"
    & $gcc -O2 -w -o $exe "$root\tools\test_gui_markup.c" $src 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) {
        Write-Host ("{0,-38} FAILS (good)  [build broke]" -f $s.n) -ForegroundColor Green
        Restore-Src
        continue
    }
    $res = & $exe 2>&1
    $code = $LASTEXITCODE
    $cases = ($res | Where-Object { $_ -match '^FAIL' } |
              ForEach-Object { ($_ -replace '^FAIL\s+', '') -split '\s\s+' | Select-Object -First 1 }) -join ', '

    # ⭐ THE PARITY CHECK IS PART OF THE SUITE HERE.
    #
    # Several mechanisms (the button COUNT test, de-duplication vs deletion)
    # are pinned by nothing in the 18 hand-picked cases -- they were found by
    # comparing against the Python oracle over the whole corpus in the first
    # place. Without running parity, sabotaging them "passes" and looks like a
    # hole that is really just a check running in the wrong place.
    if ($code -eq 0) {
        python "$root\tools\check_markup_parity.py" 2>&1 | Out-Null
        if ($LASTEXITCODE -ne 0) {
            $code = 1
            $cases = 'parity vs the Python oracle'
        }
    }
    $expectPass = [bool]$s.expectPass
    if ($code -ne 0) {
        if ($expectPass) {
            # A guard documented as redundant that now MATTERS: the code moved
            # underneath the note, so the note must be re-derived.
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
& $gcc -O2 -w -o "$out\sab_gm.exe" "$root\tools\test_gui_markup.c" $src 2>&1 | Out-Null
& "$out\sab_gm.exe" | Out-Null
$clean = $LASTEXITCODE
Write-Host ""
Write-Host ("restored: clean suite exit={0}   every sabotage caught: {1}" -f $clean, $allCaught)
if (-not $allCaught -or $clean -ne 0) { exit 1 }
