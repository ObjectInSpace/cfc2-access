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
    for (unsigned i = 0; i < SITE_COUNT; ++i) {
        if ((uintptr_t)source != g_sites[i].address || size != g_sites[i].size) continue;
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
    for (unsigned i = 0; i < SITE_COUNT; ++i) {
        reset();
        InterlockedExchange(&g_active, 0);
        corrupt_site = (int)i;
        assert(!collection_probe_install() && creates == 0 && !g_file);
    }
    for (unsigned i = 0; i < SITE_COUNT; ++i) {
        reset();
        InterlockedExchange(&g_active, 0);
        fail_create = (int)i;
        assert(!collection_probe_install() && !g_file);
        assert(creates == i+1 && removes == i && disables == i);
    }
    reset();
    InterlockedExchange(&g_active, 0);
    fail_enable = 1;
    assert(!collection_probe_install() && !g_file && removes == SITE_COUNT);
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

int main(void)
{
    test_nested_call_forwarding();
    test_loss_is_explicit();
    test_guard_nesting();
    test_install_refusal_and_rollback();
    test_worker_file_completion();
    test_footer_waits_for_in_flight_producer();
    return 0;
}
