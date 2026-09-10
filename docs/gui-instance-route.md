# Reopening the GUI tree — from the INSTANCE side

Written 2026-09-10, after the user asked how much of four remaining problems
the GUI tree would solve.

## Why this is not a repeat of route 3

Route 3 is closed with an unusually solid negative
(`cfc2-gui-tree-walk`): 1789 pool-referencing objects, full ±128-byte windows,
one hop out, complete passes, different screens — **zero changes of any kind**.

⭐ But its own closing note names the successor exactly:

> the layout-template subtree is a dead end and the root must come from
> elsewhere (e.g. the GUI system's own instance list, not the layout data)

Route 3 anchored on the **name pool**, which is file data parsed out of
`menu.arc`. Everything within one hop of it is also file data — static by
design. The generalisable error it records: *"I anchored on the thing I could
FIND rather than the thing that CHANGES."*

**What is different now, and it is not a small difference:** the `setText` hook
did not exist when route 3 closed. It hands us a **live `cGUIObjText` instance
pointer** on every text assignment — an object the game constructed, not a
template it parsed. That is the instance-list root route 3 said was needed, and
it arrives as an argument with no searching at all.

## What the logs already establish

Measured from `docs/run-latest.log` and `run-2026-09-10-0053.log`, no new run
required:

- **126 live `cGUIObjText` constructed** in an ordinary session; 25 distinct
  objects seen within a single focus window.
- **Every item of a screen gets its own object.** The main menu sets six:
  `0x2BF186D0` "Offline Play", `0x2BEF2320` "Online Play", `0x2BED75F0`
  "Museum", `0x2C3A14C0` "Fighter Awards", `0x2BEB8FC0` "Options",
  `0x2C327DF0` "Quit".
- ⭐⭐ **Object identity is STABLE per item across movements.** On the Options
  screen `0x2C322A30` is "System" at move 2 and still "System" at move 3;
  `0x2BEA70D0` is "Music" in both. The address is the row's identity.
- The per-row caption reuses ONE object (`0x2BEBB390` carries every row's help
  text in turn), which is why the caption channel works at all.

So the item text for a screen is **already being captured**. What is missing is
the mapping from *object* to *position*, and the selection state.

## What this would actually fix, per the user's four problems

- **Settings values reading game names** — ⛔ nothing. That was a bug of mine
  (`row == 0 && rom_table_ready()` firing on every settings screen) and is
  already fixed. Listed so it is not credited to this work.
- **Online submenus unread** — ⭐⭐ the big win. Today each screen costs a run
  plus a caption that may never arrive within the movement window. Reading the
  instance set would give item text for **any** screen with no per-screen
  evidence-gathering and no dwell requirement.
- **Nothing under Gallery reads** — ⭐ the only plausible route. Gallery's items
  are logos and its confirm indexes resource-id tables
  (`cfc2-gui-layout-finder`), so the GMD cannot name them.
- **Music files unread** — ⭐ same. Track names are not in the message corpus.
  ⚠ The one music-screen capture we have is `"0:00 / 0:00"` (a playback timer),
  so it is NOT yet established that track titles flow through `setText` at all.
  That is the first thing to measure, not assume.
- **The consent dialog** — ⭐ the only route; it sets no item text, so its
  buttons must come from the tree.

## The plan

### Step 1 — measure, before building anything
Add a **logged-only** probe (no speech) that, on a cursor movement, dumps every
live `cGUIObjText` the ring knows about with its current text. The ring already
exists (`g_ring`, 32 slots, filled by the ctor hook) and needs raising to hold
a screen's worth.

The question it answers, and it is falsifiable: **does the set of live objects
contain one text per menu row, and can the FOCUSED one be told apart?**

⛔ Do not skip this. Route 3's whole lesson is that a reachable, identifiable
structure is not automatically live state. The equivalent trap here is that all
126 objects are alive and only some belong to the current screen.

### Step 2 — find the selection signal
If step 1 shows one object per row, diff an object's own 0x210 bytes between
"selected" and "not selected". The class has known fields (`+0x54` flags,
`+0x60` bitfields, `+0x70` first child, `+0x78` next sibling) and the layouts
carry `DEFAULT`/`SELECT`/`DECIDE`/`DISABLE` animation states, so selection is
modelled somewhere in the object.

⚠ **Use a same-screen control**, exactly as route 3's closing note insists:
capture twice on the same screen one item apart, so animation phase and timers
can be subtracted. Without it, everything that merely ticks looks like a signal.

### Step 3 — only then, speech
Join the focused object to its text and speak it. This subsumes the caption
channel for item NAMES and removes the dwell requirement.

⚠ Keep the existing cursor+GMD path. It is verified, and two independent
channels that agree is the standard this project has settled on.

## What could kill it, stated in advance

- The live set may not distinguish the current screen from resident ones (126
  objects, ~6-40 per screen).
- Selection may not be a field of the text object at all — it may live on a
  parent node or in the animation system.
- Objects may be reused across screens, so a stable address is only stable
  *within* a screen.

Each of those is answerable by step 1 or step 2 **before** any speech code is
written, which is the point of ordering it this way.
