/*
 * emu_ram -- reach the emulated arcade machine's RAM (CFC2 "world 2").
 *
 * WHY THIS EXISTS
 * ---------------
 * The shell is MTFramework and has real text; the 8 games are raw CPS-2/NAOMI
 * ROM sets under Capcom's own emulator, and they draw every character name,
 * gauge and round announcement as SPRITES. There is no string to hook. The only
 * route to in-game state is to read the emulated machine's RAM out of this
 * process.
 *
 * HOW THE ANCHOR WAS FOUND -- and why it is not a memory scan
 * -----------------------------------------------------------
 * This project's record is unambiguous: reading the answer out of the
 * INSTRUCTIONS is 4 for 4 (sPad, sKeyboard, the GMD table, the menu-cursor
 * chokepoint); searching memory for something SHAPED like the answer is 0 for 4
 * and burned many of the user's play sessions debugging our own scanners.
 *
 * So all of the following came from the dump + riprefs, with zero game launches:
 *
 *     0x140048270   mov [rip -> 0x141c14f98], rcx     ; the ONLY write
 *     0x140048283   edx = 0x10000000                  ; 256 MB
 *     0x140048288   r8d = 0x1000                      ; page aligned
 *     0x140048291   call [rax+0x40]                   ; alloc(size, align)
 *     0x1400482a6   [rcx] = rax                       ; arena at state+0x00
 *
 * Exactly ONE instruction in the image writes the singleton slot, while 3070
 * read it. That asymmetry is what makes it trustworthy.
 *
 * ⚠⚠ THE ARENA IS NOT GUEST RAM, AND THIS HEADER MUST NOT BLUR THAT.
 * The constructor asks for 256 MB. NAOMI system RAM is 16 MB; CPS-2 is far
 * smaller. Guest RAM is expected to be a SUB-RANGE of the arena, and which
 * sub-range is exactly what the next stage has to PROVE rather than assume.
 * "I found the structure" is not progress toward live state.
 *
 * ⚠ MEASURED, not assumed: the first live run reported the arena at
 * 0x7d81000 with a VirtualQuery region of 511 MB -- i.e. the region the memory
 * manager reports is NOT the 256 MB the constructor requested. Do not treat
 * EMU_ARENA_SIZE as the bound of anything; it records what the code asks for,
 * and the region size is what actually exists. A scan must use the measured
 * size, and must not assume the arena begins at the region's base either.
 *
 * ⚠ ASLR is off and the exe carries no relocations, so the two literal
 * addresses below are valid on every launch -- but they are still verified at
 * runtime before use, because a wrong pointer here reads someone else's memory.
 */
#ifndef CFC2_EMU_RAM_H
#define CFC2_EMU_RAM_H

#include <windows.h>

/* --- The anchors (image-relative, ASLR off -> literal is fine) ------------ */

/* The emulator session singleton. Written by exactly one instruction. */
#define EMU_STATE_SLOT   0x141c14f98ULL

/* Field offsets within the emulator state object, read off the instructions
 * that set them (0x140049dcd.. and 0x1400524c0..). */
#define EMU_OFF_ARENA        0x00   /* the 256 MB page-aligned arena          */
#define EMU_OFF_ROM_NAME     0x30   /* const char* e.g. "cvs2"                */
#define EMU_OFF_DESC_A       0x40   /* descriptor byte (+0x28)                */
#define EMU_OFF_ROM_INDEX    0x44   /* 1..18, from rom_index_from_name()      */
#define EMU_OFF_DESC_B       0x48   /* descriptor byte (+0x29)                */
#define EMU_OFF_DERIVED_ID   0x4c   /* from 0x1400b6190(rom_index)            */
#define EMU_OFF_DESC_C       0x50   /* descriptor byte (+0x2a)                */

#define EMU_ARENA_SIZE   0x10000000ULL   /* 256 MB, as the constructor asks   */

/* --- Outcomes ------------------------------------------------------------
 * ⭐⭐ "Could not measure" is a RETURN VALUE here, never an all-false struct
 * and never merely a logged warning. This project has already shipped a tool
 * that printed "coverage is INCOMPLETE" while every assertion stayed green; a
 * caller must not be able to confuse "looked and found nothing" with "never
 * looked". Every function below reports which of the two happened. */
typedef enum {
    EMU_OK = 0,           /* proven, and the out-params are meaningful        */
    EMU_NOT_RUNNING,      /* looked: no game is loaded (a real, clean answer) */
    EMU_UNREADABLE,       /* could NOT look: memory refused. NOT a negative.  */
    EMU_UNVERIFIED        /* found a candidate that FAILED its content check  */
} emu_status;

const char *emu_status_name(emu_status s);

/* --- The emulator state object ------------------------------------------ */

/* Resolve the singleton. Fills *out with the state object's address.
 * EMU_NOT_RUNNING when the slot is null (the shell is up, no game started). */
emu_status emu_state(ULONGLONG *out);

/* The arena: *(void**)state. Page-aligned, nominally EMU_ARENA_SIZE.
 * ⚠ Being the right size is NOT evidence of anything; this only locates. */
emu_status emu_arena(ULONGLONG *base_out, SIZE_T *size_out);

/* --- Which ROM is running ------------------------------------------------ */

/* The ROM index (0..17) over REVISIONS, straight from state+0x44.
 *
 * ⛔⛔ IT IS 0-BASED, and index 0 (cvs1) is a VALID game, not "nothing
 * matched". The first version of this file read it as 1-based and treated 0 as
 * NOT_RUNNING; the live cross-check caught it on the first run (the game said
 * index 3 while its own name string said 'cvs2u'). See the long comment in
 * emu_rom_index() for why the loop at 0x1400b6250 looks 1-based and is not.
 *
 * ⚠ This is NOT the 0..7 canonical game index in move_list.h. Two games ship
 * two revisions each (Z3U ships four); converting between the two numberings is
 * a real mapping, not a cast -- see emu_game_from_rom_index(). */
emu_status emu_rom_index(int *out);

/* The ROM set's short name ("cvs2", "z3uu", ...), copied out of the state
 * object's own string pointer. Bounded copy; never returns unterminated. */
emu_status emu_rom_name(char *out, unsigned cap);

/* Map the 0-based ROM index (0..17) to the canonical 0..7 game index used by
 * move_list.h / roster_table.h, and to a revision ordinal within that game.
 * Returns FALSE for an out-of-range index rather than clamping -- a silently
 * clamped id would name the WRONG GAME's characters, which is the worst
 * failure class in this project. */
BOOL emu_game_from_rom_index(int rom_index, int *game_out, int *rev_out);

/* The expected short name for a 0-based ROM index (0..17), or NULL if out of
 * range.
 * Lets a caller CROSS-CHECK the live string at state+0x30 against the table the
 * game's own rom_index_from_name() must have used to produce state+0x44. Two
 * independent sources agreeing is the standard used throughout this project;
 * one source "looking right" is not. */
const char *emu_rom_name_for_index(int rom_index);

/* --- Finding guest RAM inside the arena ----------------------------------
 *
 * ⚠⚠ THE ARENA'S BOUNDS ARE NOT KNOWN FROM VirtualQuery. The live run measured
 * a 511 MB region for a 256 MB request, which means the allocator handed back a
 * pointer INSIDE a larger committed run and VirtualQuery reports the enclosing
 * run, not our allocation. So a scan must be bounded by what it can actually
 * read, and must never present "I stopped at my own limit" as "it is not here".
 *
 * ⭐⭐ THE SIGNATURE IS AN ALGEBRAIC INVARIANT, NOT A VALUE MATCH.
 * Published CVS2 (NAOMI) prior art gives a player struct of stride 0x5CC with
 * health and max_health adjacent. Those are MAME/Flycast guest addresses and
 * are useless here as literals -- CFC2 rebases guest RAM into its own arena.
 * What survives rebasing is the RELATIONSHIP between the fields:
 *
 *     P2 == P1 + 0x5CC                  (the stride)
 *     health <= max_health              (for BOTH players)
 *     max_health equal and nonzero      (both players share the cap)
 *
 * Several independent fields agreeing at one base is a positive
 * identification. ONE field "looking right" is not -- this project has already
 * been fooled by a pointer pair that passed 13 trials with clean controls and
 * was a monotonic allocation counter.
 *
 * ⚠ AND A MATCH IS STILL ONLY A CANDIDATE. Variance identifies candidates;
 * REVERSIBILITY identifies state. Confirming this needs a hit taken in
 * training mode and then reset: real health returns to its cap, a look-alike
 * does not. emu_ram_find() reports candidates; it does not claim to have found
 * guest RAM, and its documentation must not either. */

typedef struct {
    ULONGLONG p1;          /* address of the P1 player struct               */
    ULONGLONG guest_base;  /* p1 minus the prior-art offset -- INFERRED     */
    unsigned  health;      /* what P1's health field read at match time     */
    unsigned  max_health;
} emu_ram_hit;

/* ⚠ `*n_out` counts every pair ENCOUNTERED, which may exceed `cap`. The
 * caller keeps at most `cap` of them but the sweep always runs to completion:
 * a cap that ended the search would make every "found nothing further" mean
 * "I stopped early", which is indistinguishable from a real negative. The
 * first live run filled 16 slots inside 0.5 MB and stopped, leaving 99.8% of
 * the arena unexamined -- and reported that as the result. */

/* Scan the arena for the CVS2 player-pair signature.
 *
 * Fills up to `cap` hits and sets *n_out. The return value distinguishes:
 *   EMU_OK          - the scan COMPLETED and *n_out is a real count (0 is a
 *                     real answer: the signature is not present)
 *   EMU_UNREADABLE  - the scan could not run or was cut short; *n_out says
 *                     nothing about the game. NOT a negative.
 *   EMU_NOT_RUNNING - no game is loaded
 *
 * ⚠ `scanned_out` receives the number of bytes actually examined, so a caller
 * can tell a complete sweep from a truncated one. "0 failed" must never be
 * reportable as if it were "0 checked". */
emu_status emu_ram_find(emu_ram_hit *out, unsigned cap, unsigned *n_out,
                        SIZE_T *scanned_out);

/* --- Watching a candidate over time (this is what decides it) ------------
 *
 * ⭐⭐ A REAL MATCH IS A BETTER INSTRUMENT THAN A STATIC SWEEP, and it does not
 * need training mode. Finding a pair that fits the arithmetic proves nothing;
 * what separates the fighters from filler is how the values BEHAVE:
 *
 *   - health DIVERGES between the two players (filler and lookup tables do
 *     not have P1 at 87 while P2 is at 132);
 *   - health FALLS during a round, in irregular steps, as hits land;
 *   - health RETURNS TO THE CAP at a round transition -- which is
 *     reversibility, supplied by the game on its own schedule rather than by
 *     asking the player to do anything special.
 *
 * A constant scores zero on all three, however well it satisfies the
 * arithmetic. This is [[anchor-on-what-changes]] step 6 applied without
 * requiring a particular game mode.
 *
 * emu_watch_sample() is called on each sweep; it re-reads every tracked
 * candidate and accumulates the evidence. Candidates are keyed by address, so
 * a pair that stops matching still keeps its history. */
typedef struct {
    ULONGLONG p1;
    unsigned  samples;      /* how many times this address was read          */
    unsigned  changes;      /* how often P1's health CHANGED                 */
    unsigned  drops;        /* ... changed downward (a hit landing)          */
    unsigned  resets;       /* ... jumped back UP to the cap (a new round)   */
    unsigned  diverged;     /* ... samples where P1 and P2 health DIFFERED   */
    unsigned  max_health;   /* the cap as first seen                         */
    unsigned  last_h1, last_h2;
} emu_watch;

/* Sample all tracked candidates, adopting any new ones from `hits`.
 * Returns the number currently tracked. */
unsigned emu_watch_sample(const emu_ram_hit *hits, unsigned n);

/* Read back the tracked candidates, best evidence first. A candidate with
 * drops AND resets AND divergence is the fighters; one with samples but no
 * changes is filler that happened to fit. */
unsigned emu_watch_report(emu_watch *out, unsigned cap);

void emu_watch_reset(void);

/* --- Offline test seam ---------------------------------------------------
 * Point the state slot at a planted fixture. The shipping code never calls
 * this; passing 0 restores the real constant. */
void emu_ram_test_redirect(ULONGLONG state_slot);

/* Override the scan window, so the offline suite can plant a fixture arena
 * instead of needing a running game. Passing base 0 restores normal behaviour
 * (derive the window from the live state object). */
void emu_ram_test_window(ULONGLONG base, SIZE_T size);

/* Restart the budgeted sweep from the beginning of the window.
 *
 * ⚠ The sweep is TIME-BUDGETED and resumes where it left off, because a single
 * full-arena pass stalled the mod for 114 seconds. One call to emu_ram_find()
 * therefore covers only part of the arena; full coverage accrues across
 * several calls, and `scanned_out` says how much this one examined. A caller
 * must not read "found nothing" from one budgeted sweep as "it is not there". */
void emu_ram_scan_rewind(void);

#endif /* CFC2_EMU_RAM_H */
