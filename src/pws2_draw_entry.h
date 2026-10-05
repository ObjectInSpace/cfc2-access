#ifndef CFC2_PWS2_DRAW_ENTRY_H
#define CFC2_PWS2_DRAW_ENTRY_H
#include <stdint.h>

/* Unwired diagnostic ABI prototype. No installer, native reads or audio.
 * Configure original/sink before enabling a future hook; do not replace them
 * concurrently with execution. The sink receives opaque native addresses and
 * must implement its own bounded guarded reads and lifecycle checks. */
typedef void (*pws2_draw_entry_sink_fn)(const void *matrix, const void *object,
                                       const void *resource, uintptr_t caller);
extern void *pws2_draw_entry_original;
extern pws2_draw_entry_sink_fn pws2_draw_entry_sink;

/* Assembly entry: preserve incoming machine state, observe, tail-forward.
 * This is NOT a C function with object/resource arguments. At the reviewed
 * call site those identities reside in RDI/RBX, not RDX/R8. */
void pws2_draw_entry(void);
#endif
