# Durable naming: the discriminator, 14 languages, and retiring weak methods

Written 2026-09-11 after Training Options shipped. Three coupled problems, in
the order their evidence supports. **Nothing here is implemented.**

⚠ Written as a plan rather than started as code because three errors shipped
into this session's work already — wrong GMD indices, a deploy before the
suites ran, and seven phantom test failures reported to the user. Each came
from moving before checking. The cost of a plan is one turn.

---

## ⛔⛔ RETRACTED: "the caption channel is dead in most sessions"

**The first version of this file led with a wrong measurement, and it was mine.**
It claimed 26 non-English sessions against 11 English, from log lines reading
`matched shipped variant 2 of 4, a non-English locale`.

⛔ Those lines do NOT report the session's language. They come from
`verify_text_variant()` (`src/gmd_find.c:793-801`) and say which of a PROBE
STRING's four shipped variants matched while verifying the table — a detail of
table verification, not a language census. `gmd_probes.h:125` holds that
4-variant list.

⭐ The lines that actually report language say
`Table language: eng (matches config.ini)`, and they say **eng in 38 of 38
archived runs**. `config.ini` in the game directory really does hold
`Language=eng`. So language selection has been working, and the caption channel
has NOT been silently failing.

⚠ Generalisable: I read a verification detail as a measurement, then made it the
headline of a plan. The check that would have caught it is the one this project
already uses everywhere else — ask what PRODUCED the line before counting it.

## The REAL language gap, measured

Three separate English-only dependencies, none of which has bitten yet because
every observed session was English:

1. **`g_lang_by_idx0` lists 5 of 14 languages** (`src/gmd_find.c:1350`): eng,
   fre, ita, ger, pol. Its own comment says so — "Only the ASCII-renderable
   locales are listed." `gmd_text_language()` returns NULL for the other nine,
   so the live table cannot be IDENTIFIED, and therefore cannot be matched
   against the player's setting. The fallback logs "item names will be in the
   WRONG LANGUAGE" and serves the first blob it recognises.
2. **`prompt_table.h` is generated from `menu_eng` alone**
   (`tools/gen_prompt_table.py:89`), and `prompt_identify()` compares captured
   text with `memcmp` after trimming (`src/prompt_id.c:20-37`). Exact bytes, so
   every caption misses in the other 13 languages.
3. **`gui_text.c:1574` and `:1633` anchor on the literal `"Yes"`** to find the
   modal dialog's parent. In any non-English session the quit dialog loses its
   selection readout entirely.

## ⭐⭐ TWO FINDINGS THAT MAKE THIS CHEAP

**All 14 tables are structurally identical**: 4165 labels, same set, same order
in every one (measured). So **GMD indices are portable across languages** —
row and value maps need NO per-language work, and a per-language prompt table
is a pure text swap with no index remapping.

**Index 0 self-identifies 13 of 14 uniquely** (`LANG_NAME_JAPANESE` in each
table's own language):

    jpn 日本語 [jp]       eng Japanese [jp]      fre Japonais [jp]
    ita Giapponese [jp]   ger Japanisch [jp]     spa Japonés [jp]
    oth Japonés [jp]  ← COLLIDES with spa       kor 일본어 [jp]
    pol Japoński [jp]     por Japonês [jp]       rus Японский [jp]
    chT 日文[jp]          chS 日语               ara اليابانية [jp]

⭐ **The collision is real and must be resolved, not deferred**: `menu_oth` is
Spanish (Mexico) (`tools/extract_gmd.py:32` maps it to `spanish_mx`), and
`LANG_NAME_SPANISH2` = "Spanish (Mexico) [es-MX]" is one of the 14 options the
Language row offers. A player can select it.

⭐ **The tiebreaker is affordable.** spa and oth differ at **813 of 4165**
labels, the earliest at index 29 (`SYS_WINDOW_YES2`: `Aceptar` vs
`Sí, acepto`). `skip_strings()` already walks N NUL-terminated strings under a
guard, so reaching index 29 is 29 short hops — NOT the 4165-entry index build
that `read_index0` exists to avoid. The 12-second-stall rule is not violated by
a bounded walk.

---

## What is ALREADY language-safe, and must not be "fixed"

**Row and value maps are GMD INDICES**, and indices are language-independent.
`gmd_find` resolves the LIVE table and asks it which language it is
(`gmd_text_language()`, built after the stale-French bug where the mod spoke
French in an English session). `gmd_text_by_index()` then returns whatever the
live table holds.

So `g_training_option_rows = {683, 694, 700, 705, 2960, 168}` speaks Italian in
an Italian session with no extra work. ⛔ Do not add per-language row maps;
the indices already are the abstraction.

---

## 1. THE SAME-CALLER DISCRIMINATOR (option C)

### The problem, measured
One caller serves several screens, now **four** times:
`0x14009ba21` (Select Mode 3 / Online Play 6), `0x14009890d` (Casual 4 /
Ranked 3), `0x1400a749d` (pause 8 / training pause 7), and the new case:

    0x1400a749d count=8  arcade   rows 6/7 = Load Quick Save / Quick Save
    0x1400a749d count=8  training rows 6/7 = Training Menu / Change Character

`menu_speak_lookup_n(caller, count)` cannot separate the last pair — same
caller, same count — so it picks the arcade map and speaks two wrong rows.
USER-REPORTED, and I twice told the user the log showed otherwise.

### ⛔ Why the mode strip (option B) does NOT work
Run 1605 has **zero** `0x1400b2729` movements and shows the pause menu at
count=7 AND count=8. The strip only logs when the cursor MOVES, so a user
accepting the default mode emits no signal at all. The signal is frequently
**absent**, not merely noisy.

⚠ My first explanation to the user was weaker and wrong ("all three runs ended
on Training Mode while shapes varied"). The absence is the real reason.

### ▶ The design: a per-screen EXPECTED LABEL at a known row
Add to `mc_screen` an optional discriminator: `{row, gmd_index}`. When two
entries share (caller, count), the live text at that row decides.

    arcade   pause: row 5 must read GMD 254 ("Load Quick Save")
    training pause: row 5 must read GMD 756 ("Training Menu")

⭐ **Compare against `gmd_text_by_index()` of the LIVE table, never a literal
string.** That keeps the discriminator language-independent by construction —
the same property that makes the row maps work — instead of adding a second
English-only dependency to fix the first.

⚠ Needs a new reader: `gt_item_at_position()` answers "what text is at position
N" for MC_COORD_LIVE, but the discriminator must ask "what is at row R of THIS
screen" before a kind is chosen. Either reuse the coordinate ordering with the
screen's own count, or read the row label object directly.

⛔ If the discriminator cannot be read, the answer is REFUSE (speak position),
never fall through to the first matching entry. A wrong row name is worse than
a bare position — that is the whole premise of this file.

### Evidence needed before shipping
One run pausing in BOTH arcade and training on the same game, with
`cfc2_layout.on`, so both 8-row variants are captured with coordinates.

---

## 2. FOURTEEN LANGUAGES FOR THE PROMPT TABLE

`msg.arc` ships 14 tables: `menu_jpn eng fre ita ger spa oth kor pol por rus
chT chS ara`.

### Two options, and the second is better
**(a) Emit all 14 into `prompt_table.h`.** Simple, mechanical, ~14x the table.
Still exact-text matching, still breaks if a table is missing.

**(b) ⭐ Key captions by INDEX, not text.** `prompt_identify()` exists to answer
"which LABEL produced this string?". If the live table is resolved, the mod can
ask the *live* table directly rather than a baked English copy.

⛔ **Blocker for (b), checked:** `gmd_find.h` exposes `gmd_text_by_index()` but
**no reverse lookup** (text → index). Building one means indexing 4165 live
strings on the worker thread — the exact cost `gen_prompt_table.py`'s header
says it exists to avoid. So (b) needs a runtime index built once and cached,
which is real work, not a flag.

▶ **Recommendation: (a)**, because it is mechanical and testable offline.

⚠ Its urgency is LOWER than the first version of this file claimed: the
retracted measurement said it would fix 26 of 37 sessions, and the truth is
that every observed session was English, so this fixes nothing that is
currently broken. It is insurance for the moment the player changes language —
which is exactly what the user asked for — not a live bug.

⛔ **And it must come SECOND, after part 0 below.** A 14-language prompt table
keyed on text is useless if the mod resolved the wrong blob to begin with:
identification is upstream of selection, and selection is upstream of captions.

### The check that must accompany it
`check_markup_parity.py` proves the C markup speaker matches a Python oracle
239/239. The equivalent here: for each of the 14 languages, every caption that
resolves in English must resolve to the SAME LABEL in that language. A language
where a caption maps to a different label is a collision that must be flagged
`PROMPT_AMBIGUOUS`, not silently resolved.

⚠ Ambiguity is per-language. "Choose a game version." collides in English; the
German pair may not, or a German pair may collide where English does not.
Generating 14 tables without re-running the collision analysis per language
would ship a guess in 13 of them.

---

## 3. RETIRING THE WEAKER METHODS — carefully, and NOT wholesale

### The census (from `src/menu_speak.c`)

    MC_SETTINGS_ROW    26      MC_LIST            19
    MC_SETTINGS_VALUE  19      MC_GAME_STRIP       5
    MC_COORD_LIVE       4      MC_NUMBERED_LIVE    2

Coordinates — the strongest method, needing no captions and no corpus text —
carry **4 rows of ~75**.

### ⛔⛔ BUT COORDINATES ARE NOT A DROP-IN REPLACEMENT, and the evidence is already in
`coord_candidate()` admits any short, markup-free, non-sentence string from the
instance set, and `gt_item_at_position()` REFUSES when the candidate count
disagrees with the screen's count. On the Music chooser that filter yielded
candidate counts of 6, 31, 31, 31, 6, 21, 30, 30 and 27 — **never 9** — so the
screen would be refused on every movement. Dead code that reads like progress.

⭐ So coordinates work where the candidate set happens to be CLEAN (the lobby
slots: single characters; Training Options: six short labels in one column).
They are not a general replacement for 45 caption-derived maps.

### ⭐ What coordinates SHOULD be used for instead
1. **The discriminator in part 1** — reading one known row, not ordering a whole
   screen, so the count check is not a barrier.
2. **Re-VERIFYING existing maps offline.** A run with `cfc2_layout.on` dumps
   every screen's labels with coordinates; comparing that ordering against each
   shipped map is a mechanical audit that needs no captions and no English.
   ⭐ This is the highest-value use: it can CHECK 45 rows without re-deriving
   them, and it is exactly the two-implementations parity check that has caught
   real bugs here before.
3. **Screens the caption channel cannot reach** — the move list (three columns,
   73px grid, per-PAGE category counts), which needs a reader wiring rather than
   a label table.

### ▶ What to retire, concretely
- ⛔ `tools/resolve_screens.py` (candidate label runs by plausibility) — already
  described in `cfc2-screen-identification` as the weakest method and the source
  of a session's worst blockers. Nothing shipped cites it.
- ⚠ Keep `name_screens.py`: it is caption-driven and therefore
  English-dependent, but it NAMES rather than narrows, and part 2 fixes its
  input.
- ⛔ Do NOT retire captions as a channel. They remain the only source for
  screens whose items are not simultaneously on screen.

---

## ▶ ORDER OF WORK

⭐ The order is a DEPENDENCY order, not a preference: identification →
selection → captions. Doing them out of order builds each layer on an
unverified one below it.

0. **Complete `g_lang_by_idx0` to 14 languages**, with the index-29 tiebreaker
   for spa/oth. This is the foundation: until the live table can be identified,
   nothing downstream can be trusted to be in the player's language. Smallest
   of the three changes and the only one that is purely additive data.
1. **Part 2(a)** — 14-language prompt table + per-language collision analysis.
   Depends on 0, because a text table is only correct for the blob actually
   resolved.
2. **The `"Yes"` literal** (`gui_text.c:1574`, `:1633`) → `gmd_text_by_index()`
   of `SYS_WINDOW_YES` (25). Small, and it removes the last hardcoded English
   string in a read path.
3. **Part 1** — the same-caller discriminator, using `gmd_text_by_index()` so
   it is language-safe from the start. Needs one run capturing both 8-row pause
   variants.
4. **Part 3.2** — the coordinate AUDIT of all shipped maps, as a tool. Then
   decide per screen whether anything migrates.

⚠ Steps 0-2 are offline and testable with no game launch. Step 3 needs a run.

⚠ Each step ships only with: `test_screen_maps.py` green, all 16 runners green
(**check EXIT CODES, not output text** — these suites print "FAIL" in normal
output and that produced seven phantom failures in this session), a build, and
a deploy verified BY HASH and only AFTER the suites pass.
