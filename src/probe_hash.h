/*
 * Verify candidate CRC-32 routines found in the image dump by CALLING them.
 *
 * trace_crc.py ranks call sites by the shape of the surrounding bytes, and
 * disassembly makes a site readable -- but neither proves a function computes
 * the GMD label hash. Shape is a mechanism, not a diagnosis. The only decisive
 * test is to run the candidate against a label whose hash we already know from
 * the offline extraction (verified over all 4165 labels) and compare.
 *
 * This is the positive control for the whole tracing approach: if no candidate
 * reproduces a known hash, that is a real finding and we fall back to the heap
 * scan, rather than hooking something that merely looked right.
 */
#ifndef CFC2_PROBE_HASH_H
#define CFC2_PROBE_HASH_H

#include <windows.h>

/* Call every candidate address with a known label and report which, if any,
 * reproduce the GMD hashes. Logs one line per candidate. Returns the number
 * of candidates that matched. */
int probe_hash_candidates(void);

#endif /* CFC2_PROBE_HASH_H */
