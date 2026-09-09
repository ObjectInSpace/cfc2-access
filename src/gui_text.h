/*
 * The shell's GUI text elements -- cGUIObjText.
 *
 * WHY THIS EXISTS
 * ---------------
 * ~14 shell screens still speak position only, and they are all blocked on the
 * SAME cause: their labels are assigned to GUI text elements at RUNTIME, so no
 * search of the GMD or of a screen's code can name them.
 *
 * ⭐⭐ THE CONTROL THAT PROVED IT (memory: cfc2-gui-text-route). The Options
 * screen's labels ARE known -- GMD 170..174 -- and the index 170 appears
 * nowhere in its function. A screen whose labels we already know does not
 * carry them in its code. So "read the label id out of the screen's
 * instructions" is closed for the whole family, not merely hard.
 *
 * ⭐⭐ THE CLASS IS REAL AND HEAVILY USED -- settled live, 2026-09-09 17:19.
 * The constructor probe counted **36 cGUIObjText constructed** during ordinary
 * menu navigation. So the route is OPEN; the earlier four negatives were all
 * my own targeting errors, not the game withholding anything.
 *
 * WHAT THE FIVE RUNS COST, AND THE ONE RULE THAT WOULD HAVE SAVED THEM ALL
 * -----------------------------------------------------------------------
 * Each failure was a DERIVED value that a MEASURED one later contradicted:
 *
 *  1. Hooked a "unique" vtable slot -> got an nGUI::OutlineFont. The address
 *     was unique to the vtable but ALSO reachable by direct call, so its
 *     callers passed their own `this`. Uniqueness is not reachability.
 *  2. Hooked a slot that was unique AND callerless -> got the DTI descriptor.
 *     It was the class FACTORY. Neither test asks WHAT KIND of method it is.
 *  3. Filtered a shared helper by vptr -> 10 calls, 0 ours, frozen at 10
 *     forever. It was not a per-frame method at all; "update method" was a
 *     guess from the slot's position.
 *  4. Read the vtable as the qwords after the class NAME -> wrong class. The
 *     names are a packed pool with vtables interleaved, and the DTI record's
 *     +0x08 name belongs to its NEIGHBOUR, so everything was off by one
 *     record: 0x1410563a8 is cGUIObjRoot's, not ours.
 *  5. Read the instance vtable from the factory's `lea` -> 0x141056c50, which
 *     is cGUIObjMessage's. Live objects carry 0x141056b40, stamped by the ctor
 *     itself at 0x1402efc29.
 *
 * ⭐⭐ THE RULE: when a derived value and a measured one disagree, the
 * MEASUREMENT is the fact. Four separate disassembly readings each looked
 * sound and each was wrong; four live objects agreeing settled it in one run.
 * Prefer a probe that MEASURES (the ctor count, the observed vptr) over a
 * chain of inference, even when the inference reads cleanly.
 *
 * ⭐ And the guards are what kept five wrong targets from becoming five wrong
 * FINDINGS: the vptr check refused to dump a foreign object, and `ctor_hooked`
 * kept "could not measure" from printing as "measured zero".
 *
 * WHAT IS ESTABLISHED
 * -------------------
 *   - cGUIObjText: DTI record 0x1422cb058, class size 0x210 (528) bytes --
 *     confirmed twice, by the registration stub and by its factory's own
 *     allocation.
 *   - Factory 0x1402ef980 -> ctor 0x1402efc10 (exactly ONE caller, which is
 *     what makes the ctor a complete census of the class).
 *   - ⭐ The ctor zeroes [this+0x1f8] and [this+0x200] -- the two offsets the
 *     offline accessors (0x1402fc410, 0x1402fc400) pointed at. They are this
 *     class's own fields, not a guess.
 *   - Base layout, from 0x1402f0080/0x1402f0980: +0x00 vptr, +0x54 flags,
 *     +0x58 float, +0x60 bitfields, +0x70 first child, +0x78 next sibling.
 *
 * ▶ THE REMAINING QUESTION is only WHICH offset holds the string, and the
 * timing trap around it: see hook_ctor() and gt_poll_and_log() in the .c. An
 * object is EMPTY when its ctor returns, so it must be re-read later.
 *
 * ⚠⚠ SAFETY -- this reads half a kilobyte through a game-supplied pointer, on
 * the game's own threads, and this project has already killed the game once by
 * trusting such a pointer (memory: hooked-function-pointers-are-untrusted).
 * The detours DEREFERENCE NOTHING beyond a single guarded vptr read; all wide
 * reading happens on the worker thread, VirtualQuery-checked and fault-guarded
 * every time and never cached. Originals are always called and their return
 * values passed through untouched. Off switch: cfc2_guitext.on to enable,
 * cfc2_hook.off to disable along with every other hook.
 */
#ifndef CFC2_GUI_TEXT_H
#define CFC2_GUI_TEXT_H

#include <windows.h>

/* ⭐⭐⭐ THE ERROR UNDERNEATH ALL FOUR FAILED RUNS: THE VTABLE WAS OFF BY ONE
 * DTI RECORD, so every run hooked cGUIObjRoot and called it cGUIObjText.
 *
 * The DTI table is a 0x38-byte stride array of records:
 *     +0x00 = the class's vtable        +0x08 = a class NAME pointer
 * ⚠ but the NAME at +0x08 belongs to the record's NEIGHBOUR, not to itself.
 * Reading "the name beside the vtable" therefore shifts every class by one.
 * Walking the table properly (tools/dti_table.py) gives:
 *
 *     0x1422cae28  vtable 0x1410563a8  cGUIObjRoot     <- what we hooked
 *     ...
 *     0x1422cb020  vtable 0x1410562e8  cGUIObjMessage
 *     0x1422cb058  vtable 0x141056350  cGUIObjText     <- the real one
 *
 * So 0x1410563a8 -- the address in every note, and in the first four runs --
 * is cGUIObjRoot's. That single off-by-one explains all four negatives at once,
 * and it is why an OutlineFont kept arriving: we were never filtering for the
 * class we wanted.
 *
 * ⭐⭐ AND THE DTI VTABLE IS NOT THE OBJECT VTABLE. 0x141056350 is a TWO-slot
 * class-descriptor table (getDTI + factory). What an INSTANCE carries is what
 * its factory stamps in. cGUIObjText's factory is slot 1, 0x1402ef980:
 *
 *     mov edx, 0x210            <- allocates 528 bytes: the documented size,
 *                                  which independently confirms the class
 *     call [r10+0x38]           <- allocate
 *     call 0x1402efc10          <- the REAL ctor
 *     lea rax,[rip+0xd6728b]    <- 0x141056c50
 *     mov [rbx], rax            <- stamped into the object
 *
 * ⭐ THE CONTROL THAT SETTLES IT: that ctor zeroes [rbx+0x1f8] and [rbx+0x200]
 * -- the EXACT two offsets the offline accessors pointed at (0x1402fc410,
 * 0x1402fc400). The original candidates were right the whole time; they were
 * being read out of the wrong class's objects. And slot 29 of the instance
 * vtable IS 0x1402fc410, the [this+0x1f8] accessor. The class agrees with
 * itself from three directions.
 *
 * ⚠ Note the size check cuts the other way too: the FIRST factory I found
 * (0x1402efa10, in cGUIObjRoot's table) allocated 0xe0 = 224 bytes, not 0x210.
 * That mismatch was visible in run 2 and I read past it. A documented class
 * size is a free identity check on any factory -- use it. */

/* Live addresses. ASLR is off and the exe has no relocations, so these are
 * valid in every future run. */
#define GT_DTI_RECORD    0x1422cb058ULL   /* cGUIObjText's DTI record         */
#define GT_DTI_VTABLE    0x141056350ULL   /* its 2-slot descriptor vtable     */
#define GT_FACTORY_ADDR  0x1402ef980ULL   /* descriptor slot 1: makes objects */

/* ⭐⭐ What an INSTANCE ACTUALLY CARRIES at +0x00 -- MEASURED, not derived.
 *
 * ⛔ THE FACTORY'S `lea` IS NOT THE ANSWER. The factory at 0x1402ef980 does
 * `lea rax,[rip+0xd6728b]` -> 0x141056c50 and stores it to [rbx], and I took
 * that as the instance vtable. It is the BASE's. The ctor it calls first,
 * 0x1402efc10, stamps 0x141056b40 at 0x1402efc29 -- and although the ctor runs
 * BEFORE the factory's store in program order, the value live objects carry is
 * 0x141056b40, measured identically across four distinct instances in the run
 * of 2026-09-09 17:19.
 *
 * ⭐ So the run settled a question the disassembly had already answered wrongly
 * twice. When a measured value and a derived one disagree, the measurement is
 * the fact and the derivation is the hypothesis. 0x141056c50 turns out to be
 * cGUIObjMessage's vtable (its factory allocates 0x1e0 and stamps it), which is
 * why it looked plausible: a sibling class of identical shape, 0x110 away.
 *
 * ⚠ Do NOT "fix" this back to the factory's lea. Four live objects say
 * otherwise, and there is a comment here because it looks wrong. */
#define GT_VTABLE_ADDR   0x141056b40ULL

/* What the FACTORY stamps, kept for the record: cGUIObjMessage's vtable, and
 * the value four runs of derivation kept producing. Naming it in the log turns
 * "unknown vptr" into "you are looking at the sibling class". */
#define GT_VTABLE_BASE   0x141056c50ULL

/* A vptr we have positively identified as NOT ours -- nGUI::OutlineFont, seen
 * in runs 1 and 4. Kept so the log can name it instead of saying "unknown". */
#define GT_VTABLE2_ADDR  0x141101b78ULL

/* The hook target: instance-vtable slot 13.
 *   - unique: appears in exactly one vtable
 *   - callerless: nothing reaches it by a direct call, so the vtable is the
 *     only way in
 *   - ⭐ AN INSTANCE METHOD, which is question 3 and the one the first four
 *     runs kept failing: it reads [this+0x142], [this+0x1e0] and [this+0xd0]
 *     -- deep fields inside the 0x210 object, including +0x1e0 which the real
 *     ctor initialises -- and takes a float in xmm1. That is a per-object
 *     update/draw, not a factory, a destructor or a getter.
 *   - prologue `push rbx; sub rsp,0x30` = 40 53 48 83 ec 30, six bytes over
 *     two whole instructions; find_callers.py --jmp finds no branch into
 *     addr+1..+4. */
/* ⚠ Slot 13 of the REAL instance vtable (0x141056b40). The old value
 * 0x1402fd6d0 was slot 13 of 0x141056c50 -- cGUIObjMessage's -- and left
 * behind when GT_VTABLE_ADDR was corrected. The install guard caught it and
 * refused, which cost a run but prevented patching another class's function.
 *
 * ⭐ This hook is OPTIONAL now. Five runs showed no slot of this vtable is a
 * usable per-frame method, and the ctor probe is what actually captures
 * instances. It is kept only because a per-element method, if this turns out
 * to be one, would show WHEN a label changes. Its failure must never abort the
 * ctor probe -- see the ordering note in gt_install(). */
#define GT_UPDATE_ADDR   0x1402fd6a0ULL
#define GT_VTABLE_SLOT   13

/* ⭐⭐ THE CONSTRUCTOR -- a BIRTH probe, and a better question than the above.
 *
 * Run 5 hooked slot 13 through a real navigation session (the user reached
 * Select Game; the cursor hook fired 10 times and the mod spoke) and slot 13
 * ran ZERO times. Slot 13 is unique to this class's instance vtable, has no
 * direct callers, and is called from 131 generic `call [reg+0x68]` sites, so
 * it is a normal virtual method of the hierarchy. Zero calls on it therefore
 * suggests something stronger than "wrong slot": that NO cGUIObjText was ever
 * INSTANTIATED.
 *
 * ⚠ That is a hypothesis, and hooking more update-shaped methods cannot test
 * it -- every such test confounds "wrong method" with "no objects". The
 * constructor separates them: 0x1402efc10 has exactly ONE caller (the factory
 * 0x1402ef980), so hooking it catches EVERY cGUIObjText that is ever built.
 *   - ctor calls > 0  -> objects exist; slot 13 was simply the wrong method,
 *                        and we now hold real instance pointers anyway.
 *   - ctor calls == 0 -> the shell does not use this class for menu text at
 *                        all, and the whole cGUIObjText route is closed. That
 *                        is a decisive answer, not another inconclusive run.
 *
 * ⭐ Either outcome ends a line of guessing, which is what makes this worth a
 * launch where another update-method guess would not be.
 *
 * ⚠⚠ THE VPTR FILTER MUST NOT RUN ON THE CTOR. At ctor entry the object is raw
 * allocation: the factory stamps the vptr in AFTER the ctor returns
 * (`call 0x1402efc10` then `lea rax,[vtable]; mov [rbx],rax`). Filtering on the
 * vptr here would reject every object and reproduce the same false zero this
 * probe exists to escape. So the ctor detour records unconditionally, and the
 * WORKER re-checks the vptr later -- by which time the factory has written it.
 * Prologue `push rbx; sub rsp,0x20` = 40 53 48 83 ec 20, six bytes over two
 * instructions, no branch-ins. */
#define GT_CTOR_ADDR     0x1402efc10ULL
#define GT_CLASS_SIZE    0x210            /* 528 bytes -- confirmed by the
                                           * factory's own allocation size    */

/* The two candidate pointer fields.
 *
 * ⭐⭐ NO LONGER MERE CANDIDATES: the real constructor (0x1402efc10, reached
 * from the factory) explicitly zeroes BOTH of these, which is what a ctor does
 * to its own members. They are cGUIObjText's fields. Whether either holds the
 * TEXT is still open -- that is what the run answers -- but they are no longer
 * a guess about which offsets belong to this class.
 *
 * ⚠ The standing warning from run 1 still applies to reading them: in the
 * wrong (font) object both offsets held plausible strings purely because they
 * were entries in a glyph-name array. Two candidates both "hitting" is not
 * corroboration; it is what a dense pointer array looks like from outside. */
#define GT_CAND_A        0x1f8
#define GT_CAND_B        0x200

/* Why an install attempt ended the way it did.
 *
 * ⭐⭐ Same reason kbd_state and menu_cursor each have one: a test asserting
 * only "install returned FALSE" passes with every guard deleted, because
 * MinHook independently refuses an unmapped address. Reporting WHICH layer
 * refused is what makes the guards observable. */
typedef enum {
    GT_INSTALL_NOT_TRIED = 0,
    GT_INSTALL_OK,
    GT_INSTALL_BAD_PROTECT,     /* target is not committed executable memory */
    GT_INSTALL_BAD_PROLOGUE,    /* first bytes are not the expected mov      */
    GT_INSTALL_BAD_VTABLE,      /* the vtable slot does not hold the target  */
    GT_INSTALL_MINHOOK_FAILED   /* guards passed; MinHook itself refused     */
} gt_install_status;

BOOL gt_install(void);
gt_install_status gt_install_why(void);
void gt_remove(void);

/* ⭐ Both counts, not just the second. "0 instances captured" means nothing
 * beside an unknown call count: with calls == 0 the hook is not intercepting
 * and NOTHING can be concluded. This project's rule is that "could not
 * measure" must be distinguishable from "measured nothing". */
typedef struct {
    BOOL  installed;
    LONG  calls;      /* how many times the update method ran                */
    LONG  distinct;   /* how many distinct instances were inspected          */
    LONG  faults;     /* guarded reads that faulted while inspecting         */
    /* ⭐⭐ Instances whose vptr was NOT cGUIObjText's. Nonzero means the hook
     * target is reached by something other than this class's vtable -- i.e.
     * the target is wrong. It is surfaced as a COUNT rather than left in the
     * log because that is exactly the failure the first live run hit, and a
     * failure worth a diagnosis is worth a number. */
    LONG  wrong_class;
    /* Calls filtered out in the detour because the object was another class.
     * ⭐ EXPECTED and normal -- this is a shared helper. It is `ours` beside it
     * that says whether the hook is useful: a big other_class with ours == 0
     * means we are hooked where this class never goes. */
    LONG  other_class;
    LONG  ours;       /* calls whose object WAS a cGUIObjText */

    /* ⭐⭐ Constructions seen -- the number that makes every other zero here
     * interpretable. An update hook that never fires is ambiguous: wrong
     * method, or no objects? This separates them, because the ctor has exactly
     * one caller and therefore catches every object of the class.
     *
     * ⚠ Read it beside `ctor_hooked`. ctors == 0 with ctor_hooked == FALSE
     * measures nothing at all -- "could not measure" must never look like
     * "measured zero". */
    LONG  ctors;
    BOOL  ctor_hooked;

    /* ⭐ Sweep accounting. `distinct` counts objects READ; `with_text` counts
     * those that actually held a string. Reporting both is what makes a quiet
     * log interpretable: "swept 400, 0 with text" is a finding, while a bare
     * absence of log lines could equally mean the sweep never ran. */
    LONG  with_text;
} gt_stats;

void gt_get_stats(gt_stats *out);

/* Inspect one captured instance and log what its 528 bytes contain: the two
 * candidate pointer fields, every other pointer-shaped field that resolves to
 * readable text, and any inline string. Runs on the WORKER thread.
 *
 * ⭐ Returns FALSE when there is nothing new to report, so a quiet log means
 * "no new instance", never "inspected and found nothing" -- those two are
 * different findings and must not share a representation.
 *
 * ⚠ This is the DISCOVERY step, not the product. It exists to find which
 * offset holds the string; once that is known this becomes a targeted read and
 * the dump goes away. */
BOOL gt_poll_and_log(void);

/* --- test seams ----------------------------------------------------------
 * ⭐⭐ The stand-in must NOT re-derive what the shipping code derives. A
 * harness that recomputes the mechanism under test leaves the suite green with
 * that mechanism deleted -- this project has been bitten by exactly that twice
 * (memory: harness-supplies-the-mechanism). So the test hands in a buffer and
 * asks what the inspector CONCLUDED about it; it never asks the inspector to
 * confirm an answer the harness already knows how to compute. */

/* Drive the shipping detour body with a synthetic instance pointer. Returns
 * what the stand-in "original" returned, so a test can prove the return value
 * is passed through untouched. */
void *gt_test_body(void *self, void *arg);

/* What did the detour record? Returns the most recently captured pointer. */
void *gt_test_last_captured(void);

/* Drive the shipping CONSTRUCTOR detour with a synthetic object.
 *
 * ⭐⭐ The property that matters here is the opposite of the update detour's:
 * the ctor hook must capture UNCONDITIONALLY, with no vptr filter, because at
 * ctor entry the vptr has not been written yet. A filter here would reject
 * every object and produce exactly the false zero the probe exists to escape.
 * So the test hands it an object whose vptr is deliberately NOT ours and
 * requires it to be captured anyway. */
void *gt_test_ctor_body(void *self);

/* Run the shipping INSPECTOR over a caller-supplied buffer that stands in for
 * an instance, and report what it found. "text_offset" is set to the offset
 * the inspector believes holds the string, or -1 if it reached no conclusion.
 *
 * ⭐ -1 is a RETURN VALUE, not an all-zero result: "could not tell" and
 * "offset 0" must not look alike (memory: inconclusive-must-be-a-return-value).
 *
 * Returns TRUE if the inspector ran to completion over the whole buffer. */
BOOL gt_test_inspect(const void *buf, SIZE_T len, int *text_offset);

/* Reset all captured state, so one test cannot inherit another's. */
void gt_test_reset(void);

#endif /* CFC2_GUI_TEXT_H */
