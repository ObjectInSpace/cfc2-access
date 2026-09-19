/*
 * CVS2 hit-handler probe: find where health lives.
 *
 * WHY A HOOK AND NOT A SNAPSHOT (2026-09-19, all measured):
 * Snapshot diffing of the emulator arena is RULED OUT, not merely unpromising.
 * The whole 256 MiB arena was captured twice across a damaging exchange:
 * 16,233 of 233,832,448 bytes differ, ALL of them inside 3 MiB at
 * arena+0x8000000 (the C/scratch block). `R` itself was enumerated
 * instruction-by-instruction: every `sub` into a player struct offset
 * (26 of them) and every add/sub on an R-global (5). None is health.
 *
 * What remains is a pointer that only exists while a hit resolves:
 * at 0x1406C007C the hit handler does `mov rsi,[rdx+0x3F0]`, and that slot
 * reads NULL in all six snapshots taken between hits. A snapshot cannot
 * observe it by construction, however many are taken.
 *
 * THE SIGNATURE, read off the instructions rather than assumed.
 * 0x1406C0060 reads only rcx and rdx before writing anything:
 *   0x1406C0072  cmp byte ptr [rdx+0x5EC], 0   <- rdx first use
 *   0x1406C0083  mov rdi, rcx                  <- rcx first use
 * r8 and r9 are WRITTEN first (0x1406C0130 `movzx r8d,al`,
 * 0x1406C014D `mov r9,rsi`), so they are not inbound. The [rsp+0x10]/[rsp+0x18]
 * stores at entry are home-space register saves, not argument reads.
 * => exactly two arguments.
 *
 * ⛔⛔ THE SIGNATURE TRAP THIS PROJECT ALREADY PAID FOR: menu_cursor declared
 * FOUR arguments where the helper took SIX, so the original read its 5th and
 * 6th from stack slots our frame never filled. Ten tests passed because each
 * asked what the hook REPORTED, none what it PASSED ON. Hence the reading
 * above is from the disassembly, and the detour passes every argument through
 * untouched.
 *
 * rdx is confirmed to be the player struct B: B+0x5EC reads 0 for P1 and 1 for
 * P2 in every capture, which is how the selection reader already identifies
 * players.
 *
 * This probe SPEAKS NOTHING and writes nothing to game memory beyond the
 * detour itself. It records, the worker logs. Off unless `cfc2_hit.on` exists
 * next to the exe; `cfc2_hook.off` disables it as it does every other hook.
 */
#ifndef CFC2_HIT_H
#define CFC2_HIT_H

#include <windows.h>

/* Install the detour. FALSE if switched off, if the game is not the audited
 * build, or if MinHook refused. */
BOOL cvs2_hit_install(void);
void cvs2_hit_remove(void);

/* Called from the worker. Logs newly captured hit records, if any. */
void cvs2_hit_poll_and_log(void);

#endif /* CFC2_HIT_H */
