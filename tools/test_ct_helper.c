/*
 * A second module, whose only job is to call GetAsyncKeyState from OUTSIDE the
 * test exe.
 *
 * ⭐⭐ WHY A WHOLE SEPARATE DLL RATHER THAN A FUNCTION IN THE TEST
 * The single claim the caller trace rests on is that it can tell OUR calls from
 * the GAME's. In-game that distinction is between two modules -- dinput8.dll
 * (us) and the exe (the shell). A test whose "foreign" caller lives in the same
 * module as its "self" caller cannot exercise that distinction at all: both
 * would be inside the same base/size range, so a completely broken filter and a
 * perfect one would give identical results.
 *
 * So the test needs two real modules, and the assertions then mean what they
 * say: `foreign_calls` counts calls from here, `self_calls` counts calls from
 * the exe, and sabotaging the filter changes the answer.
 *
 * Two entry points with distinct VK behaviour, because the trace also claims to
 * count DISTINCT VKs per site -- the signal that separates a real input reader
 * (sweeps the key table) from a hotkey check (asks about one key).
 */

#include <windows.h>

/* A "hotkey check": always asks about the same key. */
__declspec(dllexport) void ct_helper_one_key(int times)
{
    for (int i = 0; i < times; i++)
        GetAsyncKeyState(VK_F7);
}

/* An "input reader": sweeps a range of keys, like the shell's KB_* table.
 * ⚠ Must be a DIFFERENT function from the one above so it is a different
 * return address, i.e. a genuinely distinct site in the table. */
__declspec(dllexport) void ct_helper_sweep(int first, int count)
{
    for (int i = 0; i < count; i++)
        GetAsyncKeyState(first + i);
}
