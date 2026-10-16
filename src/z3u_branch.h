#ifndef CFC2_Z3U_BRANCH_H
#define CFC2_Z3U_BRANCH_H
#include <windows.h>

/* Street Fighter Alpha 3 runs on one of two separately compiled copies of the
 * game, chosen by the game's own Additional Characters setting (session
 * +0x2B4; docs/port-tasks/z3u-alternate.md):
 *   - ordinary: ROMs 6 z3u / 7 a3u, setting off;
 *   - alternate: ROMs 16 z3uu / 17 a3uu, setting on, with Eagle, Maki, Yun
 *     and Ingrid on the select grid.
 * The two copies share their logic but not their addresses or layout: the
 * player object gains 8 bytes from +0x51 on, and the root block grows from
 * 0x12F98 to 0x1B528. Each value below for the alternate copy was read off
 * the instruction aligned with the ordinary one (tools/branch_map.py). */
typedef struct {
    const char *name;
    unsigned rom_jp, rom_us;     /* ROM index for the Japanese / US version */
    ULONGLONG root_slot;         /* the published primary block             */
    unsigned state_size;
    ULONGLONG frame_fn;          /* per-frame update (hooked)               */
    ULONGLONG walker_fn;         /* attract text walker (hooked)            */
    /* player objects: R + obj_off + side*obj_step */
    unsigned obj_off, obj_step;
    unsigned o_hp, o_gauge, o_motion, o_side, o_id, o_random, o_ism;
    unsigned o_mode;             /* EX Mode Select cursor, object state 1   */
    /* root fields */
    unsigned clock, camera, queue, messages;
    unsigned id_limit;           /* grid IDs are 0 .. id_limit-1            */
} z3u_branch;

/* Fields at or below +0x50 of the player object, and root words +0x28 to
 * +0x3C, +0x59 and +0x69, are the same on both copies. */
static const z3u_branch z3u_ordinary = {
    .name = "ordinary", .rom_jp = 6, .rom_us = 7,
    .root_slot = 0x1422FB960ull, .state_size = 0x12F98u,
    .frame_fn = 0x14086D160ull, .walker_fn = 0x1408797D0ull,
    .obj_off = 0x1B0u, .obj_step = 0x550u,
    .o_hp = 0x88u, .o_gauge = 0x1EEu, .o_motion = 0x52u, .o_side = 0x1D1u,
    .o_id = 0x1D2u, .o_random = 0x59u, .o_ism = 0x202u, .o_mode = 0x237u,
    .clock = 0xF1u, .camera = 0xC55Au, .queue = 0x9D70u, .messages = 0x9C68u,
    .id_limit = 38u,
};

/* Alternate addresses, each with the instruction that shows it:
 *   update 0x140DCC3F0 (wrapper 0x140DCB970), root slot 0x142C44050 (both
 *     loaded by the update), walker 0x140DD92B0;
 *   objects R+0x1F8 / R+0x778 (0x140E0AB97, 0x140E0ABA3), stride 0x580;
 *   health +0x90 (round setup 0x140DCC132, damage 0x140E11FEC);
 *   gauge +0x1F6 (spend 0x140DEA599); side +0x1D9, ID +0x1DA, ism +0x20A
 *     (screen X 0x140DD2960); random +0x61 (0x140E0B13F);
 *   motion byte +0x52 -> +0x5A (every +0x51.. displacement moves by 8);
 *   camera R+0xE4EA (0x140DD2973); queue R+0xBC00 (print 0x140DD925A);
 *   messages R+0xBAF8 (0x140DD8170); clock R+0x129 (0x140E03ECF);
 *   grid 0x1419E83F0 holds IDs 0..41;
 *   mode cursor +0x242 (0x140E0B807; ordinary +0x237 at 0x1408AA926).
 *     ⚠ Not +8: fields past about +0x220 do not move uniformly (the mode
 *     flags +0x22C and +0x230 become +0x238 and +0x23B). */
static const z3u_branch z3u_alternate = {
    .name = "alternate", .rom_jp = 16, .rom_us = 17,
    .root_slot = 0x142C44050ull, .state_size = 0x1B528u,
    .frame_fn = 0x140DCC3F0ull, .walker_fn = 0x140DD92B0ull,
    .obj_off = 0x1F8u, .obj_step = 0x580u,
    .o_hp = 0x90u, .o_gauge = 0x1F6u, .o_motion = 0x5Au, .o_side = 0x1D9u,
    .o_id = 0x1DAu, .o_random = 0x61u, .o_ism = 0x20Au, .o_mode = 0x242u,
    .clock = 0x129u, .camera = 0xE4EAu, .queue = 0xBC00u, .messages = 0xBAF8u,
    .id_limit = 42u,
};

/* The copy that runs a ROM index, or NULL for any other game. */
static inline const z3u_branch *z3u_branch_for_rom(unsigned rom)
{
    if (rom == z3u_ordinary.rom_jp || rom == z3u_ordinary.rom_us) return &z3u_ordinary;
    if (rom == z3u_alternate.rom_jp || rom == z3u_alternate.rom_us) return &z3u_alternate;
    return NULL;
}

/* The shell's name table index for a native Alpha 3 ID: the move list adds
 * one from 0x26 (0x14008221C), so 0x26..0x29 name Eagle, Maki, Yun and
 * Ingrid. Ordinary-copy IDs never reach 0x26. */
static inline unsigned z3u_name_index(unsigned id) { return id >= 0x26u ? id + 1u : id; }
#endif
