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
enum { SITE_UPDATE, SITE_LOAD, SITE_REGISTER, SITE_CLEANUP, SITE_RELEASE,
       SITE_QUICK_LOAD, SITE_APPLICATION, SITE_TRAINING, SITE_INITIALIZE,
       SITE_PAUSE, SITE_COUNT, SITE_WAIT_SINGLE = SITE_COUNT, SITE_WAIT_MULTIPLE };
enum { LEGACY_SITES = 6, HOOK_COUNT = SITE_COUNT + 2,
       PROBE_CAP = 4096, PROBE_BATCH = 128, WAIT_HANDLES = 64 };
typedef struct {
    uint64_t sequence, ticks, session, root, argument;
    DWORD thread, site, exiting, depth, readable, game, rom, bytes;
    DWORD load_count, intermediate, transfer;
    int argument_length, result;
    uint64_t invocation, application_invocation, return_site, handles[WAIT_HANDLES];
    DWORD versus, training, reset_state, reset_request, character_gate;
    DWORD wait_count, wait_all, timeout, handles_readable, last_error;
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
static BOOL g_lifetime;
static unsigned g_native_sites = LEGACY_SITES;
static void *g_targets[HOOK_COUNT];
static __thread uint64_t g_call_id, g_application_id;
typedef void (*one_argument)(void *);
typedef unsigned char (*load_callback)(void *, int);
static one_argument g_real_update, g_real_register, g_real_cleanup, g_real_release;
static load_callback g_real_load;
/* Reviewed caller sign-extends EAX into a slot index. Preserve all 32 bits. */
typedef int (*quick_load_callback)(void *);
static quick_load_callback g_real_quick_load;
static one_argument g_real_application, g_real_training, g_real_initialize, g_real_pause;
static DWORD (WINAPI *g_real_single)(HANDLE, DWORD);
static DWORD (WINAPI *g_real_multiple)(DWORD, const HANDLE *, BOOL, DWORD);

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
            if (g_lifetime) {
                event->versus = *(volatile DWORD *)(state + 0x80c);
                event->training = *(volatile DWORD *)(state + 0x828);
                event->reset_state = *(volatile DWORD *)(state + 0x830);
                event->reset_request = *(volatile DWORD *)(state + 0x838);
                event->character_gate = *(volatile DWORD *)(state + 0x840);
            }
        }
    });
    event->readable = !faulted;
}

/* A test can replace the reader without dereferencing the game's address. */
static void (*g_read_state)(probe_event *) = read_state;

static void record_extended(unsigned site, unsigned exiting, void *argument,
                            int argument_length, int result, const probe_event *extra)
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
    if (extra) event = *extra;
    event.application_invocation = g_application_id;
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

static void record_event(unsigned site, unsigned exiting, void *argument,
                         int argument_length, int result)
{
    record_extended(site, exiting, argument, argument_length, result, NULL);
}

/* IDs are per-thread, never reused. Exhaustion disables observation. */
static uint64_t next_invocation(void)
{
    if (g_call_id == UINT64_MAX) {
        InterlockedIncrement(&g_dropped);
        InterlockedExchange(&g_active, 0);
        return 0;
    }
    return ++g_call_id;
}

static void observe_one(unsigned site, void *object, one_argument original, uintptr_t caller)
{
    if (!InterlockedCompareExchange(&g_active, 0, 0)) { original(object); return; }
    DWORD incoming_error = GetLastError();
    probe_event extra = {0};
    uint64_t outer = g_application_id;
    extra.invocation = next_invocation();
    extra.return_site = caller;
    if (site == SITE_APPLICATION) g_application_id = extra.invocation;
    SetLastError(incoming_error);
    record_extended(site, 0, object, 0, -1, &extra);
    original(object);
    DWORD outgoing_error = GetLastError();
    record_extended(site, 1, object, 0, -1, &extra);
    g_application_id = outer;
    SetLastError(outgoing_error);
}

#define ONE_DETOUR(name, site) \
static void name##_detour(void *object) { \
    observe_one(site, object, g_real_##name, (uintptr_t)__builtin_return_address(0)); \
}
ONE_DETOUR(application, SITE_APPLICATION)
ONE_DETOUR(training, SITE_TRAINING)
ONE_DETOUR(initialize, SITE_INITIALIZE)
ONE_DETOUR(pause, SITE_PAUSE)

static BOOL single_caller(uintptr_t caller)
{
    return caller == UINT64_C(0x1401f7da2) || caller == UINT64_C(0x1401f847f) ||
           caller == UINT64_C(0x1401f81b9) || caller == UINT64_C(0x1401f8208);
}

static void copy_wait_handles(probe_event *extra, const HANDLE *handles)
{
    volatile BOOL faulted;
    if (!extra->wait_count || extra->wait_count > WAIT_HANDLES || g_mem_guard_armed) return;
    MEM_GUARDED(faulted, {
        for (DWORD i = 0; i < extra->wait_count; ++i)
            extra->handles[i] = (uintptr_t)((const volatile HANDLE *)handles)[i];
    });
    extra->handles_readable = !faulted;
}

static DWORD observe_single(uintptr_t caller, HANDLE handle, DWORD timeout)
{
    if (!single_caller(caller) || !InterlockedCompareExchange(&g_active, 0, 0))
        return g_real_single(handle, timeout);
    DWORD incoming_error = GetLastError();
    probe_event extra = {0};
    extra.invocation = next_invocation();
    extra.return_site = caller;
    extra.wait_count = extra.handles_readable = 1;
    extra.timeout = timeout;
    extra.handles[0] = (uintptr_t)handle;
    SetLastError(incoming_error);
    record_extended(SITE_WAIT_SINGLE, 0, handle, 0, -1, &extra);
    DWORD result = g_real_single(handle, timeout);
    extra.last_error = GetLastError();
    record_extended(SITE_WAIT_SINGLE, 1, handle, 0, (int32_t)result, &extra);
    SetLastError(extra.last_error);
    return result;
}

static DWORD WINAPI single_detour(HANDLE handle, DWORD timeout)
{
    return observe_single((uintptr_t)__builtin_return_address(0), handle, timeout);
}

static DWORD observe_multiple(uintptr_t caller, DWORD count, const HANDLE *handles,
                              BOOL all, DWORD timeout)
{
    if (caller != UINT64_C(0x1401f80ad) || !InterlockedCompareExchange(&g_active, 0, 0))
        return g_real_multiple(count, handles, all, timeout);
    DWORD incoming_error = GetLastError();
    probe_event extra = {0};
    extra.invocation = next_invocation();
    extra.return_site = caller;
    extra.wait_count = count;
    extra.wait_all = (DWORD)all;
    extra.timeout = timeout;
    copy_wait_handles(&extra, handles);
    SetLastError(incoming_error);
    record_extended(SITE_WAIT_MULTIPLE, 0, (void *)handles, 0, -1, &extra);
    DWORD result = g_real_multiple(count, handles, all, timeout);
    extra.last_error = GetLastError();
    record_extended(SITE_WAIT_MULTIPLE, 1, (void *)handles, 0, (int32_t)result, &extra);
    SetLastError(extra.last_error);
    return result;
}

static DWORD WINAPI multiple_detour(DWORD count, const HANDLE *handles, BOOL all, DWORD timeout)
{
    return observe_multiple((uintptr_t)__builtin_return_address(0), count, handles, all, timeout);
}

static void update_detour(void *object)
{
    DWORD incoming_error = GetLastError();
    ++g_depth;
    SetLastError(incoming_error);
    record_event(SITE_UPDATE, 0, object, 0, -1);
    g_real_update(object);
    DWORD outgoing_error = GetLastError();
    record_event(SITE_UPDATE, 1, object, 0, -1);
    --g_depth;
    SetLastError(outgoing_error);
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

static int quick_load_detour(void *owner)
{
    int result;
    record_event(SITE_QUICK_LOAD, 0, owner, 0, -1);
    result = g_real_quick_load(owner);
    record_event(SITE_QUICK_LOAD, 1, owner, 0, result);
    return result;
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
    unsigned guards = g_lifetime ? sizeof g_wait_guards / sizeof g_wait_guards[0] : 0;
    for (unsigned i = 0; i < g_native_sites + guards; ++i) {
        const probe_site *site = i < g_native_sites ? &g_sites[i] : &g_wait_guards[i-g_native_sites];
        if (!site->size || site->size > sizeof bytes ||
            !mem_copy(bytes, (const void *)(uintptr_t)site->address, site->size) ||
            fingerprint(bytes, site->size) != site->hash) {
            log_line("collection-probe: reviewed body differs/unreadable: %s", site->name);
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL resolve_wait_targets(void)
{
    HMODULE module = GetModuleHandleW(L"kernel32.dll");
    const uintptr_t slots[] = {UINT64_C(0x140fdc350), UINT64_C(0x140fdc0c8)};
    const char *names[] = {"WaitForSingleObject", "WaitForMultipleObjects"};
    if (!module) return FALSE;
    for (unsigned i = 0; i < 2; ++i) {
        void *iat = NULL;
        void *export = (void *)GetProcAddress(module, names[i]);
        if (!export || !mem_copy(&iat, (void *)slots[i], sizeof iat) || iat != export)
            return FALSE;
        g_targets[SITE_COUNT+i] = export;
    }
    return TRUE;
}

static BOOL (*g_resolve_wait_targets)(void) = resolve_wait_targets;

static void rollback_hooks(unsigned created)
{
    for (unsigned i = 0; i < created; ++i) {
        void *address = g_targets[i];
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
    void *detours[HOOK_COUNT] = {(void *)update_detour, (void *)load_detour,
        (void *)register_detour, (void *)cleanup_detour, (void *)release_detour,
        (void *)quick_load_detour, (void *)application_detour, (void *)training_detour,
        (void *)initialize_detour, (void *)pause_detour, (void *)single_detour,
        (void *)multiple_detour};
    void **originals[HOOK_COUNT] = {(void **)&g_real_update, (void **)&g_real_load,
        (void **)&g_real_register, (void **)&g_real_cleanup, (void **)&g_real_release,
        (void **)&g_real_quick_load, (void **)&g_real_application, (void **)&g_real_training,
        (void **)&g_real_initialize, (void **)&g_real_pause, (void **)&g_real_single,
        (void **)&g_real_multiple};
    unsigned hook_count = g_lifetime ? HOOK_COUNT : LEGACY_SITES;
    if (g_attempted) return InterlockedCompareExchange(&g_active, 0, 0) != 0;
    g_attempted = TRUE;
    g_native_sites = g_lifetime ? SITE_COUNT : LEGACY_SITES;
    if (!mem_guard_install() || !verify_sites()) return FALSE;
    for (unsigned i = 0; i < g_native_sites; ++i) g_targets[i] = (void *)(uintptr_t)g_sites[i].address;
    if (g_lifetime && !g_resolve_wait_targets()) return FALSE;
    DWORD length = GetModuleFileNameW(NULL, path, MAX_PATH);
    if (!length || length >= MAX_PATH || !(slash = wcsrchr(path, L'\\'))) return FALSE;
    size_t remaining = MAX_PATH - (size_t)(slash + 1 - path);
    int written = _snwprintf(slash + 1, remaining, L"cfc2_boundary_%lu_%llu.csv",
                            GetCurrentProcessId(), (unsigned long long)GetTickCount64());
    if (written < 0 || (size_t)written >= remaining) return FALSE;
    g_file = _wfopen(path, L"wb");
    if (!g_file) { log_line("collection-probe: cannot create capture file"); return FALSE; }
    QueryPerformanceFrequency(&frequency);
    fprintf(g_file, "# boundary_probe,%u,qpc_frequency,%llu,duration_ms,120000\n",
            g_lifetime ? 3 : 2, (unsigned long long)frequency.QuadPart);
    fputs("sequence,ticks,thread,site,exit,depth,readable,session,root,game,rom,bytes,"
          "load_count,intermediate,transfer,argument,length,result", g_file);
    if (g_lifetime)
        fputs(",invocation,application_invocation,return_site,versus,training,reset_state,"
              "reset_request,character_gate,wait_count,wait_all,timeout,handles_readable,last_error,handles", g_file);
    fputc('\n', g_file);
    if (fflush(g_file) || ferror(g_file)) goto fail;
    status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) goto fail;
    for (unsigned i = 0; i < hook_count; ++i) {
        if (MH_CreateHook(g_targets[i], detours[i], originals[i]) != MH_OK)
            goto fail;
        ++created;
    }
    for (unsigned i = 0; i < hook_count; ++i)
        if (MH_QueueEnableHook(g_targets[i]) != MH_OK) goto fail;
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

BOOL collection_probe_install_lifetime(void)
{
    if (g_attempted) return FALSE;
    g_lifetime = TRUE;
    return collection_probe_install();
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
    if (now - g_last_drain < (g_lifetime ? 25 : 250)) return;
    g_last_drain = now;
    if (now - g_started >= 120000) InterlockedExchange(&g_active, 0);
    count = take_events(events);
    for (unsigned i = 0; i < count; ++i) {
        const probe_event *e = &events[i];
        fprintf(g_file, "%llu,%llu,%lu,%lu,%lu,%lu,%lu,%llx,%llx,%lu,%lu,%lu,"
                "%lu,%lu,%lu,%llx,%d,%d",
                (unsigned long long)e->sequence, (unsigned long long)e->ticks,
                e->thread, e->site, e->exiting, e->depth, e->readable,
                (unsigned long long)e->session, (unsigned long long)e->root,
                e->game, e->rom, e->bytes, e->load_count, e->intermediate,
                e->transfer, (unsigned long long)e->argument, e->argument_length, e->result);
        if (g_lifetime) {
            fprintf(g_file, ",%llu,%llu,%llx,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,",
                    (unsigned long long)e->invocation, (unsigned long long)e->application_invocation,
                    (unsigned long long)e->return_site, e->versus, e->training, e->reset_state,
                    e->reset_request, e->character_gate, e->wait_count, e->wait_all,
                    e->timeout, e->handles_readable, e->last_error);
            if (e->handles_readable)
                for (DWORD j = 0; j < e->wait_count && j < WAIT_HANDLES; ++j)
                    fprintf(g_file, "%s%llx", j ? ";" : "", (unsigned long long)e->handles[j]);
        }
        fputc('\n', g_file);
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
