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
 * The error was a MISALIGNED DTI table. Records are 0x38 bytes with the size
 * field at +0x30; reading them 8 bytes late made 0x22c82c8 look like sPad's
 * own slot when it is actually the next record's first field. With the
 * alignment fixed the sizes decode cleanly (sPad::Pad 0xb8, sPad::Map 0x50,
 * sPad 0x3c4), which is the check that proves the boundaries.
 *
 * ⚠ Verified three ways, not assumed:
 *   - two independent code sites resolve to it:
 *       0x140036de1  mov rcx,[rip+0x2291430]  -> 0x1422c8218
 *       0x1401880e3  mov rcx,[rip+0x214012e]  -> 0x1422c8218
 *   - the gate at 0x1401880ea tests [rcx+0x38] as a live enable flag
 *   - the update routine writes the button words at the offsets below */
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

/* ⚠ INFERRED, not measured: pad[0]'s base is +0x8 within the instance (from
 * Be_flag being the first registered field at record-relative 0x08), and the
 * array is about 5 records. Consistent with the observed +0x1c0 = 0x8 + 0x1b8
 * indexing, but no count constant was found. The dump below covers the whole
 * range regardless, so the analysis does not depend on either. */
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
