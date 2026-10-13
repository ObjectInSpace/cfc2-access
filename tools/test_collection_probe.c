/* Synthetic calls only: no game process, code patches or native execution. */
#define mem_copy probe_test_mem_copy
#include "../src/collection_probe.c"
#undef mem_copy
#include "../build/test/collection_probe_fixtures.inc"
#include <assert.h>

static int corrupt_site = -1, fail_create = -1, fail_enable;
static unsigned creates, removes, disables, calls;
static void *last_argument;
static int last_length;
static HANDLE reader_entered, reader_release;

BOOL probe_test_mem_copy(void *destination, const void *source, SIZE_T size)
{
    for (unsigned i = 0; i < SITE_COUNT + sizeof g_wait_guards / sizeof g_wait_guards[0]; ++i) {
        const probe_site *site = i < SITE_COUNT ? &g_sites[i] : &g_wait_guards[i-SITE_COUNT];
        if ((uintptr_t)source != site->address || size != site->size) continue;
        memcpy(destination, fixtures[i], size);
        if ((int)i == corrupt_site) ((unsigned char *)destination)[size-1] ^= 1;
        return TRUE;
    }
    return FALSE;
}

void log_line(const char *format, ...) { (void)format; }
MH_STATUS WINAPI MH_Initialize(void) { return MH_OK; }
MH_STATUS WINAPI MH_CreateHook(LPVOID address, LPVOID detour, LPVOID *original)
{
    (void)address; (void)detour; (void)original;
    unsigned index = creates++;
    return (int)index == fail_create ? MH_ERROR_MEMORY_ALLOC : MH_OK;
}
MH_STATUS WINAPI MH_QueueEnableHook(LPVOID address) { (void)address; return MH_OK; }
MH_STATUS WINAPI MH_ApplyQueued(void) { return fail_enable ? MH_ERROR_MEMORY_PROTECT : MH_OK; }
MH_STATUS WINAPI MH_DisableHook(LPVOID address) { (void)address; ++disables; return MH_OK; }
MH_STATUS WINAPI MH_RemoveHook(LPVOID address) { (void)address; ++removes; return MH_OK; }

static void fake_reader(probe_event *event)
{
    event->readable = 1;
    event->session = 0x100000;
    event->root = 0x200000;
    event->game = 1;
    event->rom = 2;
    SetLastError(654);
}

static void reset(void)
{
    assert(!g_file);
    assert(!g_in_flight);
    InterlockedExchange(&g_active, 1);
    InterlockedExchange(&g_dropped, 0);
    g_head = g_count = g_depth = 0;
    g_sequence = 0;
    g_deadline_ticks = UINT64_MAX;
    g_attempted = FALSE;
    g_lifetime = FALSE;
    g_native_sites = LEGACY_SITES;
    g_call_id = g_application_id = 0;
    g_resolve_wait_targets = resolve_wait_targets;
    creates = removes = disables = calls = 0;
    fail_create = corrupt_site = -1;
    fail_enable = 0;
    g_read_state = fake_reader;
}

static unsigned char original_load(void *source, int length)
{
    last_argument = source;
    last_length = length;
    ++calls;
    SetLastError(777);
    return 0xa5; /* Preserve the entire observed byte, not a BOOL conversion. */
}

static void original_update(void *object)
{
    assert(object == (void *)(uintptr_t)0x12345678);
    assert(GetLastError() == 123);
    assert(load_detour((void *)(uintptr_t)0xabcdef, -17) == 0xa5);
    assert(GetLastError() == 777);
}

static void original_one(void *object) { last_argument = object; ++calls; }

static void test_nested_call_forwarding(void)
{
    probe_event events[PROBE_BATCH];
    reset();
    g_real_load = original_load;
    g_real_update = original_update;
    SetLastError(123);
    update_detour((void *)(uintptr_t)0x12345678);
    assert(GetLastError() == 777);
    assert(calls == 1 && last_argument == (void *)(uintptr_t)0xabcdef && last_length == -17);
    assert(g_depth == 0 && take_events(events) == 4);
    assert(events[0].site == SITE_UPDATE && !events[0].exiting);
    assert(events[1].site == SITE_LOAD && !events[1].exiting && events[1].argument_length == -17);
    assert(events[2].site == SITE_LOAD && events[2].exiting && events[2].result == 0xa5);
    assert(events[3].site == SITE_UPDATE && events[3].exiting);
    for (unsigned i = 0; i < 4; ++i) {
        assert(events[i].sequence == i+1 && events[i].depth == 1);
        assert(events[i].thread == GetCurrentThreadId() && events[i].root == 0x200000);
    }
    g_real_register = g_real_cleanup = g_real_release = original_one;
    register_detour((void *)1); cleanup_detour((void *)2); release_detour((void *)3);
    assert(calls == 4 && last_argument == (void *)3 && take_events(events) == 6);
    InterlockedExchange(&g_active, 0);
    cleanup_detour((void *)4);
    assert(calls == 5 && last_argument == (void *)4 && take_events(events) == 0);
    puts("ok: nested original calls, signed argument, byte result and passive forwarding");
}

static int original_quick_load(void *owner)
{
    assert(GetLastError() == 123);
    last_argument = owner;
    ++calls;
    SetLastError(777);
    return -1234567; /* Detect truncation to the snapshot callback's AL. */
}

static void test_quick_load_forwarding(void)
{
    probe_event events[PROBE_BATCH];
    reset();
    g_real_quick_load = original_quick_load;
    SetLastError(123);
    assert(quick_load_detour((void *)0x1234) == -1234567);
    assert(GetLastError() == 777 && calls == 1 && last_argument == (void *)0x1234);
    assert(take_events(events) == 2);
    assert(events[0].site == SITE_QUICK_LOAD && !events[0].exiting);
    assert(events[1].site == SITE_QUICK_LOAD && events[1].exiting);
    assert(events[1].result == -1234567 && events[1].argument == 0x1234);
    InterlockedExchange(&g_active, 0);
    SetLastError(123);
    assert(quick_load_detour((void *)0x5678) == -1234567);
    assert(GetLastError() == 777 && calls == 2 && last_argument == (void *)0x5678);
    assert(take_events(events) == 0);
    puts("ok: Quick Load argument, signed 32-bit result, last-error and passive forwarding");
}

static void test_loss_is_explicit(void)
{
    probe_event events[PROBE_BATCH];
    reset();
    AcquireSRWLockExclusive(&g_lock);
    record_event(SITE_UPDATE, 0, NULL, 0, -1);
    ReleaseSRWLockExclusive(&g_lock);
    assert(g_dropped == 1 && g_count == 0);
    for (unsigned i = 0; i < PROBE_CAP+1; ++i) record_event(SITE_UPDATE, 0, NULL, 0, -1);
    assert(g_dropped == 2 && g_count == PROBE_CAP);
    assert(take_events(events) == PROBE_BATCH && events[0].sequence == 1);
    record_event(SITE_CLEANUP, 1, NULL, 0, -1);
    assert(g_count == PROBE_CAP-PROBE_BATCH+1);
    assert(g_ring[(g_head+g_count-1)%PROBE_CAP].sequence == PROBE_CAP+1);
    g_deadline_ticks = 0;
    record_event(SITE_UPDATE, 1, NULL, 0, -1);
    assert(!g_active && g_count == PROBE_CAP-PROBE_BATCH+1);
    puts("ok: contention, overflow, wraparound and deadline remain observable");
}

static void test_guard_nesting(void)
{
    probe_event event = {0};
    reset();
    g_mem_guard_armed = 1;
    read_state(&event);
    assert(!event.readable && g_mem_guard_armed == 1);
    g_mem_guard_armed = 0;
    puts("ok: an existing TLS fault guard is never replaced");
}

static void test_install_refusal_and_rollback(void)
{
    for (unsigned i = 0; i < LEGACY_SITES; ++i) {
        reset();
        InterlockedExchange(&g_active, 0);
        corrupt_site = (int)i;
        assert(!collection_probe_install() && creates == 0 && !g_file);
    }
    for (unsigned i = 0; i < LEGACY_SITES; ++i) {
        reset();
        InterlockedExchange(&g_active, 0);
        fail_create = (int)i;
        assert(!collection_probe_install() && !g_file);
        assert(creates == i+1 && removes == i && disables == i);
    }
    reset();
    InterlockedExchange(&g_active, 0);
    fail_enable = 1;
    assert(!collection_probe_install() && !g_file && removes == LEGACY_SITES);
    puts("ok: whole-body changes refuse hooks; partial install rolls back");
}

static void test_worker_file_completion(void)
{
    reset();
    InterlockedExchange(&g_active, 0);
    assert(collection_probe_install() && g_file && g_active);
    record_event(SITE_UPDATE, 0, NULL, 0, -1);
    record_event(SITE_UPDATE, 1, NULL, 0, -1);
    g_started = GetTickCount64()-120001;
    g_last_drain = g_started;
    collection_probe_tick();
    assert(!g_active && g_count == 0 && g_file);
    g_last_drain = GetTickCount64()-251;
    collection_probe_tick();
    assert(!g_file);
    puts("ok: worker drains records and completes the bounded capture");
}

static void delayed_reader(probe_event *event)
{
    (void)event;
    SetEvent(reader_entered);
    assert(WaitForSingleObject(reader_release, 5000) == WAIT_OBJECT_0);
}

static DWORD WINAPI delayed_producer(void *unused)
{
    (void)unused;
    record_event(SITE_LOAD, 0, NULL, 0, -1);
    return 0;
}

static void test_footer_waits_for_in_flight_producer(void)
{
    reset();
    InterlockedExchange(&g_active, 0);
    assert(collection_probe_install());
    reader_entered = CreateEventW(NULL, TRUE, FALSE, NULL);
    reader_release = CreateEventW(NULL, TRUE, FALSE, NULL);
    assert(reader_entered && reader_release);
    g_read_state = delayed_reader;
    HANDLE thread = CreateThread(NULL, 0, delayed_producer, NULL, 0, NULL);
    assert(thread && WaitForSingleObject(reader_entered, 5000) == WAIT_OBJECT_0);
    g_started = GetTickCount64()-120001;
    g_last_drain = g_started;
    collection_probe_tick();
    assert(!g_active && g_in_flight == 1 && g_file);
    SetEvent(reader_release);
    assert(WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0);
    g_last_drain = GetTickCount64()-251;
    collection_probe_tick();
    assert(!g_in_flight && !g_file && !g_count);
    CloseHandle(thread);
    CloseHandle(reader_entered);
    CloseHandle(reader_release);
    puts("ok: completion waits for a producer already inside the capture window");
}

static DWORD wait_result;
static const HANDLE *wait_array;
static DWORD WINAPI fake_single(HANDLE handle, DWORD timeout)
{
    assert(handle == (HANDLE)0x456 && timeout == INFINITE);
    assert(GetLastError() == 123);
    ++calls;
    SetLastError(ERROR_INVALID_HANDLE);
    return wait_result;
}

static DWORD WINAPI fake_multiple(DWORD count, const HANDLE *handles, BOOL all, DWORD timeout)
{
    assert(count == 2 && handles == wait_array && all == TRUE && timeout == INFINITE);
    assert(GetLastError() == 123);
    ++calls;
    /* Prove the diagnostic keeps the entry copy, not a post-call reread. */
    ((HANDLE *)handles)[0] = (HANDLE)0x999;
    SetLastError(ERROR_INVALID_HANDLE);
    return wait_result;
}

static DWORD WINAPI first_wait_on_thread(void *unused)
{
    (void)unused;
    SetLastError(123);
    DWORD result = observe_single(0x1401f847f, (HANDLE)0x456, INFINITE);
    return result == wait_result && GetLastError() == ERROR_INVALID_HANDLE ? 0 : 1;
}

static void test_wait_forwarding_and_correlation(void)
{
    probe_event events[PROBE_BATCH];
    const DWORD results[] = {WAIT_OBJECT_0, WAIT_FAILED, WAIT_TIMEOUT, WAIT_ABANDONED, 0x87654321};
    reset();
    g_lifetime = TRUE;
    g_real_single = fake_single;
    g_real_multiple = fake_multiple;
    g_application_id = 77;
    for (unsigned i = 0; i < sizeof results / sizeof results[0]; ++i) {
        wait_result = results[i];
        SetLastError(123);
        assert(observe_single(0x1401f7da2, (HANDLE)0x456, INFINITE) == wait_result);
        assert(GetLastError() == ERROR_INVALID_HANDLE);
        assert(take_events(events) == 2);
        assert(events[0].invocation == events[1].invocation && events[0].invocation);
        assert(events[1].application_invocation == 77 && (DWORD)events[1].result == wait_result);
        assert(events[1].handles[0] == 0x456 && events[1].last_error == ERROR_INVALID_HANDLE);
        HANDLE handles[] = {(HANDLE)0x111, (HANDLE)0x222};
        wait_array = handles;
        SetLastError(123);
        assert(observe_multiple(0x1401f80ad, 2, handles, TRUE, INFINITE) == wait_result);
        assert(GetLastError() == ERROR_INVALID_HANDLE && take_events(events) == 2);
        assert(events[0].handles_readable && events[1].handles_readable);
        assert(events[1].handles[0] == 0x111 && events[1].handles[1] == 0x222);
        assert(events[1].wait_count == 2 && events[1].wait_all == TRUE);
    }
    SetLastError(123);
    assert(observe_single(0x1234, (HANDLE)0x456, INFINITE) == wait_result);
    assert(GetLastError() == ERROR_INVALID_HANDLE && take_events(events) == 0);
    HANDLE first = CreateThread(NULL, 0, first_wait_on_thread, NULL, 0, NULL);
    DWORD code = 1;
    assert(first && WaitForSingleObject(first, 5000) == WAIT_OBJECT_0);
    assert(GetExitCodeThread(first, &code) && code == 0);
    CloseHandle(first);
    assert(take_events(events) == 2 && events[0].invocation == 1);
    assert(events[0].application_invocation == 0);
    probe_event extra = {0};
    extra.wait_count = 65;
    copy_wait_handles(&extra, (HANDLE *)1);
    assert(!extra.handles_readable);
    extra.wait_count = 2;
    g_mem_guard_armed = 1;
    copy_wait_handles(&extra, (HANDLE *)1);
    assert(!extra.handles_readable && g_mem_guard_armed);
    g_mem_guard_armed = 0;
    assert(mem_guard_install());
    copy_wait_handles(&extra, (HANDLE *)1);
    assert(!extra.handles_readable && !g_mem_guard_armed);
    puts("ok: raw wait results, last-error, exact arguments, copied handles and caller filtering");
}

static void nested_application(void *object)
{
    assert(GetLastError() == 123 && g_application_id);
    observe_one(SITE_TRAINING, object, original_one, 0x1400391f6);
    SetLastError(777);
}

static BOOL fake_wait_targets(void)
{
    g_targets[SITE_COUNT] = (void *)fake_single;
    g_targets[SITE_COUNT+1] = (void *)fake_multiple;
    return TRUE;
}

static void test_lifetime_install_and_nesting(void)
{
    probe_event events[PROBE_BATCH];
    reset();
    SetLastError(123);
    observe_one(SITE_APPLICATION, (void *)123, nested_application, 0x14020bd7a);
    assert(GetLastError() == 777 && !g_application_id && take_events(events) == 4);
    assert(events[0].invocation == events[3].invocation);
    assert(events[1].invocation == events[2].invocation);
    assert(events[0].invocation != events[1].invocation);
    for (unsigned i = 0; i < 4; ++i) assert(events[i].application_invocation == events[0].invocation);
    for (unsigned i = 0; i < SITE_COUNT + sizeof g_wait_guards / sizeof g_wait_guards[0]; ++i) {
        reset();
        InterlockedExchange(&g_active, 0);
        corrupt_site = (int)i;
        assert(!collection_probe_install_lifetime() && !creates && !g_file);
    }
    for (unsigned i = 0; i < HOOK_COUNT; ++i) {
        reset();
        InterlockedExchange(&g_active, 0);
        g_resolve_wait_targets = fake_wait_targets;
        fail_create = (int)i;
        assert(!collection_probe_install_lifetime() && !g_file);
        assert(creates == i+1 && removes == i);
    }
    reset();
    InterlockedExchange(&g_active, 0);
    g_resolve_wait_targets = fake_wait_targets;
    assert(collection_probe_install_lifetime() && creates == HOOK_COUNT);
    SetLastError(123);
    observe_one(SITE_APPLICATION, (void *)123, nested_application, 0x14020bd7a);
    g_real_single = fake_single;
    wait_result = WAIT_FAILED;
    SetLastError(123);
    observe_single(0x1401f81b9, (HANDLE)0x456, INFINITE);
    g_started = GetTickCount64()-120001;
    g_last_drain = g_started;
    collection_probe_tick();
    g_last_drain = GetTickCount64()-251;
    collection_probe_tick();
    assert(!g_file);
    puts("ok: lifetime IDs, enclosing application, all guards and partial-hook rollback");
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    test_nested_call_forwarding();
    test_quick_load_forwarding();
    test_loss_is_explicit();
    test_guard_nesting();
    test_install_refusal_and_rollback();
    test_worker_file_completion();
    test_footer_waits_for_in_flight_producer();
    test_wait_forwarding_and_correlation();
    test_lifetime_install_and_nesting();
    return 0;
}
