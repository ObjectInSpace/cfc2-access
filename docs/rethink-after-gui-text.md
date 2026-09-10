# Rethinking the mod now that live GUI text is readable

Written 2026-09-09, immediately after the `setText` hook started working
(324 calls, 324 strings, 0 faults). This is a design note, not a plan of
record: it separates what the finding actually licenses from what it merely
makes tempting.

## 1. What the finding changes

The mod's speech path was built as a **chain of inference**, because reading the
shell's text was believed impossible:

```
cursor moves -> caller address -> table lookup -> screen identity
             -> base_index + cursor index -> GMD index -> string
```

Every link is a place to be confidently wrong, and this project has been wrong
at almost all of them:

- **caller -> screen**: a row added on plausibility made a different 6-item
  screen speak the main menu's labels, AND suppressed the discovery block that
  would have exposed it.
- **base + index -> label**: assumes items are a contiguous GMD run. False for
  settings screens, where names interleave with their own value labels.
- **the whole chain**: needs a per-screen table entry, so ~14 screens speak
  position only and each new one costs an offline investigation.

Reading the text directly **collapses the chain**. There is no screen table, no
base index, no contiguity assumption, and no per-screen work. The text is the
text.

⭐ That is why this is worth re-examining the architecture over, rather than
bolting on as a fifth data source.

## 2. What the finding does NOT establish

⚠ **The captured strings are not menu item labels.** Measured, not guessed --
from the 21:19 run, all six distinct captures:

```
"<FONT 2>PRESS <SIZE 80><ICON DECIDE></SIZE> TO START</FONT>"
"<SIZE 58><ICON DECIDE></SIZE>Confirm <SIZE 58><ICON CANCEL></SIZE>Back"
"Choose the game you want to play.  ...Training ...Game Settings
 ...Controller Settings ...Start Game ...Back"
"P1"
```

These are **title prompts, button-hint strips and captions**. In the same
seconds, the cursor hook was correctly speaking `"Offline Play, 1 of 6"` and
`"Game, Capcom vs. SNK 2..., 2 of 8"` -- item names that appear in NONE of the
captured strings.

Three further measurements from that run:

- **324 setText calls produced only 4 distinct objects and 6 distinct
  strings.** A per-item label channel would show far more churn.
- **Captures do not track cursor movement.** Three of six coincided with no
  cursor move at all; the strips changed on SCREEN transitions.
- **`cGUIObjMessage` never fired.** If per-item labels flow anywhere, it is not
  through either of its overloads either.

⛔ So the tempting conclusion -- "we can now read menu items directly, delete
the table" -- **is not supported by the evidence**. Acting on it would replace a
narrow, guarded mechanism with a broad one that says the wrong thing, which is
the single worst outcome for a blind user (a wrong name is indistinguishable
from a right one).

## 3. The revised model: two channels, different jobs

The honest reading is that the shell has (at least) two text populations, and
we have just gained access to a different one than we already had.

| | what it answers | how |
|---|---|---|
| cursor + GMD (existing) | "which ITEM am I on?" | index -> label, per-screen table |
| setText (new) | "what does this SCREEN say?" | live string, no table |

⭐ They are complementary, not competing. The new channel is strong exactly
where the old one is weak:

1. **Screen identity.** The blocker on ~14 screens is not reading labels, it is
   knowing WHICH SCREEN the user is on. `"Choose the game you want to play."`
   identifies the Select Game screen outright, with no caller-address table and
   no risk of a plausible-but-wrong match. ⭐⭐ This may be the finding's
   biggest single win, and it is a use the old chain could never serve.
2. **Context the sighted player has and the blind player does not.** The button
   hint strip (`Training / Game Settings / Controller Settings / Start Game /
   Back`) is on screen permanently for a sighted user and currently invisible to
   ours. Speaking it on demand is new capability, not a rewrite.
3. **Screen-change announcements**, since the strips change on transitions.

## 4. What NOT to do

- ⛔ **Do not delete or bypass `g_screens`.** It is verified, guarded, and
  answers a question the new channel has not been shown to answer.
- ⛔ **Do not join setText strings to cursor indices by timing.** Three of six
  captures had no cursor movement in the same second. A timing join would
  fabricate correlations -- the same error shape as the plausibility-based table
  row.
- ⛔ **Do not speak raw strings.** `<FONT 2>`, `<SIZE 58>`, `<ICON DECIDE>` are
  tag soup to a screen reader.
- ⚠ **Do not assume this is all the text.** 324 calls on one overload with only
  4 objects suggests we are seeing one population. Per-item labels may use a
  different mechanism entirely, and "not found yet" is not "does not exist"
  ([[not-in-the-file-is-not-absent]]).

## 5. Proposed order of work

1. **Markup stripper + ICON mapping** (offline, testable). `<ICON DECIDE>` ->
   the confirm button's name. 132 ICON tokens are already catalogued in the GMD
   corpus. Pure string handling with a real test suite.
2. **Screen identification from prompt text** (highest value). Match a captured
   strip against known prompts to name the screen -- replacing caller-address
   guessing for screens that have no table row. Must carry the same
   "verified or silent" discipline as `g_screens`.
3. **A hotkey to speak the current screen's context strip.** Small, safe,
   immediately useful, and it does not touch the item-speech path.
4. **THEN, and only with evidence: find where per-item labels are set.** The
   next run should record, for one screen, every setText call with its object
   pointer while the cursor moves. If item labels never appear, they use another
   path and the existing chain stays. ⭐ That is a measurement, not an
   assumption, and it is the question this note deliberately leaves open.

## 6. The through-line

The project's repeated failure mode is **acting on a plausible reading before
measuring it** -- the base+index assumption, the plausible table row, eight
wrong hook targets, a synchronisation pattern copied across a 65x frequency
change. The finding is genuinely significant. That is exactly the moment to
apply the rule the last two days paid for:

⭐⭐ **When a derived belief and a measurement disagree, the measurement wins --
and when there is no measurement yet, build the probe before the feature.**
