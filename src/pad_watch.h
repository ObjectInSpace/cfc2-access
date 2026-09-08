/*
 * Dump the sPad singleton object to disk. Nothing else.
 *
 * ⭐⭐ WHY THIS TOOL MAKES NO JUDGEMENTS AT ALL.
 *
 * Route 5 failed FOUR times in this project, and every failure had the same
 * shape: a decision baked into the mod that only revealed itself as wrong
 * after the user had spent a game launch on it.
 *
 *   1-3. Three scanners searched 1.4 GB for a struct recognisable only by its
 *        shape. A shadow-copy pair, an alternating boolean, and a site frozen
 *        at one value each passed every filter; each fix was another filter
 *        that a different look-alike then passed. Look-alikes are abundant and
 *        genuinely indistinguishable as data.
 *   4.   A two-state diff with NO SAME-STATE CONTROL. The captures were eight
 *        seconds apart, so the diff measured elapsed time -- timers, frame
 *        counters, animation -- not the keypress. Two runs holding DIFFERENT
 *        keys produced the identical 109 changed bytes, which is what
 *        disproved it. Its "find" (+0x34bd8 / +0x34c00) was also selected by a
 *        filter searching for exactly the values it then reported, which is
 *        circular.
 *
 * So this module does not compare, filter, rank, or conclude. It writes bytes
 * to a file and records what the keyboard said at that instant. All analysis
 * happens offline in tools/analyze_pad_dumps.py, where a wrong assumption
 * costs an analysis pass rather than a launch.
 *
 * ⚠ A consequence worth stating: this tool CANNOT report "nothing found",
 * because it never looks for anything. That failure mode is now impossible
 * here by construction.
 *
 * ⭐⭐ WHAT IT RELIES ON, ALL READ OFF THE MACHINE CODE:
 *
 *     [image + 0x22c8218]  ->  the sPad instance
 *     pad[i] fields at     +0x160 On, +0x164 Old, +0x168 Trg,
 *                          +0x16c Rel, +0x170 Chg, +0x174 Rep
 *     array stride         0x2e0
 *
 * ⚠⚠ AN EARLIER VERSION OF THIS FILE NAMED 0x22c82c8 AND WAS WRONG. That
 * address is a UI/dialog manager, which is precisely why a 0x36000-byte diff
 * of it changed identically whichever key was held. The mistake was reading a
 * 0x38-byte DTI table 8 bytes out of alignment; with the alignment fixed the
 * declared sizes decode cleanly and the boundaries are unambiguous.
 *
 * The corrected pointer is not another inference. The update routine at
 * 0x14018449e-0x1401844e7 writes Old, then On, then computes On^Old into Chg,
 * Chg&On into Trg and Chg&~On into Rel -- the textbook derivation, at exactly
 * the offsets the reflection metadata names. Two independent code sites
 * resolve to the pointer, and a live enable flag is tested at [instance+0x38].
 *
 * Read-only: guarded reads, no hooks, no calls into game code.
 */
#ifndef CFC2_PAD_WATCH_H
#define CFC2_PAD_WATCH_H

#include <windows.h>

/* Image-relative address of the sPad INSTANCE pointer.
 *
 * ⭐⭐ CORRECTED 2026-09-03. The earlier value 0x22c82c8 was WRONG and is why
 * every attempt this session failed: it pointed at a UI/dialog manager, so a
 * 0x36000-byte diff changed identically no matter which key was held.
 *
 * The error was a MISALIGNED DTI table: 0x22c82c8 is the NEXT record's field,
 * not sPad's own slot.
 *
 * ⚠⚠ THE DTI SIZE CHECK THAT ORIGINALLY "PROVED" THE ALIGNMENT IS NOT
 * REPRODUCIBLE. It read sPad::Pad 0xb8 / sPad::Map 0x50 / sPad 0x3c4 from
 * DTI+0x30 in one dump; the same addresses read ZERO in a later dump. That
 * region of the DATA section is populated at some point during startup, so
 * whether it holds anything depends on when the dump was taken. Do not cite
 * it, and do not rely on any DATA-derived offset without re-deriving it
 * against the dump in front of you.
 *
 * ⭐ The conclusion does not need it. The size is available from .text
 * instead: the registration at 0x14002a920 passes r9d = 0xf10 as sPad's
 * instance size (and 0xa78 for sKeyboard at 0x14002cce0).
 *
 * ⚠ Verified four ways, all from .text or live memory, none from the DTI:
 *   - two independent code sites resolve to it:
 *       0x140036de1  mov rcx,[rip+0x2291430]  -> 0x1422c8218
 *       0x1401880e3  mov rcx,[rip+0x214012e]  -> 0x1422c8218
 *   - the gate at 0x1401880ea tests [rcx+0x38] as a live enable flag
 *   - the update routine writes the button words at the offsets below
 *   - the registration at 0x14002a920 gives size 0xf10, matching the object
 *     actually captured */
#define PAD_INSTANCE_RVA 0x22c8218

/* ⭐⭐ FIELD OFFSETS, READ OFF THE UPDATE CODE at 0x14018449e-0x1401844e7.
 *
 * This is measured, not inferred -- the instructions literally compute:
 *     [+0x164] = old On            (Old)
 *     [+0x160] = new On            (On)
 *     [+0x170] = On ^ Old          (Chg)
 *     [+0x168] = Chg & On          (Trg)
 *     [+0x16c] = Chg & ~On         (Rel)
 *     [+0x174] = 0                 (Rep)
 * which is the textbook trigger/release derivation and matches the reflected
 * field names exactly. */
#define PAD_FIELD_ON   0x160
#define PAD_FIELD_OLD  0x164
#define PAD_FIELD_TRG  0x168
#define PAD_FIELD_REL  0x16c
#define PAD_FIELD_CHG  0x170
#define PAD_FIELD_REP  0x174

/* Per-pad array stride, from `imul rax, rdi, 0x2e0` at 0x140036df0 (and three
 * more sites), corroborated by flat offsets 0x1b8/0x498/0x778/0xa58/0xd38 whose
 * successive differences are all exactly 0x2e0.
 *
 * ⚠ NOT 0xb8. That is the DTI's declared (serialized) size; the in-memory
 * record is padded well beyond it. Chasing a 0xb8 grid earlier produced a 6/14
 * fit, which was noise. */
#define PAD_STRIDE 0x2e0

/* ⭐⭐ LAYOUT NOW PROVEN IN LIVE MEMORY, not inferred.
 *
 * The update loop at 0x140187b50-0x140187be9 does `lea rbx,[r14+0x1e5]`, calls
 * with `[rbx-0x18d]`, then `add rbx,0x2e0` / `cmp edi,5`. So:
 *
 *     pad[i] = instance + 0x58 + i*0x2e0,  exactly 5 records
 *
 * ⚠ The base is +0x58, NOT the +0x8 previously inferred from the reflection
 * registrar. A live dump settles it: Pad_no reads 0,1,2,3,4 at exactly these
 * five records and Be_flag is 1 on each. That is the structure identifying
 * itself.
 *
 * ⛔⛔ AND YET On/Old/Trg/Rel/Chg/Rep ARE ALL ZERO IN EVERY CAPTURE, across
 * three different directions with clean press/release edges.
 *
 * Why: every record has **Kind = 0**, and the update dispatcher at
 * 0x1401880a0 starts `movzx eax,[rcx+0xb]` (Kind) / `test al,al` / `je` --
 * it returns immediately. The pad update never runs. The records are
 * allocated and numbered but no device is bound to them.
 *
 * ⭐ CONCLUSION: the shell menus do NOT read the keyboard through sPad. This
 * whole object is a dead end for menu navigation, and that is a fact about the
 * game rather than a limitation of the tooling -- the layout was confirmed
 * correct before the negative was accepted. */
#define PAD_RECORD_BASE 0x58
#define PAD_RECORD_COUNT 5
#define PAD_DUMP_SPAN 0xf00

/* ⛔⛔ DISPROVED 2026-09-03 -- KEPT AS A WARNING, NOT A RESULT.
 *
 * A run holding LEFT reported exactly two bytes carrying Left bits (0x80 at
 * +0x34bd8, 0x08 at +0x34c00) and this was called a confirmed find. It was
 * not, for two independent reasons:
 *
 *  1. The filter accepted only held bytes equal to 0x08/0x80/0x88, then
 *     finding two of them was reported as evidence. Circular. With ~80 bytes
 *     dropping to zero on release, two landing on those values is roughly what
 *     chance predicts.
 *  2. There was no same-state control, so the diff was dominated by elapsed
 *     time rather than by the key.
 *
 * The disproof: a later run holding DOWN found +0x34bd8 still reading 0x80 --
 * a LEFT bit -- and the same 109 changed bytes. A byte that reads 0x80
 * whichever key is held is a constant, not input state.
 *
 * These constants are not used by any code. They exist so the wrong answer is
 * not rediscovered and mistaken for a right one. */
#define PAD_DISPROVED_STICK_OFFSET 0x34bd8
#define PAD_DISPROVED_DPAD_OFFSET  0x34c00

/* --- sKeyboard: partial trace, 2026-09-08 --------------------------------
 *
 * The shell does not read input through sPad (see above), so sKeyboard is the
 * next candidate. Established from .text, via the DTI REGISTRATION call --
 * there is exactly one code reference to each class name string:
 *
 *     0x14002cce0   sKeyboard registration
 *                   rcx = DTI  0x1422c9c38
 *                   rdx = name 0x14104e6f8
 *                   r9d = size 0xa78
 *     0x14002a920   sPad registration:  DTI 0x1422c8290, size 0xf10
 *     0x14002cd30   sMouse registration
 *
 * ⭐ Note sPad's size 0xf10 confirms PAD_DUMP_SPAN 0xf00 covered essentially
 * the whole object -- the sPad negative was not a truncation artefact.
 *
 * ⛔ THE sKeyboard INSTANCE SLOT IS NOT YET FOUND. Two candidates rejected:
 *   - DTI-0x78 (the offset that holds for sPad): 0 code references, so the
 *     DTI-to-instance distance is NOT constant between classes.
 *   - 0x1422c9bf0 (82 refs, 3 write sites -- the same shape as the known sPad
 *     instance slot): its constructor allocates 0x238, not 0xa78, so it is a
 *     different singleton. Checking the allocation SIZE is what caught this.
 *
 * ⚠ Searching for `mov ecx, 0xa78` near a store finds nothing, so sKeyboard is
 * not allocated with a literal size at a single site.
 *
 * ⛔ FOUR further offline attempts, all negative (2026-09-08):
 *   - 0x14002cce0 turns out to be a REGISTRATION STUB: it registers the class
 *     and stores a vtable/typeinfo pointer (0x14104e600) into the DTI record.
 *     It does not construct anything.
 *   - The DTI's 7 code references are the stub, that store, three `lea rcx`
 *     uses, and a one-line getDTI() accessor at 0x140253200 (`lea rax,[dti];
 *     ret`). None reaches an instance.
 *   - VK-table bounds (0x100 / 0xff / 0x1f) appear at 227 / 1436 / 1425 sites
 *     -- far too generic to filter on, and chasing common constants is what
 *     produced this project's earlier false leads.
 *   - The IAT cannot be matched statically: 295 slots hold resolved system-DLL
 *     addresses, but user32 was at a different base when the dump was taken,
 *     so none match this process's GetAsyncKeyState/GetKeyboardState/etc.
 *
 * ⭐ WHAT THIS MEANS: the offline route to sKeyboard is blocked at the point
 * where it needs a RUNTIME fact -- either the live IAT (to find which API the
 * game calls) or the live instance pointer. Both are cheap to obtain from
 * inside the process, where the mod already runs. The next step is therefore
 * a small in-game probe that resolves the input APIs by name and reports which
 * ones the game's IAT actually points at, rather than more disassembly.
 *
 * ⚠⚠ Anything derived from the DATA sections must be re-derived against the
 * dump in front of you: the game rewrites cfc2_dump on every launch and those
 * sections are captured at a different startup moment. .text is identical
 * between runs. tools/build_riprefs.py now enforces this -- it fingerprints
 * the dump and REFUSES to load a stale index rather than answering wrongly. */

/* Button bits, from the reflected names in the unpacked image:
 * bit 0 Rup, 1 Rright, 2 Rdown, 3 Rleft, 4 Lup, 5 Lright, 6 Ldown, 7 Lleft.
 * Menu navigation (up/down) and settings adjustment (left/right) are the same
 * word, so one reader covers both axes. */
#define PAD_UP     0x11u
#define PAD_RIGHT  0x22u
#define PAD_DOWN   0x44u
#define PAD_LEFT   0x88u

/* Write one capture to the game directory: cfc2_pad_NN.bin plus a line in
 * cfc2_pad_index.txt recording the sequence number, object address, byte
 * count, the keyboard mask at capture time, and a timestamp.
 *
 * `kb_dirs` is what GetAsyncKeyState reported for the arrow keys, passed in by
 * the caller so this module stays testable offline with a synthetic player.
 * It is RECORDED, never interpreted. */
void pad_dump_now(unsigned kb_dirs);

/* Poll the arrow-key mask and capture on the EDGES: the first sample where a
 * direction goes down, and the first where it goes up.
 *
 * ⭐⭐ WHY EDGES AND NOT "WHILE HELD" -- this is the correction that probably
 * un-does a class of FALSE NEGATIVES.
 *
 * Every earlier tool required a key to be down at the instant a given address
 * was sampled. The seed sweep took ~600 ms to cross 1.4 GB and reached any one
 * address at a single moment inside that window, while a keypress lasts
 * ~150 ms -- so the pad record was usually all-zero when its own address was
 * read, and the all-zero rejection then discarded it. A null result from those
 * runs is close to uninformative.
 *
 * Holding was also actively harmful for two more reasons:
 *   - it triggers menu AUTO-REPEAT, so the cursor scrolls and per-screen state
 *     churns between captures;
 *   - `Trg` is set for one frame only, so a capture taken mid-hold reads
 *     Trg = 0 and observes only On/Old.
 *
 * Capturing on the down-edge catches the frame where Trg is live, and asks the
 * player for nothing but ordinary navigation.
 *
 * ⚠ Bounded: it stops after PAD_AUTO_MAX captures so a long session cannot
 * fill the disk or bury the interesting captures. */
void pad_auto_capture(unsigned kb_dirs);

/* Automatic captures to take before stopping. Two controls plus a handful of
 * edges is plenty; more would only make the index harder to read. */
#define PAD_AUTO_MAX 12

#endif /* CFC2_PAD_WATCH_H */
