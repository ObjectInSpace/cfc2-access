/* See pickup_speech.h. */
#include "pickup_speech.h"
#include "audio.h"
#include "mod_text.h"
#include "speech.h"
#include <stdio.h>

BOOL pickup_say(const char *name, pickup_kind kind, bool human)
{
    static const mt_id word[PICKUP_KINDS] = {
        MT_PICK_STONE, MT_PICK_FOOD, MT_PICK_ITEM, MT_PICK_TREASURE, MT_PICK_THROWABLE };
    int mode = audio_option_get(AUDIO_OPT_PICKUP_SPEECH);
    wchar_t who[64], say[160];
    if (!name || (unsigned)kind >= PICKUP_KINDS || !mode || (mode == 1 && !human)) return FALSE;
    if (MultiByteToWideChar(CP_UTF8, 0, name, -1, who, 64) <= 0) return FALSE;
    mt_fmt(say, 160, MT_PICKUP, who, mt(word[kind]), NULL);
    return speech_say(say, FALSE);
}
