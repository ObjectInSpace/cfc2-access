#ifndef CFC2_FRAME_LIVENESS_H
#define CFC2_FRAME_LIVENESS_H
#include <windows.h>

/* Is the game's own per-frame update running? Worker thread only.
 *
 * Quitting a game leaves its state block in memory, frozen on whatever
 * screen it was on, and every published-root and shell gate still passes:
 * the shell's game field follows the game-select cursor. So a select
 * reader that trusts the block alone re-announced Alpha 3's last highlight
 * ("Game: Ryu") each time the cursor came back to Alpha 3 after quitting.
 * The frame hook's counter advances only while the update runs, so it
 * separates a running game from a frozen one -- and from a paused one,
 * which is treated the same way.
 *
 * Live = the counter changed within stall_ms. A first sighting is not live:
 * one value proves nothing about motion. */
typedef struct {
    LONG last;
    ULONGLONG changed;
    BOOL seen;
} frame_liveness;

static inline BOOL frame_liveness_live(frame_liveness *l, LONG count, ULONGLONG now,
                                       ULONGLONG stall_ms)
{
    if (!l->seen) {
        l->seen = TRUE;
        l->last = count;
        l->changed = 0;
        return FALSE;
    }
    if (count != l->last) {
        l->last = count;
        l->changed = now;
    }
    return l->changed && now - l->changed < stall_ms;
}
#endif
