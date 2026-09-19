CVS2 prior-art source follow-through — 2026-09-18

This supplements [the audit](cvs2-prior-art-audit.md). Work in this pass was
source review only. No new CFC2 disassembly, live probe, hook, game launch or
installation change was made. Downloaded public source snapshots are under
`build/prior-art/`; retrieval manifests record URLs and hashes for the batches
they cover. This directory is ignored by Git. The linked upstream sources
remain the durable references.

**Extraction toolbox and its follow-ups**

- Read all five comments returned for [issue 81](https://github.com/shawngmc/game-extraction-toolbox/issues/81).
  The author's MVC2 finding concerns split audio: sound effects were moved to
  another archive and music converted to Ogg. The author doubts reconstruction
  of a complete playable original ROM. This is not evidence that live game
  state is absent or inaccessible.
- Reviewed the first 100 all-state issue/PR records, including
  [implementation PR 86](https://github.com/shawngmc/game-extraction-toolbox/pull/86),
  release PR 94 and Japanese-ROM issue 95. The implementation excludes MVC2;
  issue 95 supplies CPS2 Japanese program cuts, not a NAOMI implementation.
- Listed 20 public forks. Inspected task directories on the default branches
  of the nine whose push dates follow CFC2's release: tristanjl, pathawks,
  dekr0nos, Ruj89, GuiMiTomo, jeandrek, TheMasterCoders, Morplson, fliperama86.
  Each exposes `cfc` and `mvscc`, with no separately named CFC2/NAOMI task.
  This is a directory check, not a claim to have compared every file or branch.

**CFC2-specific palette mappings: a concrete additional source**

[PalMod's CVS2 classes](https://github.com/Preppy/PalMod/blob/master/palmod/Game/Game_CVS2_A.h)
explicitly distinguish NAOMI and Steam. The Steam class expects
`cvs2.21D3D8A7`, size `0x5BA8840`, and lists CRC32 `0x5AC6D5EA`.
The NAOMI class expects `SNKGD_SL.bin`, size `0x9800000`.
Those are published expectations, not a fresh verification of our files.

The [palette definitions](https://github.com/Preppy/PalMod/blob/master/palmod/Game/CVS2_A_DEF.h)
contain `CVS2_S_UNITS` with per-character adjustments to the arcade layout.
Examples, expressed as arcade file offset to Steam file offset:

- Ryu: `0x1488E80` to `0xD92400`.
- Ken: `0x1553500` to `0xE690C0`.
- Chun-Li: `0x166F140` to `0xF90680`.
- Akuma: `0x20759C0` to `0x1A327A0`.

These adjustments differ by character. Inference: a single whole-file offset
translation is inappropriate for these assets. These mappings can identify
known data when comparing versions; they do not identify RAM or cursor state.

[PalMod's guide](https://zachd.com/palmod/releases/ReadMe.html) points to ARC
unpack/repack and CollectionTool workflows. It does not document CFC2's
emulated memory accessor. The [MVC2 extraction guide](https://zachd.com/mvc2/colors/tools/ComboMasher/Combomasher%20Guide/extract-c.html)
likewise provides an asset workflow. The [stage-creation tutorial](https://github.com/TVIndustries/MvC2-StageCreation/blob/main/MvC2_Stage_Tutorial.md)
covers stage models, textures and SH4 assembly tools, rather than a CFC2
runtime mapping.

**Backport thread: previous notes were incomplete**

The correct [thread URL](https://www.arcade-projects.com/threads/capcom-fighting-collection-2-street-fighter-zero-3-upper-sega-naomi-p-o-r-t.34097/)
contains `p-o-r-t`. Page 1 was retrieved. HDemon's posts evolve from rebuilding
CFC2 data with a NAOMI header to inserting changes into the original NAOMI DAT.
Post 16 reports the latter approach as playable with sound, with 23 differing
files and four new files whose placement was unresolved. Therefore the early
freeze/no-sound report is not the last result on that page. It also does not
establish that executable bytes came from CFC2.

[Page 2](https://www.arcade-projects.com/threads/capcom-fighting-collection-2-street-fighter-zero-3-upper-sega-naomi-p-o-r-t.34097/page-2)
was read. In post 30 the author says there are discoveries and earlier
misstatements still to explain. Later posts mainly discuss additional
characters and official fixes. Page 3 failed repeated web retrieval and a
direct HTTP request returned 403. Its contents remain unknown. Do not describe
this thread as fully reviewed or its latest technical outcome as established.

**Existing state and hitbox tools**

- [Flycast Dojo CVS2 Lua](https://github.com/blueminder/flycast-dojo-training/blob/main/cvs2.lua)
  remains the strongest reviewed NAOMI field schema. Both public forks listed
  by GitHub, Nailok and SamuelioX, returned identical CVS2 Lua files to each
  other (SHA256 `a967d7de72d3a775466fadf708f0581bdda39fd8cc5b4855b13a338351775eff`).
  No separately documented hover field was identified in this follow-through.
- Retrieved [Jesuszilla's CVS2 table](https://github.com/Jesuszilla/cheatengine-scripts/blob/master/CvS2.CT)
  and [README](https://github.com/Jesuszilla/cheatengine-scripts/blob/master/README.md).
  The documented supported CVS2 versions are PS2 NTSC-U/J using specific
  PCSX2 builds. The actual table additionally includes a DEmul 0.57 path
  selecting `CVS2_DC` and hard-coded bases, so calling the source strictly
  PS2-only is inaccurate. That path concerns Dreamcast, not NAOMI.
  It exposes object, sprite, animation, position and hitbox fields. No explicit
  groove, ratio or character-select cursor label appeared in the targeted
  source search. Porting these hard-coded host addresses to CFC2 is unjustified.
- Read both [Smoker1 NAOMI CVS2 cheat files](https://github.com/Smoker1/RetroArch-174-and-above-Cheats/tree/master/Reicast-NAOMI).
  They provide timer, health, super and groove-points writes with different
  address sets. Groove points are not the highlighted groove choice. Retain
  source filename/version distinctions and byte-width conventions; do not
  combine these into one asserted CVS2 layout.
  `CvsS2 NAOMI MAME.cht` uses timer offset `0x2259C4`, health-byte offsets
  `0x2419A9`/`0x2419AB`, super `0x241A46`, groove points `0x241D94`,
  and CPU super `0x242012`. Its timer and CPU-super offsets agree with the
  corresponding Dojo fields after removing the guest region prefix.
  The separate `Millionaire Fighting 2001 (Rev A)` file instead uses timer
  `0x224C80` and health bytes `0x240C69`/`0x240C6B`. This does not itself
  establish a uniform relocation or explain the revision difference.
- [Pugsy's NAOMI discussion](https://www.mamecheat.co.uk/forums/viewtopic.php?t=12507)
  returned a bot-verification page. The archived SuperCombo hitbox thread
  returned 403; its linked author's GitHub source was available as above.

**Other leads and what they actually establish**

- [dcrecomp](https://github.com/sp00nznet/dcrecomp) describes an independent
  SH4-to-C framework, with CPU state and address translation. Nothing reviewed
  connects its implementation to Capcom's. It cannot establish how CFC2 runs
  code or explain a negative SH4 scan by itself.
- [NaomiMod extraction tools](https://github.com/NaomiMod/games-ExtractTools):
  reviewed the complete returned repository tree. Generic AFS/PVR/Ninja/audio
  utilities and other-game extractors are present; no named CFC2/CVS2 loader
  was identified. [Ghidra exporter](https://github.com/NaomiMod/Ghidra_Export_SH4_ASM)
  exports selected assembly for an SH4 assembler; it is not an emulator map.
- [ncrypt](https://github.com/prrole/ncrypt) is a NAOMI binary encryption utility.
  Its README supplies no CFC2 state layout. Decryption is a reference-image
  preparation concern, not the explanation for missing CFC2 selection state.
- [Gibbed.MT](https://github.com/gibbed/Gibbed.MT) documents ARC pack/unpack;
  [RevilLib](https://github.com/PredatorCZ/RevilLib) is a format library/toolset.
  Their reviewed landing documentation does not supply the required CFC2
  emulator accessor. This is not a claim that every implementation file was read.
- [GameBanana CFC2 hub](https://gamebanana.com/games/22515) and indexed results
  yielded music/portrait asset work, not a published selection-state map.
  JavaScript-dependent listings limit coverage.
- [Xmod's CFC2 page](https://www.xmodhub.com/en/xmod-cheats/capcom-fighting-collection-2-trainers)
  advertises 49 cheats, including health controls. That contradicts a blanket
  claim that no trainer is advertised. No implementation, addresses or validated
  CVS2 support were exposed in the retrieved page. No trainer was installed.
- [Fearless request thread](https://fearlessrevolution.com/viewtopic.php?t=35032)
  remains inaccessible here. An indexed forum listing shows replies; its title
  alone cannot justify the old claim that it is unanswered.
- Search results often return Prism Access's own posted session transcript.
  That is recycled project history, not independent corroboration.

**Current boundary**

We have additional published asset mappings and better provenance for existing
state addresses. We still do not have a verified CFC2 guest-RAM accessor or
hover field. The inaccessible final backport page, uninspected full historical
transcripts, trainer implementation and unreviewed non-default branches remain
explicit limits. These limits must not be rewritten as proof that no answer
exists. Preserve the user's requirement to finish relevant prior-art follow-up
before another independent binary investigation.
