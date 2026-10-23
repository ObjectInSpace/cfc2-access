#include "pws_select_decode.h"
#include "character_name_table.h"
#include <string.h>
#include <stdio.h>

/* 0x1409A0F83 indexes 0x14109C510 with the cursor; +4 of the fighter
 * receives the ID, then 0x1409A10CD publishes it to shell+0x578+4*slot. */
static const unsigned char pws1_ids[11] = { 8,0,6,2,4,3,5,7,1,9,10 };

bool pws_select_from_state(unsigned game, unsigned variant, const void *state,
                           size_t size, pws_select_sample *out)
{
    const unsigned char *r = state;
    if (!out) return false;
    memset(out, 0, sizeof *out);
    if (!r || variant > 1 || (game != 4 && game != 5) ||
        size != (game == 4 ? 0x428F0u : 0x8CD40u)) return false;
    out->game = game;
    out->variant = variant;
    memcpy(out->phase, r + (game == 4 ? 0x27E58 : 0x52BD8), sizeof out->phase);
    out->gates[0] = r[game == 4 ? 0x27F8B : 0x52D2B];
    out->gates[1] = r[game == 4 ? 0x27F8E : 0x52D2E];
    out->mask = r[game == 4 ? 0x27FF1 : 0x54B49];
    for (unsigned p = 0; p < (game == 4 ? 2u : 4u); p++) {
        if (game == 4) {
            unsigned actor = 0x18428 + p * 0x7890;
            out->state[p] = r[0x27DC2 + p * 0x78];
            out->cursor[p] = r[0x27DCC + p * 0x78];
            out->slot[p] = r[actor + 3];
            memcpy(&out->shuffle[p], r + actor + 0x7874, 4);
        } else {
            unsigned actor = 0x40818 + p * 0x42B8;
            out->state[p] = r[0x529D2 + p * 0x98];
            out->cursor[p] = r[actor + 2];
            out->slot[p] = r[actor + 1];
            memcpy(&out->shuffle[p], r + actor + 0x4258, 4);
        }
    }
    return true;
}

int pws_select_decode(const pws_select_sample *s, unsigned p)
{
    unsigned id;
    if (!s || s->variant > 1 || (s->game != 4 && s->game != 5) ||
        p >= (s->game == 4 ? 2u : 4u)) return -1;
    if (s->gates[0] || s->gates[1]) return -1;
    if (s->game == 4) {
        if (s->phase[0] != 1 || s->phase[5] != 2 || s->phase[6] != 2 ||
            s->state[p] != 2 || s->mask > 3 || !(s->mask & (1u << p))) return -1;
    } else {
        if (s->phase[0] != 1 || s->phase[1] != 2 || s->phase[4] != 1 ||
            s->phase[5] != 1 || s->phase[6] != 1 || s->phase[7] != 2 ||
            s->state[p] != 1 || s->mask > 15 || !(s->mask & (1u << p))) return -1;
    }
    if (s->slot[p] != p || s->shuffle[p]) return -2;
    id = s->cursor[p];
    if (s->game == 4) {
        if (id >= sizeof pws1_ids) return -2;
        id = pws1_ids[id];
    } else if (id >= 16 || id == 12 || id == 13) {
        /* The traced chooser skips boss IDs 12/13, including random picks.
         * Their existence in the shell name table does not make them choices. */
        return -2;
    }
    return character_name(s->game, id, s->variant) ? (int)id : -2;
}

void pws_selection_copy(const pws_select_sample *s,
                        game_selection_choice out[GAME_SELECTION_OWNERS])
{
    if (!out) return;
    memset(out, 0, sizeof(*out) * GAME_SELECTION_OWNERS);
    for (unsigned p = 0; p < GAME_SELECTION_OWNERS; p++) {
        int id = pws_select_decode(s, p);
        out[p].status = id == -1 ? GAME_SELECTION_INACTIVE : GAME_SELECTION_UNAVAILABLE;
        if (id < 0) continue;
        out[p].status = GAME_SELECTION_VALID;
        out[p].value = (unsigned)id;
        swprintf(out[p].label, GAME_SELECTION_LABEL, L"%hs",
                 character_name(s->game, (unsigned)id, s->variant));
    }
}
