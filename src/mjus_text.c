/* See mjus_text.h. Only the MJUS-specific part: where its text is drawn and
 * how its bytes read. Buffering, publication, de-duplication, speech and the
 * ` key are shared (src/game_text.h). */
#include "mjus_text.h"
#include "game_text.h"
#include "log.h"
#include "../libs/minhook/include/MinHook.h"
#include <string.h>
#include <wchar.h>

/* 0x140B96130: ecx x, edx y, xmm2 scale, r9d colour, [rsp+0x20] string. Each
 * byte above 0x10 draws glyph table 0x141792D20[byte]; 0x10 and below, and
 * (sign-extended) bytes from 0x80, only advance the pen (0x140B961CB). */
typedef void (*pfn_draw)(int x, int y, float scale, int colour, const char *text);
static pfn_draw g_real_draw;
static volatile LONG g_installed;
static const unsigned char DRAW_SIG[] = { 0x4C,0x8B,0xDC, 0x49,0x89,0x5B,0x18, 0x49,0x89,0x73,0x20 };

static game_text_source source;
static game_text_state state;       /* zeroed, so .bss: see game_text.h */
static tc_frame collecting;         /* the MJUS update's thread only */

static void draw_detour(int x, int y, float scale, int colour, const char *text)
{
    tc_frame_add(&collecting, (float)x, (float)y, colour, 0, text, TC_RAW_CAP);
    g_real_draw(x, y, scale, colour, text);
}

BOOL mjus_text_install(void)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery((LPCVOID)(ULONG_PTR)MJUS_TEXT_FN, &mbi, sizeof mbi) ||
        mbi.State != MEM_COMMIT) {
        log_line("mjus-text: ⚠ COULD NOT read 0x%llx -- concluding nothing", MJUS_TEXT_FN);
        return FALSE;
    }
    if (memcmp((const void *)(ULONG_PTR)MJUS_TEXT_FN, DRAW_SIG, sizeof DRAW_SIG)) {
        log_line("mjus-text: ⛔ entry bytes differ from the audited build; refusing");
        return FALSE;
    }
    if (MH_CreateHook((LPVOID)(ULONG_PTR)MJUS_TEXT_FN, (LPVOID)draw_detour,
                      (LPVOID *)&g_real_draw) != MH_OK ||
        MH_EnableHook((LPVOID)(ULONG_PTR)MJUS_TEXT_FN) != MH_OK) {
        log_line("mjus-text: ⛔ MinHook refused 0x%llx", MJUS_TEXT_FN);
        return FALSE;
    }
    InterlockedExchange(&g_installed, 1);
    log_line("mjus-text: hooked the text draw at 0x%llx", MJUS_TEXT_FN);
    return TRUE;
}

void mjus_text_remove(void)
{
    if (InterlockedCompareExchange(&g_installed, 0, 1) == 1)
        MH_DisableHook((LPVOID)(ULONG_PTR)MJUS_TEXT_FN);
}

void mjus_text_capture(void)
{
    tc_frame_publish(&state.publisher, &collecting);
}

/* The copyright line begins with '\', which the font draws as the copyright
 * sign: omitted, as CFJ omits its special glyph. Pen-only bytes read as a
 * space. The literals are plain ASCII (image strings at 0x1410E2C10..). */
BOOL mjus_text_decode(int style, const char *raw, wchar_t *out, SIZE_T cap)
{
    SIZE_T i = 0, n = 0;
    (void)style;
    out[0] = 0;
    if (cap < 2) return FALSE;
    while (i < TC_RAW_CAP && raw[i]) {
        unsigned c = (unsigned char)raw[i++];
        if (c == '\\') continue;
        if (c <= 0x10 || c >= 0x80) c = ' ';
        if (c < 0x20 || c > 0x7E) return FALSE;
        if (n + 1 >= cap) return FALSE;
        out[n++] = (wchar_t)c;
    }
    out[n] = 0;
    return tc_squeeze(out) > 0;
}

static game_text_source source =
    GAME_TEXT_SOURCE("mjus-text", "Project Justice", mjus_text_decode, NULL, &state);

static tc_read_result read_current(void) { return game_text_read_current(&source); }

void mjus_text_observe(ULONGLONG root) { game_text_observe(&source, root); }

void mjus_text_register(void) { tc_register("Project Justice", read_current); }
