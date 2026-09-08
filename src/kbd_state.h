/*
 * The shell's decoded keyboard state.
 *
 * WHAT WAS FOUND OFFLINE (see memory cfc2-skeyboard-found)
 * -------------------------------------------------------
 * Disassembling around the single in-exe return address the caller trace
 * produced resolved the whole input model without another game launch:
 *
 *   - The class is sKeyboard -- its name is inline ASCII at 0x14104e6f8,
 *     sitting inside its own vtable/DTI block. That is a positive ID, not a
 *     shape match.
 *   - vtable 0x14104e718; the update method is slot +0x30 -> 0x140253220.
 *     Exactly ONE qword in the image holds that address.
 *   - 0x140253220 does `lea rdi,[rcx+0x138]`, so the state block is this+0x138.
 *   - 0x140253750 is the state BUILDER, called once from 0x1402532f3 with
 *     rcx = the manager and rdx = the state block.
 *
 * THE LAYOUT, read off 0x140253302 onward -- 8 dwords (256 keys) per bank:
 *
 *     +0x00 current   +0x20 previous   +0x40 Trg   +0x60 Rel   +0x80 Chg
 *
 * with Chg = cur^old, Trg = Chg&cur, Rel = Chg&old. That is the same model the
 * sPad metadata named -- but on the object that ACTUALLY updates. sPad has
 * Kind = 0 and its update never runs.
 *
 * ⚠⚠ THE CORRECTION THIS FILE ENCODES
 * The keyboard does NOT come through GetAsyncKeyState. 0x140253750 reads it
 * with a virtual call [rax+0x48] on [rbp+0xa40] -- a DirectInput GetDeviceState
 * filling a 0x100-byte buffer -- then translates each pressed DIK through a
 * table at manager+0x38 into an internal code. Only a 5-iteration MOUSE loop
 * (VK table 0x14104e790 = LBUTTON/RBUTTON/MBUTTON/XBUTTON1/XBUTTON2) uses
 * GetAsyncKeyState, which is exactly the vks=5, last_vk=0x06 the trace saw.
 * The disassembly and the live measurement agree independently.
 *
 * WHY A HOOK RATHER THAN A SEARCH
 * -------------------------------
 * The instance pointer is NOT statically reachable: 0x140253220 has zero direct
 * callers (it is vtable-dispatched) and nothing in the image points at the
 * sKeyboard name string. That is the same fact that closed routes 3 and 5 --
 * this engine walks its metadata rather than addressing it.
 *
 * But we no longer need to search for it. The builder receives both pointers as
 * ARGUMENTS, so hooking one known code address hands us the instance directly.
 * ⭐ This is the method that has worked in this project every time: read the
 * answer out of the instructions, never scan memory for a recognisable shape
 * (three false positives, all indistinguishable as data).
 *
 * SAFETY
 * ------
 * The hook records two pointers and returns; it calls the original
 * unconditionally and changes nothing. All READING of the state happens later,
 * on our worker thread, through mem_copy's fault guard -- a captured pointer is
 * still an untrusted pointer.
 * Off switch: cfc2_hook.off next to the exe, shared with the caller trace.
 */
#ifndef CFC2_KBD_STATE_H
#define CFC2_KBD_STATE_H

#include <windows.h>

/* Live addresses. ASLR is off and the exe carries no relocations, so these are
 * valid in every future run. */
#define KBD_BUILDER_ADDR   0x140253750ULL  /* the state builder we hook */
#define KBD_UPDATE_ADDR    0x140253220ULL  /* sKeyboard's vtable slot +0x30 */
#define KBD_VTABLE_ADDR    0x14104e718ULL
#define KBD_NAME_ADDR      0x14104e6f8ULL  /* inline "sKeyboard" -- the ID */
#define KBD_STATE_OFFSET   0x138           /* state block = manager + this */

/* Bank offsets within the state block. 8 dwords each = 256 keys. */
#define KBD_BANK_CUR   0x00
#define KBD_BANK_OLD   0x20
#define KBD_BANK_TRG   0x40
#define KBD_BANK_REL   0x60
#define KBD_BANK_CHG   0x80
#define KBD_BANK_WORDS 8

typedef struct {
    BOOL      installed;
    LONG      calls;         /* how many times the builder ran */
    void     *manager;       /* rcx: the sKeyboard instance */
    void     *state;         /* rdx: the bitmap, == manager + 0x138 */
    BOOL      offset_ok;     /* does state - manager == 0x138 as predicted? */
} kbd_capture;

/* Why an install attempt ended the way it did.
 *
 * ⭐⭐ THIS ENUM EXISTS BECAUSE A TEST ASSERTING ONLY "install returned FALSE"
 * PASSED WITH ALL THREE GUARDS DELETED. MinHook itself refuses an unmapped
 * address, so the refusal arrived by a different route than the one the test
 * claimed to check -- the outcome was right and the assertion was worthless.
 * Reporting the REASON makes the guards observable, so a test can require that
 * the identity check specifically is what rejected the address. */
typedef enum {
    KBD_INSTALL_NOT_TRIED = 0,
    KBD_INSTALL_OK,
    KBD_INSTALL_BAD_PROTECT,   /* target not committed executable memory */
    KBD_INSTALL_BAD_VTABLE,    /* vtable+0x30 is not the update function */
    KBD_INSTALL_BAD_NAME,      /* the class name is not "sKeyboard" */
    KBD_INSTALL_MINHOOK_FAILED /* guards passed; MinHook itself refused */
} kbd_install_status;

/* Install the detour on the state builder. Returns FALSE if any guard rejected
 * the address or MinHook failed; kbd_install_why() then says which. */
BOOL kbd_install(void);
kbd_install_status kbd_install_why(void);
void kbd_remove(void);

/* What the hook has captured so far. */
void kbd_snapshot(kbd_capture *out);

/* Read the five banks through the fault guard. Returns FALSE if the state
 * pointer has not been captured yet or the read faulted -- ⭐ a return value,
 * so "could not measure" can never be mistaken for "all keys are up".
 * `words` receives 5 banks x 8 dwords in the order cur, old, trg, rel, chg. */
BOOL kbd_read(DWORD words[5][KBD_BANK_WORDS]);

/* --- Edges -----------------------------------------------------------------
 * ⛔⛔ DO NOT USE THE Trg/Rel BANKS FROM A SLOW POLLER. Measured in-game:
 * `Trg` is set for exactly ONE frame (~16 ms), and polling at 100 ms caught it
 * roughly 1 press in 6 -- most arrow presses logged `pressed=(none)` while the
 * `cur` bank plainly showed the key going down. A Trg-based reader silently
 * misses most keypresses.
 *
 * ⭐ So derive edges from `cur` instead: we read `cur` on every poll, and
 * comparing successive reads gives correct press/release at ANY poll rate.
 * (The engine's own Trg is correct -- it is computed per frame. It is only
 * unusable by US, from a 100 ms thread. Read at frame rate inside the hook it
 * would be fine.)
 *
 * ⭐ Same class of error as a sampling gate making negatives meaningless:
 * never read a single-frame field from a slow sampler.
 *
 * kbd_poll_edges() reads the state and reports what CHANGED since the previous
 * call. Returns FALSE if the state could not be read -- a return value, so
 * "could not measure" can never be mistaken for "nothing was pressed". */
typedef struct {
    DWORD down[KBD_BANK_WORDS];      /* currently held */
    DWORD pressed[KBD_BANK_WORDS];   /* went down since the previous call */
    DWORD released[KBD_BANK_WORDS];  /* came up since the previous call */
    BOOL  first;                     /* TRUE on the first call: `pressed` is
                                      * seeded, not observed, so callers must
                                      * not treat it as real presses */
} kbd_edges;

BOOL kbd_poll_edges(kbd_edges *out);

/* Test-only: feed a synthetic `cur` bank instead of reading the game, so the
 * edge derivation can be driven through a known sequence offline. NULL (the
 * default) means "read the real state". This exists so the test drives the
 * SHIPPING function rather than a copy of its logic. */
extern const DWORD *g_kbd_test_cur;

/* Test-only: forget the previous sample, so a test can start from a known
 * state without the residue of an earlier case. */
void kbd_reset_edges(void);

/* Is a given internal code currently down / newly pressed in `e`? */
BOOL kbd_is_set(const DWORD bank[KBD_BANK_WORDS], int code);

/* The shell's internal key codes, measured in-game 2026-09-08. The DIK
 * translation maps back onto the standard VK set. */
#define KBDC_UP     0x26
#define KBDC_DOWN   0x28
#define KBDC_LEFT   0x25
#define KBDC_RIGHT  0x27
#define KBDC_ENTER  0x0d

/* Log the state, naming any keys that are down or newly pressed. Called on a
 * change only, so the log shows navigation rather than a per-frame flood. */
void kbd_poll_and_log(void);

#endif /* CFC2_KBD_STATE_H */
