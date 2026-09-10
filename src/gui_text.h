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
/* ⭐⭐⭐ THE ANSWER, FOUND OFFLINE: THE TEXT SETTER. slot 26 -> 0x1402f0b50.
 *
 * Six runs were spent asking "which method hands me a populated object?" That
 * was the wrong question. The right one -- and the shape that has worked 4/4 on
 * this project -- is "who RECEIVES the text as an ARGUMENT?"
 *
 * The chain, read entirely from the dump:
 *   - The factory zeroes [obj+0x1f8] and [obj+0x200] (0x1402ef9e1/9e8), which
 *     is why those two offsets looked like the text fields.
 *   - ⛔ They are NOT a string. 0x1402f0d03/0d0a write BOTH from the SAME
 *     value, copied out of [rbx+0xd0] -- a begin/end pointer pair over a
 *     buffer that +0xd0 owns. And 0x1402f1159 sets +0x1f8 from
 *     [manager+0x3118] behind a dirty-flag at +0x1e4: that is FONT assignment.
 *     Reading +0x1f8 as the label would have reported a font object.
 *   - +0xd0 is filled at 0x1402f0bca, inside 0x1402f0b50, from the result of
 *     two calls (0x14022af10, 0x14022b550) that take the caller's `rdx`.
 *
 * ⭐ So 0x1402f0b50 is setText(cGUIObjText *this [rcx], const char *s [rdx],
 * ...). It passes all three questions AND the one that matters:
 *   1. unique -- exactly one 8-aligned data slot in the image (0x141056c10);
 *   2. zero direct callers -- reachable only through the vtable;
 *   3. ⭐ it is an INSTANCE method that receives the TEXT ITSELF, so there is
 *      no waiting for an object to be filled and no timing trap at all.
 * Prologue `mov [rsp+8],rbx` = 48 89 5c 24 08, five bytes, one instruction, no
 * branch-ins into addr+1..+4.
 *
 * ⭐ `rdx` is a plain char*, established not assumed: the builder walks it a
 * BYTE at a time (`cmp byte ptr [rdx],0xa`, `cmp byte ptr [rdx+1],0x3b` at
 * 0x14022b71b/776), so it is 8-bit -- UTF-8 from the GMD, not UTF-16.
 *
 * ⚠ Slot 26 is overloaded across classes: one caller (0x1400f93ca) passes a
 * 32-bit value in edx, so it is NOT a char* for every class that shares this
 * slot. The vptr filter is what makes reading rdx safe, and it is why that
 * filter is kept even though this target is far better than the last five. */
/* ⭐⭐⭐ IT WORKS -- confirmed live 2026-09-09 21:19.
 *   324 calls, 324 strings captured, 0 faults, and the overload is named:
 *   cGUIObjText SLOT 25 (0x1402f0d80). Slot 26 never fires; nor does either
 *   cGUIObjMessage overload. Hooking one and guessing would have been wrong
 *   FOUR ways out of four.
 *
 * Captured verbatim:
 *   "<FONT 2>PRESS <SIZE 80><ICON DECIDE></SIZE> TO START</FONT>"
 *   "Choose the game you want to play.  <SIZE 58><ICON BTN_START></SIZE>
 *    Training <SIZE 58><ICON BTN_X></SIZE>Game Settings ... Back"
 *   "P1"
 *
 * ⭐ So the shell's text is UTF-8 with inline engine MARKUP: <FONT n>, <SIZE n>
 * and <ICON NAME> tags, exactly matching the ICON token vocabulary already
 * catalogued in the GMD corpus. Speaking these raw would read tag soup to the
 * user; they need stripping, and the ICON names need mapping to spoken words
 * ("DECIDE" -> the confirm button). That is the next piece of work and it is
 * ordinary string handling, not reverse engineering.
 *
 * ⚠ Note what the strings ARE: whole help/prompt STRIPS, not per-item labels.
 * A menu row's own caption may come from elsewhere. That is a question for the
 * next run, not an assumption to bake in here.
 *
 */

/* ⛔⛔ THERE ARE FOUR setText FUNCTIONS, NOT ONE, AND I HOOKED ONE OF THEM.
 *
 * The run of 2026-09-09 20:42 said so plainly: 53 objects CONSTRUCTED and
 * setText ran ZERO times. The class was right; the overload was not.
 *
 * All four write the same field (+0xd0) and share an identical prologue and
 * signature. They sit at slots 25 and 26 of TWO different classes:
 *
 *     cGUIObjText    vtable 0x141056b40   slot 25 = 0x1402f0d80
 *                                         slot 26 = 0x1402f0b50   <- hooked
 *     cGUIObjMessage vtable 0x141056c50   slot 25 = 0x1402f0e50
 *                                         slot 26 = 0x1402f0c40
 *
 * ⭐ And cGUIObjMessage is if anything the MORE likely holder of menu label
 * text -- its factory (0x1402ef930) allocates 0x1e0 bytes and stamps
 * 0x141056c50, the vtable I spent two runs misattributing to cGUIObjText.
 *
 * ⚠ THE MISTAKE, AND IT IS THE SAME ONE AGAIN: I found A writer, confirmed it
 * was real, and stopped -- without asking whether it was the ONLY one. The
 * offline evidence for four was there the whole time: my own +0xd0 scan listed
 * SIX writers, and I followed exactly one of them.
 *
 * ⭐ So hook all four and let the run say which fires. Four hooks cost nothing
 * extra -- the vptr filter already discriminates, and each carries its own
 * counter, so the log names the overload the shell actually uses instead of
 * leaving another guess to be paid for with a launch. */
#define GT_SETTEXT_COUNT 4
#define GT_SETTEXT_A     0x1402f0d80ULL   /* cGUIObjText    slot 25 */
#define GT_SETTEXT_B     0x1402f0b50ULL   /* cGUIObjText    slot 26 */
#define GT_SETTEXT_C     0x1402f0e50ULL   /* cGUIObjMessage slot 25 */
#define GT_SETTEXT_D     0x1402f0c40ULL   /* cGUIObjMessage slot 26 */

/* cGUIObjMessage's instance vtable, so its objects pass the filter too. Its
 * factory 0x1402ef930 allocates 0x1e0 and stamps this. */
#define GT_VTABLE_MSG    0x141056c50ULL

#define GT_SETTEXT_ADDR  GT_SETTEXT_B
#define GT_SETTEXT_SLOT  26

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

    /* ⭐ Calls to the text setter. Beside `with_text` it says which of two very
     * different faults you are looking at: 0 calls means the hook never fired;
     * many calls with 0 texts means the vptr filter rejected them all. */
    LONG  settext_calls;

    /* ⭐ Per-overload counts, in the order cGUIObjText.25, .26,
     * cGUIObjMessage.25, .26. The previous run could only say "setText ran 0
     * times" because one of four was hooked; this says WHICH fires. */
    LONG  settext_each[GT_SETTEXT_COUNT];
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

/* ---- The focus channel: item 4's measurement, and the caption source. ----
 *
 * ⭐⭐ THE OPEN QUESTION THIS ANSWERS. Does the shell push a per-ROW caption
 * through setText when the cursor moves? The corpus proves such strings exist
 * and are distinct (docs/screen-id-from-prompt-text.md); it does NOT prove the
 * game sets one per row change -- the 21:19 run saw 6 distinct strings from 324
 * calls. Nothing here assumes the answer.
 *
 * ⛔ NOT A TIMING JOIN. docs/rethink-after-gui-text.md forbids correlating
 * captured text with cursor indices by time, because 3 of 6 captures in that
 * run coincided with NO cursor movement. Instead the cursor side stamps a
 * MOVEMENT SEQUENCE NUMBER, and records carry the stamp current when they
 * arrived -- an ordering fact. If captions do not track the cursor, the record
 * shows the same few strings repeating across many move numbers, which is a
 * clean NO rather than a fabricated correlation. */

/* Buffer size a caller must provide to gt_take_caption(). Matches the capture
 * buffer, so a caption can never be truncated further than the game's own
 * capture already was. */
#define GT_CAPTION_MAX 256

/* Called by the cursor hook on a real movement. Bumps the sequence AND counts
 * the notification, so the report can tell "no captions" from "the cursor side
 * never called us" -- without this count the negative is unfalsifiable. */
void gt_note_focus_move(void);

/* The newest PROSE caption recorded since the last movement, if any.
 *
 * ⚠ Returns FALSE when there is none, when the recorder is busy, or when the
 * hook is not installed -- all of which mean "say nothing", never "say the
 * previous row's caption". Hint strips (which carry markup) are skipped: they
 * describe the SCREEN and do not change per row, so speaking one on every
 * movement would be noise. */
BOOL gt_take_caption(char *out, int outlen);

/* Log the whole ordered record, with a positive count of what was examined and
 * an explicit reading guide. Refuses to conclude if the cursor side never
 * notified us. */
void gt_focus_report(void);

/* ⭐⭐ Log the LIVE INSTANCE SET: every cGUIObjText the hook has seen, keyed by
 * OBJECT, with its latest text and whether the pointer is still a valid object.
 *
 * Step 1 of docs/gui-instance-route.md. The question: does the live set contain
 * one text per menu row, and can the focused one be told apart? If so, item
 * names can come from the instance tree for ANY screen -- including Gallery,
 * the music list and the consent dialog, which neither the GMD nor the caption
 * channel can reach.
 *
 * ⛔ NOT route 3 again. That anchored on the name POOL (file data, static by
 * design, closed on a clean negative). This anchors on a pointer the game hands
 * us in setText -- an object it CONSTRUCTED. Route 3's closing note says the
 * root must come from "the GUI system's own instance list"; this is that list.
 *
 * ⚠ LOGGED, NEVER SPOKEN. A live object set is not yet a row->text mapping. */
void gt_instance_report(void);

/* ---- The SELECTION probe: step 2 of docs/gui-instance-route.md -----------
 *
 * THE QUESTION: when the cursor moves, does anything inside a cGUIObjText say
 * "I am the selected one"? Step 1 gave each row a stable object with correct
 * text; what is missing is which one is FOCUSED. A numbered list (music tracks)
 * does not need it, but Gallery's unnumbered logos do.
 *
 * ⛔⛔ THE SAME-SCREEN CONTROL IS THE DESIGN, and it is route 3's parting
 * lesson: "without it, everything that merely ticks looks like a signal." Three
 * snapshots -- one pair with NO movement (the control), one pair across a
 * movement (the signal). Bytes that change in both are animation or timers and
 * are SUBTRACTED. A plain before/after diff would have been worthless.
 *
 * Usage: press F6, hold still, then move ONE item when the log says to. */
void gt_selection_arm(void);

/* Take the next snapshot, or -- on the third -- print the subtracted result.
 * Idle and free when not armed. Runs on the worker thread, never in a detour. */
void gt_selection_poll(void);

/* The subtraction itself, exposed so a test drives the SHIPPING code rather
 * than a copy. `a`->`b` is the control (no movement), `b`->`c` the signal (one
 * movement); a byte differing in the control is a ticker and never becomes a
 * candidate. Accumulates so several objects vote. */
void gt_selection_subtract(const BYTE *a, const BYTE *b, const BYTE *c,
                           int *ticker, int *signal);

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

/* Drive the shipping TEXT SETTER detour with a synthetic object and string.
 * `out`/`outlen` receive whatever the detour published, so the test asks what
 * was CAPTURED rather than re-deriving what the detour should have done.
 * Returns the value the stand-in original returned, to prove pass-through. */
void *gt_test_settext_body(void *self, const char *s, char *out, int outlen);

/* Drain the text mailbox the way the worker does. Returns TRUE if it held
 * something. ⭐ Exists so a test can prove the READER side works under load --
 * the half that failed silently in the first live run. */
BOOL gt_test_take_text(char *out, int outlen);

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

/* Test-only: enable the caption path without a real install. The shipping
 * `g_installed` guard is load-bearing (the mod must not read a recorder no hook
 * is filling), so it stays -- but a suite that cannot reach the code cannot
 * test it either. */
void gt_test_focus_enable(void);

/* Test-only: perform the window clear that gt_focus_report() does, so the
 * "recorder does not go blind when full" case can run offline. */
void gt_test_focus_clear(void);

/* Test seams for the instance set: how many objects, and what does one say?
 * ⚠ Asserting the LOG TEXT instead would test the formatter, not the table. */
int gt_test_instance_count(void);
int gt_test_instance_text(void *obj, char *out, int outlen);

/* ⭐⭐ Is item 4's measurement CONCLUSIVE? Returns FALSE when the cursor side
 * never notified the recorder -- in which case every record carries sequence 0
 * and "no captions tracked the cursor" is indistinguishable from "the cursor
 * hook never ran". gt_focus_report() refuses to draw a conclusion on FALSE.
 *
 * ⚠ This is a RETURN VALUE, not a logged warning, for the reason this project
 * has re-learned repeatedly: if the code notices it cannot measure something,
 * a caller must be able to act on that, and a test must be able to assert it.
 * `*records` and `*moves` report the positive counts behind the verdict.
 * (memory: inconclusive-must-be-a-return-value) */
BOOL gt_focus_conclusive(long *records, long *moves);

#endif /* CFC2_GUI_TEXT_H */
