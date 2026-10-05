#ifndef CFC2_PWS2_DRAW_READER_H
#define CFC2_PWS2_DRAW_READER_H
#include <windows.h>
#include <stdint.h>

/* Unwired v1 diagnostic component. Caller supplies independently admitted
 * identity and an invocation deadline; this API does not grant admission. */
enum pws2_read_error { PWS2_READ_OK, PWS2_READ_RANGE, PWS2_READ_BUDGET,
    PWS2_READ_DEADLINE, PWS2_READ_CONTENTION, PWS2_READ_MEMORY,
    PWS2_READ_STAGE, PWS2_READ_POOL, PWS2_READ_ROOT, PWS2_READ_COUNT,
    PWS2_READ_ASSOCIATION };
enum pws2_read_issue { PWS2_OBJECT=1, PWS2_OWNER=2, PWS2_OBJECT_DISABLED=4,
    PWS2_RESOURCE_DISABLED=8, PWS2_TYPE=16, PWS2_PACKET=32,
    PWS2_SLOT=64, PWS2_NONFINITE=128, PWS2_IDENTITY=256 };
typedef struct {
    uint64_t root, object, resource, matrix, caller, invocation, revision;
    int64_t deadline_qpc;
} pws2_read_request;
typedef struct {
    pws2_read_request request;
    uint32_t error, issues, attempts, bytes, queries, valid_mask;
    int32_t ordinal;
    unsigned char root_before[8], context_before[8], object[224], association[64];
    unsigned char resource[168], packet[24], slots[264], globals[128];
    unsigned char root_after[8], context_after[8];
} pws2_draw_record;
_Static_assert(sizeof(pws2_draw_record) <= 1280, "v1 event storage budget");
/* Transport returns exact copied length, or zero, with explicit error.
 * Query count is charged even on failure. Never publish partial copy bytes. */
typedef SIZE_T (*pws2_read_fn)(void *, uint64_t, void *, SIZE_T,
                              unsigned *, enum pws2_read_error *);
typedef int64_t (*pws2_clock_fn)(void *);
void pws2_draw_read(pws2_draw_record *, const pws2_read_request *,
                   pws2_read_fn, pws2_clock_fn, void *);
/* Call once on the worker before use, with mem_guard_install already passed.
 * No guard installation/removal or logging takes place on the draw thread. */
BOOL pws2_draw_reader_page_size_ok(void);
SIZE_T pws2_draw_native_copy(void *, uint64_t, void *, SIZE_T,
                            unsigned *, enum pws2_read_error *);
int64_t pws2_draw_qpc(void *);
#endif
