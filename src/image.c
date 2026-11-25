#include "image.h"
#include "log.h"
#include "memsafe.h"


/* readable_run lives in memsafe.c now -- Enigma decommits its own unpacking
 * scaffolding (sections 1, 2, 4 and 5 vanish between t+0 and t+10), so a
 * section header can describe memory that is gone. Ask the memory manager,
 * not the PE. The heap scanner needs the same guard, so it is shared rather
 * than duplicated. */
#define readable_run(p, want) mem_readable_run((p), (want))

/* Count distinct byte values in a sample window -- our entropy proxy. */
static int distinct_bytes(BYTE *p, SIZE_T len)
{
    int seen[256] = {0};
    int distinct = 0;
    for (SIZE_T i = 0; i < len; i++) {
        if (!seen[p[i]]) {
            seen[p[i]] = 1;
            distinct++;
        }
    }
    return distinct;
}

int image_scan(image_section *out, int max)
{
    BYTE *base = (BYTE *)GetModuleHandleW(NULL);
    if (!base || max <= 0)
        return 0;

    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);

    int n = 0;
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections && n < max; i++) {
        if (!(sec[i].Characteristics & IMAGE_SCN_MEM_READ))
            continue;

        BYTE *p = base + sec[i].VirtualAddress;
        DWORD vsize = sec[i].Misc.VirtualSize;
        if (vsize == 0)
            vsize = sec[i].SizeOfRawData;

        SIZE_T avail = readable_run(p, vsize);
        if (avail < CFC2_SAMPLE_WINDOW)
            continue;   /* decommitted or too small to judge -- skip silently */

        MEMORY_BASIC_INFORMATION mbi;
        VirtualQuery(p, &mbi, sizeof(mbi));

        image_section *s = &out[n++];
        s->base       = p;
        s->rva        = sec[i].VirtualAddress;
        s->size       = (DWORD)avail;
        s->protect    = mbi.Protect;
        s->distinct   = distinct_bytes(p, CFC2_SAMPLE_WINDOW);
        s->unpacked   = s->distinct <= CFC2_PACKED_THRESHOLD;
        s->executable =
            (sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
        /* Section names are blanked by Enigma, but copy whatever is there. */
        memcpy(s->name, sec[i].Name, 8);
        s->name[8] = '\0';
    }
    return n;
}

BOOL image_wait_for_unpack(DWORD timeout_ms)
{
    DWORD start = GetTickCount();
    int report = 0;

    for (;;) {
        image_section secs[CFC2_MAX_SECTIONS];
        int n = image_scan(secs, CFC2_MAX_SECTIONS);

        /* The main code section is the first executable one. When it reads as
         * real code AND has been locked back down to RX, Enigma has finished
         * with it. Protection alone is not enough (it is RX before unpacking
         * starts too), and entropy alone can race the final flip -- so require
         * both. */
        for (int i = 0; i < n; i++) {
            if (!secs[i].executable)
                continue;   /* a low-entropy .rdata says nothing about .text */
            BOOL rx = (secs[i].protect & (PAGE_EXECUTE_READ | PAGE_EXECUTE)) != 0;
            if (secs[i].unpacked && rx) {
                log_line("image: unpacked after %lu ms -- section rva=0x%lx "
                         "distinct=%d/256 prot=0x%lx",
                         GetTickCount() - start, secs[i].rva,
                         secs[i].distinct, secs[i].protect);
                return TRUE;
            }
        }

        if (GetTickCount() - start > timeout_ms) {
            log_line("image: STILL PACKED after %lu ms (%d exec sections) -- "
                     "giving up; do NOT trust any scan from here",
                     timeout_ms, n);
            for (int i = 0; i < n; i++)
                log_line("image:   rva=0x%lx distinct=%d/256 prot=0x%lx",
                         secs[i].rva, secs[i].distinct, secs[i].protect);
            return FALSE;
        }

        /* Progress reporting: a silent 10 s wait is indistinguishable from a
         * hang, and this runs before anything else can report health. */
        if (++report % 10 == 0)
            log_line("image: waiting for unpack... %lu ms elapsed",
                     GetTickCount() - start);
        Sleep(250);
    }
}
