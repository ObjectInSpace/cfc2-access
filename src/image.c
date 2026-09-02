#include "image.h"
#include "log.h"
#include "memsafe.h"

#include <stdio.h>

BYTE *image_base(void)
{
    return (BYTE *)GetModuleHandleW(NULL);
}

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
    BYTE *base = image_base();
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

/* Build <dir of the exe>\cfc2_dump\<leaf>. */
static BOOL dump_path(char *buf, size_t cap, const char *leaf)
{
    char exe[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, exe, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return FALSE;

    char *slash = strrchr(exe, '\\');
    if (!slash)
        return FALSE;
    *slash = '\0';

    _snprintf(buf, cap, "%s\\cfc2_dump", exe);
    buf[cap - 1] = '\0';
    CreateDirectoryA(buf, NULL);

    if (leaf) {
        size_t used = strlen(buf);
        _snprintf(buf + used, cap - used, "\\%s", leaf);
        buf[cap - 1] = '\0';
    }
    return TRUE;
}

static BOOL write_file(const char *path, const void *data, DWORD len)
{
    HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        log_line("dump: cannot create %s (err %lu)", path, GetLastError());
        return FALSE;
    }
    DWORD wrote = 0;
    BOOL ok = WriteFile(h, data, len, &wrote, NULL) && wrote == len;
    CloseHandle(h);
    if (!ok)
        log_line("dump: short write to %s", path);
    return ok;
}

int image_dump(void)
{
    image_section secs[CFC2_MAX_SECTIONS];
    int n = image_scan(secs, CFC2_MAX_SECTIONS);
    if (n == 0) {
        log_line("dump: no readable executable sections -- nothing written");
        return 0;
    }

    char dir[MAX_PATH];
    if (!dump_path(dir, sizeof(dir), NULL)) {
        log_line("dump: cannot resolve output directory");
        return 0;
    }

    /* The manifest is what makes the dump usable offline. A raw .bin with no
     * record of where it came from cannot be mapped back to a live address,
     * and every address we find offline has to be applied in-process later.
     * ASLR is off and there are no relocations, so these addresses are stable
     * across every run, forever -- which is exactly why writing them down
     * pays off. */
    char manifest[4096];
    int m = 0;
    BYTE *base = image_base();
    m += _snprintf(manifest + m, sizeof(manifest) - m,
                   "# CFC2 unpacked image dump\n"
                   "# module_base=0x%llx\n"
                   "# ASLR is off and the exe has no relocations, so these\n"
                   "# addresses are valid in every future run.\n"
                   "# file, rva, live_address, size, distinct_bytes, exec, protect\n",
                   (unsigned long long)(uintptr_t)base);

    int written = 0;
    for (int i = 0; i < n; i++) {
        /* Only executable sections have to look decrypted. A data section may
         * legitimately be high-entropy (compressed or encrypted assets), and
         * refusing to dump it would discard exactly the bytes we might need
         * to identify later. Dump it and record the entropy in the manifest. */
        if (secs[i].executable && !secs[i].unpacked) {
            log_line("dump: skipping exec rva=0x%lx -- still packed (%d/256)",
                     secs[i].rva, secs[i].distinct);
            continue;
        }

        char leaf[64];
        _snprintf(leaf, sizeof(leaf), "sec_%08lx%s.bin", secs[i].rva,
                  secs[i].executable ? "_code" : "_data");
        char path[MAX_PATH];
        dump_path(path, sizeof(path), leaf);

        if (!write_file(path, secs[i].base, secs[i].size))
            continue;

        m += _snprintf(manifest + m, sizeof(manifest) - m,
                       "%s, 0x%lx, 0x%llx, %lu, %d, %s, 0x%lx\n",
                       leaf, secs[i].rva,
                       (unsigned long long)(uintptr_t)secs[i].base,
                       secs[i].size, secs[i].distinct,
                       secs[i].executable ? "code" : "data", secs[i].protect);
        log_line("dump: wrote %s (%lu bytes, rva 0x%lx, %d/256 distinct, %s)",
                 leaf, secs[i].size, secs[i].rva, secs[i].distinct,
                 secs[i].executable ? "code" : "data");
        written++;
    }

    char mpath[MAX_PATH];
    dump_path(mpath, sizeof(mpath), "manifest.txt");
    write_file(mpath, manifest, (DWORD)m);

    log_line("dump: %d/%d sections written to %s", written, n, dir);
    return written;
}
