CVS2 selection-state research audit — 2026-09-18

The objective is to announce the option currently highlighted on character,
groove, ratio and stage selection, separately for each player. No validated
guest-RAM mapping or selection field has been established. This review adds
no runtime probe and makes no claim that accessibility is implemented.

The user asked for prior research to be examined before another attempt.
The relevant Claude memories are under
`C:/Users/amock/.claude/projects/C--Users-amock/memory/`, outside this repository.
Several contain contradictory conclusions within the same file. Treat their
measurements and later corrections separately from their headings.

Follow-up: the user supplied `C:/Users/amock/Downloads/flycast_roms.json`.
Its arcade CVS2 dependency entries were inspected and used to download
`naomi.zip`, `cvs2.zip`, and `cvs2_chd.zip` into `build/reference/flycast/`.
Both CHDs were extracted under that directory's `cvs2/` folder. All three ZIPs
passed internal CRC checks. The CHD v5 header identifiers match MAME's
GDL-0007A and GDL-0008 definitions; this is not a full decompressed CHD
verification. The computed SHA-1 of `317-5078-com.pic` matches MAME's
`0882d0e12ca4fb81dda2268cd12724a10278c220`.
See [reference inventory](cvs2-reference-inventory.json) for provenance,
file hashes and dependencies. The netplay save state was not downloaded.
This supersedes the earlier lack of locally available reference downloads;
the historical review below still describes what had been inspected then.

The user confirmed the installed game directory is
`C:/Program Files (x86)/Steam/steamapps/common/CAPCOM FIGHTING COLLECTION 2`.
Reference files were kept in the workspace; the game installation was not changed.

**What the external sources actually provide**

The expanded source follow-through is recorded in
[the prior-art source ledger](cvs2-prior-art-sources.md). It supersedes the
earlier inaccessible-forum entry below: pages 1 and 2 are now readable using
the correct `p-o-r-t` URL; page 3 still failed retrieval. The ledger also adds
PalMod's explicit CVS2 Steam asset mappings, toolbox discussion comments and
fork checks, and the older Jesuszilla hitbox source.

- The user also supplied a [NAOMI BIOS reference](https://www.retrostic.com/roms/mame/naomi-bios-40807).
  The page could not be retrieved in this environment; no BIOS archive was
  downloaded or verified. Record its actual filenames, revision and hashes
  against the chosen emulator's definitions before using it for reference
  execution. A BIOS can support running the reference arcade game; its
  availability does not establish how CFC2 stores game state.
- game-extraction-toolbox has a `cfc` task and an `mvscc` task, but no `cfc2`
  task in the inspected main-branch directory. Its CFC handlers extract ARC
  contents and use title-specific CPS-2 offsets. This is useful packaging
  precedent; it does not describe CFC2's runtime memory or a NAOMI loader.
  Sources: [task directory](https://github.com/shawngmc/game-extraction-toolbox/tree/main/src/gex/lib/tasks/impl),
  [CFC implementation](https://github.com/shawngmc/game-extraction-toolbox/blob/main/src/gex/lib/tasks/impl/cfc/__init__.py),
  [ARC reader](https://github.com/shawngmc/game-extraction-toolbox/blob/main/src/gex/lib/archive/arc.py).
- The toolbox's Marvel implementation covers five CPS-2 titles. Its changelog
  explicitly excludes MVC2 and The Punisher. The NAOMI comparison is therefore
  missing from that implementation. Sources:
  [Marvel metadata](https://github.com/shawngmc/game-extraction-toolbox/blob/main/src/gex/lib/tasks/impl/mvscc/metadata.json),
  [changelog](https://github.com/shawngmc/game-extraction-toolbox/blob/main/CHANGELOG.md),
  [extraction discussion](https://github.com/shawngmc/game-extraction-toolbox/issues/81).
- The MVC2 modding guide documents extraction of `game_50.arc` into
  `bin/mvsc2.21D3D8A7`, using ARC tooling and a collection-specific wrapper.
  That establishes an asset workflow, not a published guest-RAM base.
  [Author's extraction guide](https://zachd.com/mvc2/colors/tools/ComboMasher/Combomasher%20Guide/extract-c.html).
- Flycast Dojo's CVS2 Lua script supplies NAOMI and Dreamcast address tables,
  fighter names, grooves and stages. It does not define a hover-cursor field.
  Its NAOMI path is selected by game ID `CAPCOM VS SNK 2 JAPAN`; compatibility
  with CFC2's `cvs2u` must be demonstrated. P1 character is `0x8C241861`,
  health `0x8C2419A8`, groove `0x8C241D90`, team bytes `0x8C241D80..82`,
  and stage `0x8C18A5B0`. P2 health is `0x8C241F74` (difference `0x5CC`).
  The script assigns P1 ratio and power the same address: do not silently
  treat both labels as independently verified semantics.
  [Primary Lua source](https://github.com/blueminder/flycast-dojo-training/blob/main/cvs2.lua).
- The user-supplied reference directory lists `gdl-0007a.chd` and
  `gdl-0008.chd`. MAME identifies the former as `cvs2mf`, version 010705,
  and the latter as `cvs2`, version 010804. MAME's expected GDL-0008 disk SHA-1
  is `a45c7df4ca6692ff0d1a34f8269e17760fbd3a04`. The directory was inspected;
  neither disk was downloaded or hash-verified in this review. A filename
  alone does not establish which CFC2 revision it matches.
  [Reference listing](https://archive.org/download/naomiromset_mame0.218/cvs2/),
  [MAME driver](https://github.com/mamedev/mame/blob/master/src/mame/sega/naomi.cpp).
- The Z3U back-port memories document asset correspondence and a boot problem,
  not cursor addresses. Their reported SH-4-positive reconstructed ROM is a
  useful control only once the origin of its executable bytes is established:
  a reconstruction can combine original-ROM code and CFC2 assets. The original
  371 MB archive was not found at the recorded Downloads path. The available
  `CFC2z3uPORT.zip` contains only `CFC2z3uPORT.bps` (11,032 bytes), not those
  notes or a reconstructed executable. The
  [Arcade-Projects thread](https://www.arcade-projects.com/threads/capcom-fighting-collection-2-street-fighter-zero-3-upper-sega-naomi-port.34097/)
  could not be retrieved. Its unseen contents remain an explicit research gap.

**Corrections that affect the next investigation**

1. NAOMI main RAM is 32 MiB. MAME maps `0x0C000000..0x0DFFFFFF`;
   the repeated 16 MiB claim in our notes and headers is incorrect. Masking
   addresses to 24 bits loses the distinction between the two halves.
   This is a hardware reference, not proof CFC2 allocates one contiguous
   32 MiB buffer. [MAME memory map](https://github.com/mamedev/mame/blob/master/src/mame/sega/naomi.cpp#L1253).
2. Run 18 sampled 4 KiB every 4 MiB in committed writable memory, about
   0.098%, and skipped arena slices covered by run 17's similarly sparse
   sampling. It establishes no detected SH-4 in those samples. It cannot
   establish absence from all readable memory, and a code detector cannot
   establish absence of game data. Positive controls establish some sensitivity,
   not completeness of coverage or sensitivity to every executable layout.
3. Recompilation does not imply guest RAM disappears. Flycast maintains
   guest RAM alongside its dynarec, with explicit address translation and
   write protection for code pages. A writable-only search can miss protected
   code. This is a counterexample to the earlier inference, not evidence that
   CFC2 uses Flycast or has the same architecture.
   [Memory access](https://github.com/flyinghead/flycast/blob/master/core/hw/sh4/sh4_mem.cpp),
   [dynarec page management](https://github.com/flyinghead/flycast/blob/master/core/hw/sh4/dyna/blockmanager.cpp).
4. The no-constant-mapping experiment compared a pointer's host storage
   location with its guest target value. Those are different objects. For a
   mapping `H(g) = B + g`, a pointer stored at guest address `s` and pointing
   to `t` gives `H(s) - t = B + s - t`, which need not be constant. One must
   identify the host location of the TARGET and compare `H(t) - t`.
   The reported 118 differences among 119 words therefore do not rule out
   a linear mapping, even if every word were a genuine pointer. The planted
   positive control imposed a relation real pointers need not satisfy.
5. `tools/find_sh4.py` constructs its random control with
   `bytes(random.Random(1).randrange(256) for _ in range(WINDOW))`.
   Reseeding inside the generator makes every byte identical. Reproduced
   offline: 1 distinct byte and 0 balanced pairs. A single RNG outside the
   generator gives 256 distinct bytes and 12 balanced pairs. Both receive
   `not code`, so this flaw does not itself demonstrate a false positive;
   it means the claimed random-data control was not performed. No source
   change was made as part of this audit.
6. The blob detector's threshold of 200 balanced prologue/epilogue occurrences
   per 64 KiB was calibrated on another game's reconstructed image. Counts
   do not pair actual functions. A sparse code region, leaf functions,
   different compiler output, alignment or encoding may evade it. The current
   result is 'no windows met this detector's threshold', not proof of static
   recompilation or proof no executable exists. Missing disc/container magic
   likewise does not prove executable bytes are absent from extracted files.
7. A VirtualQuery region is not an allocation boundary. The observed 511 MiB
   region does not extend the constructor's 256 MiB arena allocation.
   `emu_cursor.c` already caps its window accordingly; older prose disagrees.
8. A fixed offset across launches is not evidence against live game state.
   Emulated RAM often preserves guest offsets. The health candidates were
   rejected because they matched archive bytes and lacked health behavior,
   not because their relative offsets stayed constant.
9. A ripped sprite sheet's packing geometry is not necessarily the live
   selection layout. The 5-by-10 arrangement and pixel origins in the grid
   note need an independent live-layout or draw-code correspondence before
   they can turn cursor coordinates into names. The internal fighter ID,
   shell roster order and select-screen position are distinct mappings.
10. Input-correlated changes do not establish field meaning. Observed
    multi-cell movement does not by itself prove an analog implementation;
    repeat timing and the probe's own stalls can also matter. A cursor may
    be coordinates, flags, a pointer or a buffered value. Preserve the user's
    observation without requiring every valid cursor to be a stable byte
    changing once per key edge.

**Evidence worth preserving**

- The session anchor is `*(void**)0x141C14F98`; the state is reported at
  `0x141C14FA0`, with the arena pointer at offset zero. Construction and
  ROM-name/index cross-checks support the anchor. These addresses are tied to
  the analyzed executable build; ASLR being disabled does not make them
  permanent across game updates.
- Index 3 matched `cvs2u` in the recorded live run. This names the loaded
  revision but is not a guest-memory mapping.
- The latest log still records the entry-11 fingerprint at arena offset
  `0x20D800`, explicitly warning it is the loaded archive copy. Prior notes
  also match the health-pair patterns against the archive. Do not revive
  either as a RAM discovery.
- Direct watches rejected `arena+0x8030134` and the `+0x8105xxx` candidates:
  the former churned without input, the latter mostly toggled between zero
  and fixed values. These are rejected candidate identities, not evidence
  against all state in those regions.
- Run 20 supersedes both permanent identities assigned to `+0x8900000`.
  The notes record whole-region content replacement over time. Its role and
  lifetime must be established before interpreting individual fields there.
- Win quotes and ending prose exist in the shipped data. That corrects the
  blanket 'no strings in the arcade game' claim. It does not establish a
  character-select text hook.

Read-only archive verification performed during this review:

- `game_10.arc`: ARC v7, one `bin\cvs2` entry, inflated size 96,110,656 bytes.
- Payload SHA-256:
  `018f0d6e33339020cb2f56d0415e447397b64c916146dfcd69b1b1608d9aede9`.
- `IBIS` at offset zero, `AFS\0` at `0x40`, 623 entries.
- Entry 11's recorded AFS offset is `0x20D800`, length 71,088 bytes.
  Keep AFS-relative and whole-IBIS file offsets distinct.
- `game_1s.arc` and `game_1t.arc` each contain 2,119 entries, all under `sound`.
  Their directory names do not conceal another plainly named program blob.

**The next bounded investigation**

Use the existing unpacked host executable to trace ownership of state, while
using a revision-identified CVS2 reference to establish guest semantics.
These are two separate questions; solving one does not solve the other.

1. Record hashes of the unpacked sections and the selected reference before
   reusing address claims. Determine which exact reference code/data survives
   in CFC2; compare known byte sequences, not instruction-density scores alone.
2. Follow the known session constructor and ROM-selection initialization into
   the code that sets up per-game state. Track allocations, pointer stores,
   and actual load/store users. The ten `0xB10` records at `0x1422D7D30`,
   registered near `0x1406B15EB`, are an existing lead whose purpose remains
   unknown. They are not yet a page table, CPU context or cursor structure.
3. A successful offline result should name a concrete pointer path or address
   accessor and explain the instructions establishing it. If it cannot, record
   the unresolved indirect call or allocation rather than building another
   broad value scanner.
4. Only then design a small live measurement. Groove selection is the simpler
   first target according to the user's observations. Read the proposed field,
   player identity and selection phase across idle, move, reverse, confirm,
   cancel and screen re-entry. Include both players. Distinguish highlighted
   values from confirmed choices and stale values retained after exit.
5. Add speech only after those semantics and the value-to-name mapping agree.
   Character, ratio and stage selection each need their own validation; a
   successful groove result does not automatically validate all four screens.

This is an evidence-based next route, not an assertion that guest RAM is
contiguous or that it exists in the form the earlier scans expected.

**Review coverage and remaining limits**

Reviewed the repository guidance, relevant emulator/cursor sources, detector
sources, current installed log, and the Claude notes covering the session
anchor, arena, ROM content, DES attempt, field schemas, cursor candidates,
reacting pages, graphics interpretations, pointer runs, guest-mapping attempt,
SH-4 searches, Z3U reconstruction, sprite layout and prior-art searches.
Historical run claims above are identified as notes unless reproduced here.
The full old session transcripts and every historical capture were not replayed.

Primary sources were checked for extraction support, NAOMI address mapping,
Flycast memory behavior and CVS2 field definitions. Searches found no verified
CFC2 cursor address; that is not proof none has been published. The inaccessible
forum, missing large Z3U archive and unexamined reference CHD contents prevent
an honest claim that all possible prior art has been exhausted.

Existing uncommitted changes in `src/emu_cursor.c` and `src/mod.c` were
preserved. No game launch, deployment, memory write, new hook or ROM
decryption was performed for this review.
