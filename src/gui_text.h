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
 * WHAT WAS FOUND OFFLINE
 * ----------------------
 *   - cGUIObjText: name string 0x141056398 (inline ASCII, a positive ID),
 *     vtable immediately after it at 0x1410563a8, class size 0x210 (528)
 *     bytes from its registration stub (mov r9d,0x210 at 0x14002e823).
 *   - Base-class layout, from the ctor 0x1402f0080 and dtor 0x1402f0980:
 *     +0x00 vptr, +0x54 flags, +0x58 float, +0x60 bitfields, and
 *     ⭐ +0x70 = FIRST CHILD, +0x78 = NEXT SIBLING -- GUI objects form a tree.
 *   - ⭐ Two one-line accessors near the class's own code return POINTERS from
 *     [this+0x1f8] (0x1402fc410) and [this+0x200] (0x1402fc400). Both are
 *     inside the 0x210-byte object and both are the right SHAPE for a text
 *     buffer or a string object. They are the first thing this module looks at
 *     -- but they are candidates, not the answer. Only live bytes decide.
 *
 * ⛔ THE STATIC ROUTE IS CLOSED -- chased to the end, twice
 * The vtable has exactly ONE data reference (the DTI slot 0x1422cae28), and the
 * only code that touches that slot is four one-line getDTI() accessors. There
 * is no constructor to read and the DTI carries no property table. The single
 * "lea rax,[vtable]" site in the whole image (0x14002e786) is the REGISTRATION
 * block, not a ctor. Nothing static reaches an instance.
 *
 * ⭐ This is the third time on this project (sPad, sKeyboard, now this): the
 * engine walks its metadata at runtime, so a class layout has to come from a
 * LIVE object. Stop looking for it in the image.
 *
 * WHY *THIS* HOOK TARGET -- and ⛔⛔ THE TEST THAT WAS NOT ENOUGH
 * -------------------------------------------------------------
 * Vtable slot 1 -> 0x1402efa10. THREE properties, and the third is the one
 * this module learned the hard way IN A LIVE RUN:
 *
 *  1. It appears in exactly ONE 8-aligned data slot in the whole image --
 *     cGUIObjText's vtable.
 *  2. ⭐⭐ IT HAS ZERO DIRECT CALLERS. Nothing does "call 0x1402efa10", so the
 *     vtable is the ONLY way to reach it, and every "this" it receives really
 *     is a cGUIObjText.
 *  3. Prologue "push rbx; sub rsp,0x20" = 40 53 48 83 ec 20, SIX bytes across
 *     two whole instructions, clearing MinHook's five-byte minimum on a clean
 *     boundary; and find_callers.py --jmp finds zero branches into addr+1..+4.
 *
 * ⛔⛔ PROPERTY 2 IS THE WHOLE LESSON, AND IT COST A GAME LAUNCH.
 * The first shipped target was slot 5, 0x1402f9720. It PASSED property 1 --
 * genuinely unique, genuinely in this class's vtable at +0x28 -- and it was
 * still WRONG. The live run captured an object whose vptr was 0x141101b78:
 * nGUI::OutlineFont, a font object, full of Adobe glyph names ("notequal",
 * "summation", "uniFE82"). Because 0x1402f9720 is a SHARED HELPER that three
 * sites also reach by a direct "call rel32" (0x1402f63d9, 0x1402f6e65,
 * 0x1402f892a), and those callers pass their OWN this.
 *
 * ⭐ So the question "is this address unique to the class?" is the WRONG
 * question. The right one is "is this address REACHED ONLY through the class's
 * vtable?" -- which needs the caller count, not just the vtable count. A vtable
 * slot may hold code shared with anything.
 *
 * ⭐⭐ Only FOUR of cGUIObjText's 28 slots satisfy both (1 vtable, 0 callers):
 * 1, 2, 6 and 27. Slot 2 is the destructor and 27 a deleting destructor -- the
 * text is being torn down there, which is the wrong moment to read it. Slot 6
 * is a one-line "lea rax; ret" getDTI() stub, too short to patch. Slot 1 is
 * the only real function among them, which is why it is the target.
 *
 * ⭐ AND THE GUARD DID ITS JOB. The run did not report glyph names as menu
 * labels: the vptr check in gt_poll_and_log() compared the live vptr against
 * this class's and said loudly that every offset below it was unattributed.
 * A positive identity check on the object, not merely on the code address, is
 * what turned a wrong target into a diagnosis instead of a false finding.
 *
 * ⛔⛔ AND SLOT 1 IS *ALSO* WRONG -- second live run, 2026-09-09.
 * It passes both properties above and it is STILL not the target, for a third
 * reason neither test asks about: WHAT KIND of method it is.
 *
 * Slot 1 is the class FACTORY. Read its body (0x1402efa10): it allocates 0xe0
 * bytes via a virtual call, runs the base ctor 0x1402f0080, stores a vtable at
 * [obj+0], zeroes +0xa0..+0xd8 and RETURNS THE NEW OBJECT in rax. So it is
 * called ON THE CLASS DESCRIPTOR, not on an element -- and the run showed
 * exactly that: ONE distinct "instance" for 61 calls, 0x1422cae28, which is
 * cGUIObjText's DTI slot. Its "text" fields were the GUI class NAMES
 * ("cGUIObjRoot", "cGUIObjPolygon"): the descriptor table, not a label.
 *
 * ⚠ Note the vptr check PASSED here -- the descriptor legitimately carries the
 * class's vtable. So a matching vptr proves the class, NOT that the pointer is
 * an instance. Both runs were caught, but by different means: run 1 by the
 * vptr, run 2 only by noticing ONE distinct pointer across 61 calls.
 *
 * ⭐⭐ THE THREE QUESTIONS, now complete, in the order they must be asked:
 *   1. Is the address in exactly one vtable?      (slot 5 passed, was wrong)
 *   2. Does it have zero direct callers?          (slot 1 passed, was wrong)
 *   3. Is it an INSTANCE method -- does it run per-element, per-frame, on
 *      objects the game has already built?        <- the one that decides it
 * A constructor, a destructor and a factory all satisfy 1 and 2 and none of
 * them hands over a live, populated text element.
 *
 * ▶ WHAT IS LEFT. Of the four slots satisfying (1) and (2) -- 1, 2, 6, 27 --
 * slot 1 is the factory, 2 and 27 are destructors (the text is being torn
 * down) and 6 is a one-line getDTI() stub. ⛔ SO NO SLOT OF THIS VTABLE IS
 * BOTH SAFELY UNIQUE AND AN INSTANCE METHOD. The per-frame methods are the
 * shared helpers, and they are shared precisely because they are generic.
 *
 * ⭐ The way forward looked like: keep a shared helper as the hook (they DO
 * run per element per frame) and discriminate on the OBJECT by vptr.
 *
 * ⛔⛔ RUN 3 KILLED THAT TOO, AND THE NUMBER IS THE POINT.
 * Slot 5 with the vptr filter: **10 calls, 0 of them cGUIObjText**, and the
 * count STAYED at exactly 10 across a further 55 seconds of the game running.
 * So 0x1402f9720 is not a per-frame method at all -- it runs ~10 times during
 * startup, on other classes, and never again. The assumption that it was "the
 * update method" was never checked; it came from the slot's position, and the
 * slot's position was a guess.
 *
 * ⭐ The filter itself is PROVEN GOOD by that same run: 10 calls in, 10
 * correctly classified as other classes, 0 faults, 0 wrong-class leaks. Keep
 * it. It is the mechanism that turns any future shared-helper hook into a
 * usable one, and it is now tested both offline (case 2b) and live.
 *
 * ▶ WHAT THE THREE RUNS ACTUALLY ESTABLISH, so the next attempt starts here:
 *   - The whole vtable is accounted for. Slots 1/2/6/27 are factory,
 *     destructor, getDTI stub, deleting destructor. Slots 0/3/4/7/11/15-18/
 *     20-22/26 are shared across hundreds or thousands of vtables. What is
 *     left -- 8/9/10/12/13/14/19/23/24/25 -- are tiny accessors (`mov eax,
 *     [rcx+0x54]; ret` and the like), too short to patch and carrying no text.
 *   - ⛔ SO cGUIObjText's VTABLE DOES NOT CONTAIN A HOOKABLE PER-FRAME METHOD.
 *     That is a property of the class, not a failure to look hard enough.
 *
 * ⭐⭐ THE NEXT ROUTE, and why it is different in kind: stop trying to catch
 * the object being DRAWN and catch it being FILLED. Something copies a GMD
 * string into an element, and that writer takes the element and the message as
 * ARGUMENTS -- the shape that has worked four times on this project (sPad,
 * sKeyboard, the GMD table, the cursor chokepoint) and that scanning has
 * failed at every time. Idea 2 in cfc2-gui-text-route, never tried:
 * find the call that takes an rGUIMessage and an element.
 *
 * ⚠ And the cheap check to run FIRST, before any of that: cGUIObjMessage
 * (0x170 bytes, vtable/DTI already known) may be the class that actually holds
 * menu label text, with cGUIObjText being the free-text/font-rendering half.
 * Nothing in three runs has yet shown a cGUIObjText carrying a menu label.
 *
 * ⚠⚠ SAFETY -- THIS IS THE RISKIEST THING IN THE PROJECT SO FAR
 * It receives a pointer from the game and reads half a kilobyte through it, on
 * the game's own threads. This project has already KILLED THE GAME once by
 * trusting a caller-supplied pointer (memory: hooked-function-pointers-are-
 * untrusted). So, in order:
 *
 *   - The DETOUR ITSELF DEREFERENCES NOTHING. It records the pointer value and
 *     calls the original. That is all. Recording a value cannot fault.
 *   - ⭐ All reading happens LATER, on our worker thread, so a fault can never
 *     land inside a game call.
 *   - Every read is VirtualQuery-checked and runs under the thread-local fault
 *     guard, every time, never cached -- a GUI object is freed when its screen
 *     changes, and a pointer that passed a check a moment ago is not a pointer
 *     that is still valid.
 *   - The original is called unconditionally and its return value passed
 *     through untouched, so the game's control flow does not depend on us.
 *   - Off switch ships WITH the experiment: cfc2_hook.off disables it along
 *     with every other hook.
 *
 * ⭐ AND IT IS OFF BY DEFAULT, behind cfc2_guitext.on. That gate is for an
 * UNPROVEN read on a per-frame path; it comes off once the field offset is
 * known and the read becomes targeted (memory: a-guard-outlives-its-bug).
 */
#ifndef CFC2_GUI_TEXT_H
#define CFC2_GUI_TEXT_H

#include <windows.h>

/* Live addresses. ASLR is off and the exe has no relocations, so these are
 * valid in every future run. */
#define GT_VTABLE_ADDR   0x1410563a8ULL   /* cGUIObjText's vtable             */
/* ⭐⭐ BACK TO SLOT 5 -- DELIBERATELY, and with the filter that makes it work.
 * It is a SHARED helper, which is exactly why it runs per element per frame,
 * which is what we need. Its problem was never that it fires too rarely; it
 * was that it also receives other classes' objects. So we keep the hook and
 * DISCRIMINATE ON THE OBJECT: the vptr filter below. Run 1 already proved the
 * filter works -- it rejected every OutlineFont it was handed. */
#define GT_UPDATE_ADDR   0x1402f9720ULL   /* vtable slot 5 -- shared helper   */
#define GT_VTABLE_SLOT   5                /* which slot must hold that value  */
#define GT_CLASS_SIZE    0x210            /* 528 bytes, from the reg stub     */

/* The two candidate pointer fields the offline accessors revealed. Checked
 * first when inspecting an instance -- and reported as candidates, never as
 * the answer.
 *
 * ⚠ The first live run is a standing warning about them: in the (wrong) font
 * object BOTH offsets held plausible strings -- "uni0623" and "uniFE84" --
 * purely because they were two more entries in a glyph-name array. Two
 * candidates both "hitting" is not corroboration; it is what a dense array of
 * pointers looks like from the outside. Only the vptr check told them apart. */
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
