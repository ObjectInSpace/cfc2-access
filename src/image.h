/*
 * Image inspection for CFC2 Access.
 *
 * Everything in this file exists because of one fact: the exe is packed with
 * Enigma Protector, and .text is still CIPHERTEXT when our DllMain runs. It
 * decrypts a short time later -- MEASURED at 531 ms on 2026-09-08, though an
 * earlier note in this project claimed ~10 s, so do not trust any fixed
 * figure. Any scan, hook or dump performed before
 * that point reads garbage -- and reads it *successfully*, with no error, which
 * is the dangerous part. So readiness is something we measure, never assume.
 */
#ifndef CFC2_IMAGE_H
#define CFC2_IMAGE_H

#include <windows.h>

/* A committed, readable, executable region of the main module. */
typedef struct {
    BYTE  *base;       /* live address */
    DWORD  rva;
    DWORD  size;       /* virtual size, clamped to what is actually committed */
    DWORD  protect;    /* current page protection */
    int    distinct;   /* distinct byte values in the sample window */
    BOOL   unpacked;   /* distinct <= PACKED_THRESHOLD */
    BOOL   executable; /* IMAGE_SCN_MEM_EXECUTE was set in the header */
    char   name[9];
} image_section;

#define CFC2_MAX_SECTIONS 24

/* Entropy proxy. Real x64 code repeats bytes heavily (0x48 REX prefixes, 0xCC
 * padding); ciphertext approaches all 256 values in any 4 KB window. Measured
 * on this exe: packed = 256/256, unpacked = 233/256. */
#define CFC2_PACKED_THRESHOLD 240
#define CFC2_SAMPLE_WINDOW    4096

/* Enumerate the committed, readable sections of the main module and sample
 * each one. Data sections are included as well as code: locating the GMD
 * lookup function offline depends on cross-referencing .rdata, so a code-only
 * dump would be half a picture. Returns the count written to out. */
int image_scan(image_section *out, int max);

/* Block until the main code section reads as decrypted, or until timeout.
 * Polls rather than sleeping a fixed interval, because "10 seconds" was one
 * measurement on one machine, not a contract. Returns TRUE if unpacked. */
BOOL image_wait_for_unpack(DWORD timeout_ms);

/* Write the unpacked executable sections to <gamedir>\cfc2_dump\.
 * One file per section plus a manifest describing where each came from, so the
 * offline analysis can map a file offset back to a live address. Returns the
 * number of sections written. */
int image_dump(void);

/* Live base address of the main module. */
BYTE *image_base(void);

#endif /* CFC2_IMAGE_H */
