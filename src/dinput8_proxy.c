/*
 * CFC2 Access — dinput8.dll proxy.
 *
 * CapcomFightingCollection2.exe statically imports exactly one symbol from
 * dinput8.dll (DirectInput8Create — verified by parsing the import table), and
 * Windows searches the exe's own directory before System32. Dropping this DLL
 * next to the exe therefore gets us loaded at process start, before the game's
 * own code runs.
 *
 * We forward the single real export to the genuine system dinput8.dll and use
 * DllMain only to bootstrap the worker thread.
 *
 * NOTE: the exe is wrapped in Enigma Protector, which is hostile to injected
 * DLLs. Whether this survives is exactly what Stage 0 measures.
 */

#include <windows.h>
#include "mod.h"

typedef HRESULT(WINAPI *DirectInput8Create_t)(HINSTANCE hinst, DWORD dwVersion,
                                              REFIID riidltf, LPVOID *ppvOut,
                                              LPUNKNOWN punkOuter);

static HMODULE g_real_dinput8;
static DirectInput8Create_t g_real_DirectInput8Create;

static void load_real_dinput8(void)
{
    if (g_real_dinput8)
        return;
    wchar_t path[MAX_PATH];
    UINT len = GetSystemDirectoryW(path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH - 13)
        return;
    lstrcatW(path, L"\\dinput8.dll");
    g_real_dinput8 = LoadLibraryW(path);
    if (g_real_dinput8)
        g_real_DirectInput8Create =
            (DirectInput8Create_t)(void *)GetProcAddress(
                g_real_dinput8, "DirectInput8Create");
}

__declspec(dllexport) HRESULT WINAPI DirectInput8Create(HINSTANCE hinst,
                                                        DWORD dwVersion,
                                                        REFIID riidltf,
                                                        LPVOID *ppvOut,
                                                        LPUNKNOWN punkOuter)
{
    load_real_dinput8();
    if (!g_real_DirectInput8Create)
        return E_FAIL;
    return g_real_DirectInput8Create(hinst, dwVersion, riidltf, ppvOut,
                                     punkOuter);
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    (void)lpvReserved;
    switch (fdwReason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hinstDLL);
        mod_start(hinstDLL);
        break;
    case DLL_PROCESS_DETACH:
        mod_stop();
        break;
    }
    return TRUE;
}
