/* Synthetic ABI checks only. Does not patch or read a game process. */
#include "../src/pws2_draw_entry.h"
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <xmmintrin.h>

unsigned char pws2_test_xmm[96], pws2_test_actual_xmm[96];
uint64_t pws2_test_gpr[9], pws2_test_flags, pws2_test_return;
unsigned pws2_test_actual_mxcsr, pws2_test_original_calls;
unsigned short pws2_test_actual_x87_control;
double pws2_test_x87_input = 3.25, pws2_test_x87_output;
static unsigned observations;
static BOOL nested;
static const void *seen_matrix, *seen_object, *seen_resource;
static uintptr_t seen_caller;
extern const unsigned char pws2_test_after_call[];
extern uint64_t pws2_test_call(void *, void *, void *);
extern void pws2_test_original(void);

static void sink(const void *matrix, const void *object,
                  const void *resource, uintptr_t caller)
{
    ++observations;
    seen_matrix = matrix;
    seen_object = object;
    seen_resource = resource;
    seen_caller = caller;
    if (nested) pws2_draw_entry(); /* Unrelated nested call, still forwarded. */
    SetLastError(999);
    _mm_setcsr(_mm_getcsr() ^ 0x6000); /* Observer changes rounding mode. */
    __asm__ volatile(
        "pxor %%xmm0, %%xmm0\n\tpxor %%xmm1, %%xmm1\n\t"
        "pxor %%xmm2, %%xmm2\n\tpxor %%xmm3, %%xmm3\n\t"
        "pxor %%xmm4, %%xmm4\n\tpxor %%xmm5, %%xmm5\n\t"
        "movq $-1, %%rax\n\tmovq $-1, %%rcx\n\tmovq $-1, %%rdx\n\t"
        "movq $-1, %%r8\n\tmovq $-1, %%r9\n\tmovq $-1, %%r10\n\t"
        "movq $-1, %%r11\n\tclc\n\tfld1"
        : : : "rax", "rcx", "rdx", "r8", "r9", "r10", "r11",
                "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "cc");
}

static void exercise(BOOL enabled)
{
    unsigned char object[1], resource[1], matrix[64];
    const uint64_t expected[] = {0x11, (uintptr_t)matrix, 0x33, 0x44,
        0x55, 0x66, 0x77, (uintptr_t)resource, (uintptr_t)object};
    unsigned mxcsr = _mm_getcsr(), old_observations = observations;
    unsigned short x87_control;
    __asm__ volatile("fnstcw %0" : "=m"(x87_control));
    unsigned old_calls = pws2_test_original_calls;
    pws2_draw_entry_sink = enabled ? sink : NULL;
    SetLastError(321);
    assert(pws2_test_call(object, resource, matrix) == 0x1234);
    assert(GetLastError() == 321);
    assert(pws2_test_original_calls == old_calls + 1 + (enabled && nested ? 1 : 0));
    assert(observations == old_observations + (enabled ? 1 : 0));
    assert(!memcmp(expected, pws2_test_gpr, sizeof expected));
    assert(!memcmp(pws2_test_xmm, pws2_test_actual_xmm, sizeof pws2_test_xmm));
    assert((pws2_test_flags & 0x8c5) == 0x45); /* CF, PF, ZF; no OF/SF. XOR leaves AF undefined. */
    assert(pws2_test_actual_mxcsr == mxcsr);
    assert(pws2_test_actual_x87_control == x87_control);
    assert(pws2_test_x87_output == pws2_test_x87_input);
    assert(pws2_test_return == (uintptr_t)pws2_test_after_call);
    if (enabled) {
        assert(seen_matrix == matrix && seen_object == object && seen_resource == resource);
        assert(seen_caller == (uintptr_t)pws2_test_after_call);
    }
}

int main(void)
{
    pws2_draw_entry_original = (void *)pws2_test_original;
    for (unsigned i = 0; i < sizeof pws2_test_xmm; ++i)
        pws2_test_xmm[i] = (unsigned char)(i + 1);
    exercise(TRUE);
    exercise(FALSE);
    nested = TRUE;
    exercise(TRUE);
    nested = FALSE;
    /* An unrelated C caller is forwarded but must never publish. */
    pws2_draw_entry_sink = sink;
    unsigned old_observations = observations, old_calls = pws2_test_original_calls;
    pws2_draw_entry();
    assert(observations == old_observations);
    assert(pws2_test_original_calls == old_calls + 1);
    puts("PWS2 entry ABI: active, inactive, nested and unrelated-caller controls passed");
    return 0;
}
