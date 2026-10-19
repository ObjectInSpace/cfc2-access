/* Project Justice's immediate-draw text decoder against its own literals
 * (image strings 0x1410E2C10.., 0x14106E368, 0x1410E43C0). Run from the
 * repository root. */
#include "../src/mjus_text.h"
#include <assert.h>
#include <stdio.h>
#include <wchar.h>

#include "../libs/minhook/include/MinHook.h"
/* The draw hook is never installed here. */
MH_STATUS WINAPI MH_CreateHook(LPVOID t, LPVOID d, LPVOID *o) { (void)t; (void)d; (void)o; return MH_UNKNOWN; }
MH_STATUS WINAPI MH_EnableHook(LPVOID t) { (void)t; return MH_UNKNOWN; }
MH_STATUS WINAPI MH_DisableHook(LPVOID t) { (void)t; return MH_UNKNOWN; }
void log_line(const char *fmt, ...) { (void)fmt; }
BOOL speech_say(const wchar_t *s, BOOL interrupt) { (void)s; (void)interrupt; return TRUE; }

static const wchar_t *dec(const char *s)
{
    static wchar_t out[128];
    return mjus_text_decode(0, s, out, 128) ? out : NULL;
}

int main(void)
{
    const wchar_t *w;
    assert((w = dec("INSERT 2 MORE COIN(S)")) && !wcscmp(w, L"INSERT 2 MORE COIN(S)"));
    assert((w = dec("CREDIT(S) 3")) && !wcscmp(w, L"CREDIT(S) 3"));
    assert((w = dec("ROUND BONUS")) && !wcscmp(w, L"ROUND BONUS"));
    /* The copyright sign is the font's glyph for '\': omitted. */
    assert((w = dec("\\CAPCOM CO., LTD. 2000 ALL RIGHTS RESERVED."))
           && !wcscmp(w, L"CAPCOM CO., LTD. 2000 ALL RIGHTS RESERVED."));
    /* The score format pads with spaces; squeezed. */
    assert((w = dec("   123456   ")) && !wcscmp(w, L"123456"));
    /* Pen-only bytes read as space. */
    assert((w = dec("A\x05" "B")) && !wcscmp(w, L"A B"));
    assert(!dec("") && !dec("   "));
    printf("PASS: Project Justice text decoder: coin, credit, round bonus, copyright, padding\n");
    return 0;
}
