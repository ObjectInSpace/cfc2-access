/*
 * emu_cursor -- find the EMULATED game's menu cursor in the arena.
 *
 * THE QUESTION THIS ANSWERS
 * -------------------------
 * "Can we tell when the in-game cursor moves, and read where it moved to?"
 *
 * The shell's menus are solved and speak. The emulated arcade games draw their
 * own menus -- character select, groove, ratio, stage -- entirely as sprites,
 * with no text layer to hook (see memory cfc2-two-worlds). The only route to
 * what is selected is an integer in the emulated machine's RAM.
 *
 * ⚠⚠ WHY A SEARCH IS JUSTIFIED HERE WHEN IT WAS NOT BEFORE
 * This project is 0-for-4 on "scan memory for something shaped like the answer"
 * and 4-for-4 on "read the answer out of the instructions". Six sessions of
 * scanning the SHELL ended in a firm negative: MTFramework keeps no flat
 * 0..N-1 index at all -- selection is a per-node SELECT flag, so a scan for an
 * index could never have succeeded (memory cfc2-heapdiff).
 *
 * That negative does NOT transfer. The emulated game is 1990s SH-4 arcade code,
 * where a flat cursor variable is the conventional implementation. Different
 * program, different question.
 *
 * ⛔ But the DISCIPLINE transfers completely, and every guard below was paid
 * for by a specific failure in this project. None of them are speculative.
 *
 * THE METHOD -- a control, then reversibility
 * -------------------------------------------
 *   S0 --(wait, NO input)--> S1 --(RIGHT)--> S2 --(LEFT)--> S3
 *
 *     (a) S0 == S1        quiet in the control   -> kills per-frame churn
 *     (b) S1 != S2        reacted to the press
 *     (c) S3 == S1        RETURNED on the reverse -> kills counters
 *     (d) |S2-S1| == 1    stepped by exactly one  -> kills pointers/floats
 *     (e) value < 64      bounded by the grid     -> kills large ints
 *
 * ⭐⭐ (c) IS THE LOAD-BEARING TEST. A previous candidate in this project
 * survived 13 page-level trials with clean controls, stepped by a constant
 * stride, and persisted -- and was an allocation bump pointer. The up-move test
 * killed it in one keypress. Variance identifies candidates; REVERSIBILITY
 * identifies state.
 *
 * ⚠ One cycle is not enough. Estimated ~400 survivors over 256 MB from a single
 * cycle, so the probe runs continuous cycles and ranks by CONSECUTIVE CYCLES
 * SURVIVED. A single-cycle hit is never reported as a finding.
 *
 * TWO PHASES, BECAUSE MEMORY IS NOT FREE
 * --------------------------------------
 * Measured on the target machine: 2.0 GB free of 15.8 GB. Holding two 256 MB
 * byte-for-byte snapshots would take a quarter of that and risk paging, which
 * would turn a 100 ms snapshot into something far worse.
 *
 *   Phase 1  hash wide    -- 32 bits per 4 KB page: 65536 pages = 0.25 MB
 *   Phase 2  keep narrow  -- full bytes ONLY for pages that reacted
 *
 * Worst case ~28 MB instead of 512 MB, with identical coverage. This is the
 * same two-phase shape heapdiff converged on after its own failures.
 *
 * ⚠⚠ THE CONTROL MUST BE APPLIED AT EVERY GRANULARITY YOU REPORT AT.
 * A page-level control does NOT filter the fields inside a surviving page: a
 * page qualifies because ONE field tracks the cursor, and the forty per-frame
 * fields sharing it are then reported as if equally interesting. Phase 2
 * re-applies the same S0/S1/S2/S3 test per FIELD.
 *
 * ⛔⛔ THE SNAPSHOT CANNOT BE BUDGETED AND RESUMED
 * emu_ram.c sweeps with SCAN_BUDGET_MS and resumes via g_resume. That pattern
 * MUST NOT be copied here. A snapshot resumed across ticks blends two moments --
 * the game runs 20 ms in between and rewrites memory -- so the diff would
 * report changes that never coexisted. The hash pass completes atomically.
 *
 * Cost: ~104 ms, derived from heapdiff's own measured throughput in this
 * process (1577 MB in 641 ms). That is ~6 dropped frames, on a menu, ~20 times
 * in an opt-in run. It is MEASURED AND LOGGED on every pass, never assumed --
 * a benchmark drifts from the code it measures.
 *
 * WIDTHS -- u8, u16 AND u32, in parallel
 * --------------------------------------
 * ⚠ A real blind spot found the hard way: a u8 index of value 3 whose
 * neighbouring bytes are nonzero reads as a u32 far above any sane bound, so it
 * is disqualified on the FIRST sample and never looked at again. The scan could
 * only ever have found a 4-byte index whose neighbours happened to be zero. A
 * NAOMI cursor is most likely u8 or u16, so all three widths are tracked.
 */
#ifndef CFC2_EMU_CURSOR_H
#define CFC2_EMU_CURSOR_H

#include <windows.h>

/* 4 KB, matching the page granularity the region filter works in. */
#define EC_PAGE          4096u

/* Values a menu cursor may plausibly take. The CVS2 select grid is 5x10 = 50
 * cells; 64 leaves headroom for a larger list without admitting every small
 * integer in the process. */
#define EC_MAX_VALUE     64u

/* How many reacting pages we will keep CONTENT for. heapdiff measured
 * 273-2262 pages reacting to a cursor move, so this covers the observed range
 * with margin. ⚠ This caps what we KEEP, not what we EXAMINE -- see
 * ec_report.pages_seen, which counts every reacting page encountered. */
#define EC_MAX_PAGES     4096u

/* Field candidates carried between cycles. */
#define EC_MAX_FIELDS    8192u

typedef enum {
    EC_OK = 0,          /* the operation completed; results are meaningful   */
    EC_NOT_RUNNING,     /* no game loaded -- a real, clean negative          */
    EC_UNREADABLE,      /* could not read; says NOTHING about the game       */
    EC_NO_MEMORY,       /* could not allocate the pools                      */
    EC_NOT_ARMED        /* a phase was requested out of order                */
} ec_status;

const char *ec_status_name(ec_status s);

/* The four phases of one cycle. The probe advances through these in order and
 * refuses anything out of sequence, because an ambiguous press has already
 * cost several runs in this project. */
typedef enum {
    EC_PHASE_BASELINE = 0,  /* S0: capture                                   */
    EC_PHASE_CONTROL,       /* S1: capture after a quiet interval, NO input  */
    EC_PHASE_MOVED,         /* S2: capture after the forward press           */
    EC_PHASE_RETURNED       /* S3: capture after the reverse press           */
} ec_phase;

const char *ec_phase_name(ec_phase p);

/* The width a candidate was read at. Tracked explicitly so a u8 index cannot
 * be lost inside a u32 read. */
typedef enum { EC_U8 = 1, EC_U16 = 2, EC_U32 = 4 } ec_width;

typedef struct {
    ULONGLONG addr;         /* absolute address of the field                 */
    ec_width  width;
    unsigned  v_base;       /* value at S1 (the control -- the true baseline)*/
    unsigned  v_moved;      /* value at S2                                   */
    int       delta;        /* v_moved - v_base, signed                      */
    unsigned  cycles;       /* CONSECUTIVE cycles this field has survived    */
    unsigned  distinct;     /* how many distinct values it has taken         */
    unsigned  vmin, vmax;
    BOOL      is_toggle;    /* only ever 0<->1: a flag, not an index         */
} ec_field;

/* What a run has established. ⚠ Every count here is POSITIVE evidence of work
 * done: "0 candidates" and "0 examined" must never be confusable. */
typedef struct {
    unsigned  cycles_done;
    SIZE_T    bytes_hashed;     /* per pass                                  */
    DWORD     last_pass_ms;     /* MEASURED, not predicted                   */
    unsigned  pages_total;      /* pages in the window                       */
    unsigned  pages_noisy;      /* changed during the CONTROL -> excluded    */
    unsigned  pages_reacted;    /* changed on the move, quiet in the control */
    unsigned  pages_seen;       /* reacting pages ENCOUNTERED (may exceed
                                 * pages_kept -- the cap must never read as
                                 * "there were no more")                     */
    unsigned  pages_kept;
    unsigned  fields_examined;  /* every field tested, all widths            */
    unsigned  fields_returned;  /* passed reversibility                      */
    unsigned  fields_counter;   /* rejected: kept climbing                   */
    unsigned  fields_toggle;    /* rejected: 0<->1 flag                      */
    unsigned  candidates;       /* survived everything                       */
    BOOL      truncated;        /* a cap bound somewhere this run            */
} ec_report;

/* Is an emulated ROM actually RUNNING right now?
 *
 * ⛔⛔ THIS EXISTS BECAUSE `emu_rom_index() == EMU_OK` IS NOT THAT QUESTION.
 * Index 0 is a VALID game (cvs1), so a status check alone cannot tell "cvs1 is
 * loaded" from "the shell is sitting on the title screen". The first live run
 * of this probe armed 24 seconds before the ROM loaded for exactly that reason,
 * spent its whole timeout on the shell, and then reported a confident negative
 * about the emulated game's input path -- having had ~6 seconds to observe it.
 *
 * The discriminator is the ROM NAME: it is empty until a ROM is actually
 * running, and the emu: cross-check already relies on that. `*name_out` is
 * filled when TRUE is returned, so a caller can also notice a ROM CHANGE.
 *
 * ⚠ The project's own memory said "index 0 is a VALID game, not 'nothing
 * matched'" before this bug was written. Encoding the check here means the next
 * caller cannot make the same mistake. */
BOOL emu_cursor_rom_running(char *name_out, unsigned cap);

/* Allocate the pools and capture S0. Safe to call repeatedly; re-arming
 * discards any in-flight cycle. */
ec_status emu_cursor_arm(void);

/* Advance one phase. `p` must be the phase the probe is expecting; passing any
 * other returns EC_NOT_ARMED rather than silently reinterpreting the capture.
 *
 * ⚠ Returns EC_UNREADABLE if the capture could not be taken. That is NOT a
 * statement about the game -- "could not measure" is a return value here, not
 * an all-false result that reads like a finding. */
ec_status emu_cursor_step(ec_phase p);

/* Which phase the probe expects next, so the log can always say what the next
 * press will do. The user cannot see internal state. */
ec_phase emu_cursor_next_phase(void);

/* Read back candidates, best first (most cycles survived, then most distinct
 * values). Returns how many were written. */
unsigned emu_cursor_candidates(ec_field *out, unsigned cap);

/* The full accounting for this run. */
void emu_cursor_status(ec_report *out);

void emu_cursor_reset(void);

/* --- Offline test seam ---------------------------------------------------
 * Point the probe at a planted fixture instead of the live arena. Passing
 * (0, 0) restores live derivation.
 *
 * ⚠ Like emu_ram_test_window(), this also RESETS the probe, so residue from a
 * previous case cannot truncate the next one's sweep. */
void emu_cursor_test_window(ULONGLONG base, SIZE_T size);

/* Force the page-keep cap, so a test can drive the truncation path
 * deterministically rather than by allocating a huge fixture. 0 restores the
 * shipping value. */
void emu_cursor_test_page_cap(unsigned pages);

/* Exposed for direct testing: the block hash. A tail bug is exactly the kind
 * that hides until the one block that needs it. */
unsigned ec_hash_block(const BYTE *p, SIZE_T n);

#endif /* CFC2_EMU_CURSOR_H */
