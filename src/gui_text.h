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
 * WHY *THIS* HOOK TARGET
 * ----------------------
 * Vtable slot 5 -> 0x1402f9720. Three properties were CHECKED, not assumed:
 *
 *  1. ⭐⭐ It is UNIQUE TO cGUIObjText. A search of every data section for the
 *     qword 0x1402f9720 finds it in exactly ONE 8-aligned slot, 0x1410563d0 =
 *     this class's vtable + 0x28. So every "this" it receives really is a
 *     cGUIObjText, and no type check is needed at all.
 *     ⛔ The first target considered, 0x1402fed50, FAILED this test: it appears
 *     in NINE vtables and in none of them is it cGUIObjText's. Hooking it would
 *     have captured objects of other classes and then read 528 bytes out of
 *     things that are not 528 bytes long. The check took one search and it
 *     changed the target.
 *  2. Its first instruction, "mov [rsp+8],rbx" = 48 89 5c 24 08, is exactly
 *     FIVE bytes, so MinHook's patch lands on a clean instruction boundary.
 *  3. Nothing branches into those five bytes: find_callers.py --jmp was run
 *     against addr+1..addr+4 and found zero sites. A branch landing
 *     mid-prologue would jump into the middle of the trampoline patch.
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
#define GT_UPDATE_ADDR   0x1402f9720ULL   /* vtable slot 5 -- the hook target */
#define GT_VTABLE_SLOT   5                /* which slot must hold that value  */
#define GT_CLASS_SIZE    0x210            /* 528 bytes, from the reg stub     */

/* The two candidate pointer fields the offline accessors revealed. Checked
 * first when inspecting an instance -- and reported as candidates, never as
 * the answer. */
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
