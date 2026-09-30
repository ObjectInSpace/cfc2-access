/* Diagnostic observer, not the session router. No observation here authorizes
 * speech, audio or retaining a native pointer. See the boundary run card. */
#include "collection_probe.h"
#include "memsafe.h"
#include "log.h"
#include "../libs/minhook/include/MinHook.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

typedef struct { const char *name; uint64_t address; size_t size; uint64_t hash; } probe_site;
#include "collection_probe_sites.inc"
enum { SITE_UPDATE, SITE_LOAD, SITE_REGISTER, SITE_CLEANUP, SITE_RELEASE, SITE_COUNT };
enum { PROBE_CAP = 4096, PROBE_BATCH = 128 };
typedef struct {
    uint64_t sequence, ticks, session, root, argument;
    DWORD thread, site, exiting, depth, readable, game, rom, bytes;
    DWORD load_count, intermediate, transfer;
    int argument_length, result;
} probe_event;

static SRWLOCK g_lock = SRWLOCK_INIT;
static probe_event g_ring[PROBE_CAP];
static unsigned g_head, g_count;
static uint64_t g_sequence;
static uint64_t g_deadline_ticks;
static volatile LONG g_active, g_dropped, g_in_flight;
static __thread unsigned g_depth;
static FILE *g_file;
static ULONGLONG g_started, g_last_drain;
static BOOL g_attempted;
typedef void (*one_argument)(void *);
typedef unsigned char (*load_callback)(void *, int);
static one_argument g_real_update, g_real_register, g_real_cleanup, g_real_release;
static load_callback g_real_load;

/* No VirtualQuery in the detour. A nested foreign-memory guard cannot safely
 * replace the existing TLS jump buffer, so report that sample as unreadable.
 * A successful read is still only a diagnostic bracket, not an atomic frame. */
static void read_state(probe_event *event)
{
    volatile BOOL faulted;
    volatile uintptr_t state = 0;
    if (g_mem_guard_armed) return;
    MEM_GUARDED(faulted, {
        state = *(volatile uintptr_t *)(uintptr_t)UINT64_C(0x141c14f98);
        if (state) {
            event->session = state;
            event->game = *(volatile DWORD *)(state + 8);
            event->rom = *(volatile DWORD *)(state + 0x44);
            event->root = *(volatile uintptr_t *)(state + 0x1b0);
            event->bytes = *(volatile DWORD *)(state + 0x1b8);
            event->load_count = *(volatile DWORD *)(state + 0x76c);
            event->intermediate = *(volatile DWORD *)(state + 0x770);
            event->transfer = *(volatile DWORD *)(state + 0x4654);
        }
    });
    event->readable = !faulted;
}

/* A test can replace the reader without dereferencing the game's address. */
static void (*g_read_state)(probe_event *) = read_state;

static void record_event(unsigned site, unsigned exiting, void *argument,
                         int argument_length, int result)
{
    probe_event event = {0};
    LARGE_INTEGER ticks;
    DWORD last_error;
    /* Increment before checking active: otherwise the worker could write a
     * clean footer between that check and a delayed producer's drop count. */
    InterlockedIncrement(&g_in_flight);
    if (!InterlockedCompareExchange(&g_active, 0, 0)) {
        InterlockedDecrement(&g_in_flight);
        return;
    }
    last_error = GetLastError();
    QueryPerformanceCounter(&ticks);
    if ((uint64_t)ticks.QuadPart >= g_deadline_ticks) {
        InterlockedExchange(&g_active, 0);
        goto done;
    }
    event.ticks = (uint64_t)ticks.QuadPart;
    event.thread = GetCurrentThreadId();
    event.site = site;
    event.exiting = exiting;
    event.depth = g_depth;
    event.argument = (uintptr_t)argument;
    event.argument_length = argument_length;
    event.result = result;
    g_read_state(&event);
    if (!TryAcquireSRWLockExclusive(&g_lock)) {
        InterlockedIncrement(&g_dropped);
        goto done;
    }
    if (InterlockedCompareExchange(&g_active, 0, 0)) {
        if (g_count == PROBE_CAP) InterlockedIncrement(&g_dropped);
        else {
            event.sequence = ++g_sequence;
            g_ring[(g_head + g_count) % PROBE_CAP] = event;
            ++g_count;
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
done:
    SetLastError(last_error);
    InterlockedDecrement(&g_in_flight);
}

static void update_detour(void *object)
{
    ++g_depth;
    record_event(SITE_UPDATE, 0, object, 0, -1);
    g_real_update(object);
    record_event(SITE_UPDATE, 1, object, 0, -1);
    --g_depth;
}

static unsigned char load_detour(void *source, int length)
{
    unsigned char result;
    record_event(SITE_LOAD, 0, source, length, -1);
    result = g_real_load(source, length);
    record_event(SITE_LOAD, 1, source, length, result);
    return result;
}

static void register_detour(void *buffer)
{
    record_event(SITE_REGISTER, 0, buffer, 0, -1);
    g_real_register(buffer);
    record_event(SITE_REGISTER, 1, buffer, 0, -1);
}

static void cleanup_detour(void *object)
{
    record_event(SITE_CLEANUP, 0, object, 0, -1);
    g_real_cleanup(object);
    record_event(SITE_CLEANUP, 1, object, 0, -1);
}

static void release_detour(void *object)
{
    record_event(SITE_RELEASE, 0, object, 0, -1);
    g_real_release(object);
    record_event(SITE_RELEASE, 1, object, 0, -1);
}

static uint64_t fingerprint(const unsigned char *bytes, size_t size)
{
    uint64_t value = UINT64_C(0xcbf29ce484222325);
    for (size_t i = 0; i < size; ++i)
        value = (value ^ bytes[i]) * UINT64_C(0x100000001b3);
    return value;
}

static BOOL verify_sites(void)
{
    unsigned char bytes[4096];
    for (unsigned i = 0; i < SITE_COUNT; ++i) {
        const probe_site *site = &g_sites[i];
        if (!site->size || site->size > sizeof bytes ||
            !mem_copy(bytes, (const void *)(uintptr_t)site->address, site->size) ||
            fingerprint(bytes, site->size) != site->hash) {
            log_line("collection-probe: reviewed body differs/unreadable: %s", site->name);
            return FALSE;
        }
    }
    return TRUE;
}

static void rollback_hooks(unsigned created)
{
    for (unsigned i = 0; i < created; ++i) {
        void *address = (void *)(uintptr_t)g_sites[i].address;
        MH_STATUS status = MH_DisableHook(address);
        /* Never free a trampoline if disabling failed. Passive detours remain
         * valid until process exit; they still forward to their originals. */
        if (status == MH_OK || status == MH_ERROR_DISABLED) MH_RemoveHook(address);
    }
}

BOOL collection_probe_install(void)
{
    wchar_t path[MAX_PATH], *slash;
    LARGE_INTEGER frequency, start_ticks;
    MH_STATUS status;
    unsigned created = 0;
    void *detours[SITE_COUNT] = {(void *)update_detour, (void *)load_detour,
        (void *)register_detour, (void *)cleanup_detour, (void *)release_detour};
    void **originals[SITE_COUNT] = {(void **)&g_real_update, (void **)&g_real_load,
        (void **)&g_real_register, (void **)&g_real_cleanup, (void **)&g_real_release};
    if (g_attempted) return InterlockedCompareExchange(&g_active, 0, 0) != 0;
    g_attempted = TRUE;
    if (!mem_guard_install() || !verify_sites()) return FALSE;
    DWORD length = GetModuleFileNameW(NULL, path, MAX_PATH);
    if (!length || length >= MAX_PATH || !(slash = wcsrchr(path, L'\\'))) return FALSE;
    size_t remaining = MAX_PATH - (size_t)(slash + 1 - path);
    int written = _snwprintf(slash + 1, remaining, L"cfc2_boundary_%lu_%llu.csv",
                            GetCurrentProcessId(), (unsigned long long)GetTickCount64());
    if (written < 0 || (size_t)written >= remaining) return FALSE;
    g_file = _wfopen(path, L"wb");
    if (!g_file) { log_line("collection-probe: cannot create capture file"); return FALSE; }
    QueryPerformanceFrequency(&frequency);
    fprintf(g_file, "# boundary_probe,1,qpc_frequency,%llu,duration_ms,120000\n",
            (unsigned long long)frequency.QuadPart);
    fputs("sequence,ticks,thread,site,exit,depth,readable,session,root,game,rom,bytes,"
          "load_count,intermediate,transfer,argument,length,result\n", g_file);
    if (fflush(g_file) || ferror(g_file)) goto fail;
    status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) goto fail;
    for (unsigned i = 0; i < SITE_COUNT; ++i) {
        if (MH_CreateHook((void *)(uintptr_t)g_sites[i].address, detours[i], originals[i]) != MH_OK)
            goto fail;
        ++created;
    }
    for (unsigned i = 0; i < SITE_COUNT; ++i)
        if (MH_QueueEnableHook((void *)(uintptr_t)g_sites[i].address) != MH_OK) goto fail;
    g_started = GetTickCount64();
    g_last_drain = g_started;
    QueryPerformanceCounter(&start_ticks);
    g_deadline_ticks = (uint64_t)start_ticks.QuadPart + (uint64_t)frequency.QuadPart * 120;
    /* Capture starts only after all hooks are enabled. The installation
     * interval is explicitly outside the observation window. */
    if (MH_ApplyQueued() != MH_OK) goto fail;
    InterlockedExchange(&g_active, 1);
    log_line("collection-probe: 120-second capture started: %ls", path);
    return TRUE;
fail:
    rollback_hooks(created);
    fputs("# install_failed\n", g_file);
    fclose(g_file);
    g_file = NULL;
    log_line("collection-probe: install failed; no usable capture");
    return FALSE;
}

static unsigned take_events(probe_event *events)
{
    unsigned count;
    AcquireSRWLockExclusive(&g_lock);
    count = g_count < PROBE_BATCH ? g_count : PROBE_BATCH;
    for (unsigned i = 0; i < count; ++i) events[i] = g_ring[(g_head+i) % PROBE_CAP];
    g_head = (g_head + count) % PROBE_CAP;
    g_count -= count;
    ReleaseSRWLockExclusive(&g_lock);
    return count;
}

void collection_probe_tick(void)
{
    probe_event events[PROBE_BATCH];
    ULONGLONG now;
    unsigned count;
    if (!g_file) return;
    now = GetTickCount64();
    if (now - g_last_drain < 250) return;
    g_last_drain = now;
    if (now - g_started >= 120000) InterlockedExchange(&g_active, 0);
    count = take_events(events);
    for (unsigned i = 0; i < count; ++i) {
        const probe_event *e = &events[i];
        fprintf(g_file, "%llu,%llu,%lu,%lu,%lu,%lu,%lu,%llx,%llx,%lu,%lu,%lu,"
                "%lu,%lu,%lu,%llx,%d,%d\n",
                (unsigned long long)e->sequence, (unsigned long long)e->ticks,
                e->thread, e->site, e->exiting, e->depth, e->readable,
                (unsigned long long)e->session, (unsigned long long)e->root,
                e->game, e->rom, e->bytes, e->load_count, e->intermediate,
                e->transfer, (unsigned long long)e->argument, e->argument_length, e->result);
    }
    if (fflush(g_file) || ferror(g_file)) {
        InterlockedExchange(&g_active, 0);
        fclose(g_file);
        g_file = NULL;
        log_line("collection-probe: capture write failed; file is incomplete");
    } else if (!InterlockedCompareExchange(&g_active, 0, 0) && count == 0 &&
               !InterlockedCompareExchange(&g_in_flight, 0, 0)) {
        fprintf(g_file, "# complete,dropped,%ld\n", InterlockedCompareExchange(&g_dropped, 0, 0));
        BOOL saved = fflush(g_file) == 0 && !ferror(g_file);
        if (fclose(g_file)) saved = FALSE;
        g_file = NULL;
        log_line("collection-probe: window ended; dropped=%ld, saved=%d",
                 InterlockedCompareExchange(&g_dropped, 0, 0), (int)saved);
    }
}
