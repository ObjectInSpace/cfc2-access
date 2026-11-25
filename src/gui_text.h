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
 * values passed through untouched. Always installed;
 * cfc2_hook.off disables it along with every other hook.
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

/* ⭐⭐ The newest prose, and ONLY IF IT HAS ARRIVED SINCE THE LAST CALL.
 *
 * ⛔ For a view that produces NO cursor movement, where gt_take_caption()'s
 * movement stamp cannot apply, and a retained history would re-fire
 * forever. The caller sees each string once; re-opening the view sets the text
 * again and that counts as a new arrival.
 *
 * ⚠ Returns FALSE when nothing new has arrived -- the normal case per poll. */
BOOL gt_newest_prose_if_new(char *out, int outlen);

BOOL gt_take_caption(char *out, int outlen);

/* ⭐⭐ Dump each live text object's raw fields, looking for one that orders
 * objects by SCREEN POSITION. Two screens need it -- the Lobby ID field and
 * the Music chooser's game strip -- because on both the text is readable and
 * the caption channel is silent, so insertion order is the only join left and
 * insertion order is DEMONSTRABLY WRONG (the edited slot is first in one run
 * and last in another).
 *
 * ⚠ A dump, not a detector: it prints offsets and values and leaves the
 * identification to a human with a known on-screen order to compare against.
 * Guessing which field "looks like" a coordinate is the shape-matching that
 * went 0 for 4 on this project.
 *
 * Switch: cfc2_layout.on. Absent = off, and it does nothing at all. */
/* ⭐⭐⭐ Hunt the MODAL SELECTION: snapshot the parent object both option
 * widgets point at (+0x088) and report what changes when the user moves.
 *
 * ⭐ Anchored, not a blind scan: the anchor is a pointer the game itself shares
 * between exactly the two widgets of one control, and the control is the user
 * pressing a direction. Reports OFFSETS AND VALUES only -- never a conclusion.
 *
 * Switch: cfc2_modal.on. Absent = off. */

/* ⭐⭐ THE NUMBERED-LIST LOOKUP: find a live object whose text begins with the
 * decimal number `n` followed by '.', and copy that text out.
 *
 * Built for the music player, whose five tracks are captured here in full --
 * "01. Title: Take Your Best Shot" -- and are reachable by NOTHING else: the
 * titles are not in the GMD corpus (they are not message labels at all), and the
 * caption channel cannot see them either.
 *
 * ⛔⛔ WHY IT MATCHES ON THE PRINTED NUMBER RATHER THAN INSERTION ORDER.
 * Insertion order DID equal row order in both runs, and relying on it would have
 * worked -- and would have been the "fits the shape" mistake this project keeps
 * paying for. The number the game itself printed is an INDEPENDENT statement of
 * which row the text belongs to, so a match is two sources agreeing (the
 * cursor's index and the game's own numbering) rather than one source trusted
 * twice. A scrolled window, a re-ordered list or a stale table then yields NO
 * match -- silence -- instead of confidently naming the wrong track.
 *
 * ⚠ THIS IS A LOOKUP, NOT A MAILBOX. Unlike gt_take_caption, nothing here is
 * stamped with a movement: the music list's text is set ONCE when the screen
 * opens (5 sets each, all at one move seq) and moving the cursor sets NOTHING.
 * That is measured, not assumed -- and it is exactly why the caption channel
 * reads this screen as silent. So the text must be pulled by index on demand.
 *
 * ⚠ Pointers are re-validated (vptr must still be ours) exactly as the report
 * does, because a GUI object is freed on a screen change and the table keeps
 * entries until they are evicted.
 *
 * Returns TRUE only if exactly ONE live object matched. Two objects claiming the
 * same number is a contradiction, not a tie to be broken -- it returns FALSE. */
BOOL gt_numbered_item(int n, char *out, int outlen);

/* ⭐⭐⭐ Name the item at cursor POSITION by the objects' own screen
 * coordinates (+0x040 X, +0x044 Y, floats). `match` filters the instance set
 * down to this screen's objects; the coordinates then ORDER them.
 *
 * ⭐ The layout axis is DERIVED -- it sorts by whichever of X/Y actually
 * varies -- so it serves vertical menus and horizontal strips alike. The lobby
 * ID field is a ROW (one Y, X 64px apart); every menu is a COLUMN.
 *
 * Refuses (returns FALSE, out empty) when the candidate count disagrees with
 * `total`, or two candidates share a coordinate. Both mean the ordering is not
 * this screen's, and a bare position is always true where a guess is not. */
typedef BOOL (*gt_cand_fn)(const char *text, void *ctx);
BOOL gt_item_at_position(int position, int total, gt_cand_fn match, void *ctx,
                         char *out, int outlen);

/* ⭐⭐ A WIDGET OF ONE SCREEN, by the parent pointer the game itself stamps at
 * +0x088 -- and optionally by column and row.
 *
 * ⭐ MEMBERSHIP COMES FROM THE GAME, not from a guess about the text. Measured
 * across three runs on three different screen pointers: every move-name object
 * (X=84), every input-notation object (X=1565) and the screen's heading
 * (X=960) all carry +0x088 == the cursor helper's own `screen` argument. Same
 * machinery as MODAL_PARENT_OFF, at screen scale.
 *
 * ⛔ Why a text-shaped filter will not do here: the move list's CONDITION
 * column (" During Hyakkishu", X=633) is a leading-space capitalised string
 * indistinguishable from a move name. Only the coordinate separates them.
 *
 * `x`   -- the column, or a negative value for "any column".
 * `y`   -- the row's Y, or a negative value for "any row". Matched within
 *          GT_COORD_EPS, because these are floats from the game.
 *
 * ⚠ X=960 is SHARED CHROME: the pause menu, Select Game and Training Options
 * all draw a heading there, and in one run its sightings carried SIX different
 * parents. So the parent check is what makes a column query safe, and passing
 * a column alone would be wrong.
 *
 * Returns TRUE only when EXACTLY ONE live object matches -- two matches is a
 * contradiction, not a tie to break. */
#define GT_COORD_EPS 1.0f
BOOL gt_widget_of(const void *screen, float x, float y, char *out, int outlen);

/* The Y of the Nth object (1-based) in `screen`'s column at `x`, sorted top to
 * bottom. ⭐ This is how a row in one column finds its partner in another --
 * the move list's name and its input notation share a Y. */
BOOL gt_column_y(const void *screen, float x, int position, float *out_y);

/* How many live objects `screen` has in the column at `x`. ⭐ This is the
 * VISIBLE count, which on the move list is NOT the cursor's total: the cursor
 * counts panels (reporting "of 3") while ten move names are on screen. */
int gt_column_count(const void *screen, float x);

/* ⭐⭐ THE WRITE STAMP: which labels belong to the page now showing.
 *
 * The game keeps a page's label objects alive after it stops showing them.
 * The EX settings pages of CVS1, CFE, Power Stone and Plasma Sword are
 * shorter than the page before them, so page 1's last labels stay -- on CVS1
 * "Battle Type" at the same Y as the real "Default" row, with page 1's value
 * "Normal" beside it -- and every read on those pages refused.
 *
 * +0x138 is the GUI frame on which the game last WROTE the label
 * (0x1402F6170, reached from the text classes' method 0x1402F9AB0, copies the
 * counter [[0x1422C8B70]+0x48]). It is NOT refreshed while a label merely
 * stays on screen: the live stamps (build/captures/stamps-20261015) put the
 * page's four rows at 1766, page 1's leftovers at 1700, the still-visible
 * heading at 1692 and freshly changed values at 1798, with the counter at
 * 1799.
 *
 * ⛔ So "old" does NOT mean hidden -- treating a label not refreshed this
 * frame as gone silenced nearly every menu (2026-10-15). What the stamp does
 * show is which labels a page change wrote TOGETHER. */
#define GT_STAMP_OFF   0x138
#define GT_PAGE_SLACK  2      /* frames one page's rows may span when written */

/* The row column of the page now showing, for a screen whose cursor reports
 * `total` rows. If the column holds exactly `total` labels it is used as it
 * is -- every screen that read correctly before is unchanged. Otherwise only
 * the labels written in the column's latest write (within GT_PAGE_SLACK of
 * its newest stamp) are this page's; a value partner must have been written
 * since then as well. The caller still refuses unless the count then equals
 * `total`. total < 0: no row count is known, so the column is used as is. */
int  gt_page_count(const void *screen, float x, int total);
BOOL gt_page_item(const void *screen, float x, float pair_x, int total, int position,
                  char *out, int outlen);

/* Call `fn` with the current text of every LIVE object on `screen` (membership
 * by the game's own +0x088 parent pointer, as gt_widget_of). Returns how many.
 *
 * ⭐ "What is on this screen now", from the live objects -- not from a history
 * of recent strings, which fills and keeps other screens' text. Markup is
 * passed through untouched; the caller decides what it wants. */
int gt_screen_texts(const void *screen, gt_cand_fn fn, void *ctx);

/* The Nth object (1-based, top to bottom) of `screen`'s column at `x`, with the
 * partner at `pair_x` on the same row appended after ", " when one exists.
 *
 * ⭐⭐ THIS IS HOW A WHOLE PANEL IS READ. The move list shows several moves at
 * once and the cursor moves between PANELS, not rows -- so announcing only the
 * focused row leaves most of the screen unspoken, and a one-entry panel silent
 * (it produces no cursor movement at all). Pass pair_x < 0 for no partner.
 *
 * ⚠ Rows are paired by SHARING A Y, never by ordinal: the two columns can have
 * different lengths, and taking the Nth of each would attach a move's input to
 * the wrong move -- a confident lie about how to perform it. */
BOOL gt_column_item(const void *screen, float x, float pair_x, int position,
                    char *out, int outlen);

/* ⭐⭐⭐ THE GAME'S OWN ROW TEXT, by part ID -- no coordinates, no order.
 *
 * Every shell screen writes a row's text with 0x14007d290(screen, part,
 * child, message), which finds the part by walking the screen's GUI tree and
 * then the child text object by its index. This repeats that walk, read-only:
 *   part node: from *(screen+0x108), id at +0x08, first child +0x68, next +0x70
 *              (0x140307640)
 *   child obj: from *(node+0x100), index at +0x50, first child +0x70,
 *              next +0x78 (0x1402fc5c0)
 * Measured: a row's label object has index 7 and its value index 8 (the
 * 15:17 Keyboard Settings dump: every label +0x050 = 7, every value = 8).
 *
 * The text is the last string the game set on that exact object (the setText
 * hook's record). FALSE when the part or child does not exist, the object is
 * not a text object, or no text was ever set on it. Every read is guarded and
 * the walk is capped, so a freed tree yields FALSE, never a crash. */
BOOL gt_part_text(const void *screen, int part_id, int child,
                  char *out, int outlen);

/* Log every child (index and text) of one part, once per (screen, part).
 * Measurement for a screen whose child layout is not yet established. */
void gt_part_dump_once(const void *screen, int part_id, const char *why);

/* ⭐⭐ The layout animation a part is PLAYING -- how the game shows or hides a
 * row's children. 0x140309440 (play animation) compares the request with
 * **(int **)(*(node+0x100) + 0xb8), the current one, so that is read here.
 * FALSE when the part or the record cannot be read (unknown, not "none"). */
BOOL gt_part_anim(const void *screen, int part_id, int *anim);

/* ⭐⭐ A NESTED part child's text: part `part_id`, its child `child`, then
 * that object's own child `sub` (searched under +0xf0) -- the path the
 * generic heading setter 0x14007df40 takes. FALSE when any step is missing. */
BOOL gt_part_sub_text(const void *screen, int part_id, int child, int sub,
                      char *out, int outlen);

/* Sample once, on the WORKER thread, after a movement. ⚠ The cursor state is
 * PASSED IN for the same reason gt_movelist_probe() takes it: fetching it here
 * would pull menu_cursor, menu_speak and the GMD reader into a focused test
 * binary. `row_x`/`val_x` are screen properties, never constants of this
 * module. */

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

/* Test seams for the instance set: how many objects, and what does one say?
 * ⚠ Asserting the LOG TEXT instead would test the formatter, not the table. */
int gt_test_instance_count(void);
int gt_test_instance_text(void *obj, char *out, int outlen);

#endif /* CFC2_GUI_TEXT_H */
