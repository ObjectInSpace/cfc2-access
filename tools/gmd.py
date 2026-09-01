"""
MT Framework ARC (v7) and GMD (v2) readers for Capcom Fighting Collection 2.

Why this exists rather than using an off-the-shelf tool: CFC2's GMD files use a
0x20-byte, 64-bit label entry with an int64 bucket table. Every public GMD tool
(GMDConverter, gmdDump, Cirilla) assumes the 0x14-byte/int32 desktop variant and
misparses these files -- silently, because the header is identical.

Both layouts are supported here and detected by whether the computed file size
matches the actual one, which is the only reliable discriminator.

Verified against the shipped data: all 14 languages parse, and all 4165 label
hashes in each check out against ~crc32(label*2) / ~crc32(label*3).
"""

import struct
import zlib
from collections import namedtuple

ARC_MAGIC = b"ARC\0"
GMD_MAGIC = b"GMD\0"
GMD_V2 = 0x00010302

# MT Framework resource type hashes seen in CFC2.
TYPE_TEX = 0x241F5DEB
TYPE_GMD = 0x242BB29A
TYPE_GFD = 0x2D462600
TYPE_GUI = 0x22948394
TYPE_ROM = 0x21D3D8A7  # the IBIS emulator blob

ArcEntry = namedtuple("ArcEntry", "name type_hash comp_size size offset")


def _crc(data):
    """The GMD label hash: an un-finalized reflected CRC-32."""
    return (zlib.crc32(data) & 0xFFFFFFFF) ^ 0xFFFFFFFF


def read_arc(path):
    """Parse an ARC v7 archive header. Returns (raw_bytes, [ArcEntry])."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != ARC_MAGIC:
        raise ValueError("%s: not an ARC archive" % path)

    version, count = struct.unpack_from("<HH", data, 4)
    if version != 7:
        raise ValueError("%s: unsupported ARC version %d" % (path, version))

    entries = []
    off = 8
    for _ in range(count):
        name = data[off:off + 0x40].split(b"\0")[0].decode("ascii", "replace")
        type_hash, comp, size_flags, foff = struct.unpack_from("<IIII", data, off + 0x40)
        # The top 3 bits are quality flags, not size.
        entries.append(ArcEntry(name, type_hash, comp, size_flags & 0x1FFFFFFF, foff))
        off += 0x50
    return data, entries


def extract(data, entry):
    """Inflate one ARC entry."""
    return zlib.decompress(data[entry.offset:entry.offset + entry.comp_size])


def parse_gmd(raw):
    """
    Parse a GMD v2 message file. Returns (name, {label: string}).

    Labels are returned in file order; Python dicts preserve insertion order,
    which keeps the label list stable for diffing between languages.
    """
    if raw[:4] != GMD_MAGIC:
        raise ValueError("not a GMD file")
    version, language = struct.unpack_from("<II", raw, 4)
    if version != GMD_V2:
        raise ValueError("unsupported GMD version 0x%08x" % version)

    label_count, section_count, label_size, section_size, name_size = \
        struct.unpack_from("<IIIII", raw, 0x14)
    name = raw[0x28:0x28 + name_size].decode("ascii", "replace")
    base = 0x28 + name_size + 1

    # Detect the entry layout by which one accounts for the file exactly.
    # 0x20/int64 is what CFC2 ships; 0x14/int32 is the common desktop form.
    for entry_size, bucket_width in ((0x20, 8), (0x14, 4)):
        expected = (base + label_count * entry_size
                    + (0x100 * bucket_width if label_count else 0)
                    + label_size + section_size)
        if expected == len(raw):
            break
    else:
        raise ValueError("GMD size mismatch: no known label layout fits "
                         "(%d bytes, %d labels)" % (len(raw), label_count))

    labels_at = base + label_count * entry_size + (0x100 * bucket_width if label_count else 0)
    label_blob = raw[labels_at:labels_at + label_size]
    strings = raw[len(raw) - section_size:].split(b"\0")

    out = {}
    for i in range(label_count):
        if entry_size == 0x20:
            # sectionId, hash1, hash2, padding, labelOffset(64), listLink(64)
            sid, _h1, _h2, _pad, loff, _link = struct.unpack_from("<iIIIqq", raw, base + i * 0x20)
        else:
            sid, _h1, _h2, loff, _link = struct.unpack_from("<iIIii", raw, base + i * 0x14)
        end = label_blob.index(b"\0", loff)
        label = label_blob[loff:end].decode("ascii", "replace")
        out[label] = strings[sid].decode("utf-8", "replace") if 0 <= sid < len(strings) else ""

    return name, language, out


def verify_gmd(raw):
    """
    Re-derive every label hash and confirm it matches what the file stores.

    This is the integrity check that proves the parse is correct rather than
    merely plausible -- a wrong entry stride still yields readable-looking
    strings, so string sanity alone is not evidence. Returns (checked, failed).
    """
    label_count, _sc, label_size, section_size, name_size = \
        struct.unpack_from("<IIIII", raw, 0x14)
    base = 0x28 + name_size + 1
    for entry_size, bucket_width in ((0x20, 8), (0x14, 4)):
        expected = (base + label_count * entry_size
                    + (0x100 * bucket_width if label_count else 0)
                    + label_size + section_size)
        if expected == len(raw):
            break
    else:
        return 0, label_count

    labels_at = base + label_count * entry_size + (0x100 * bucket_width if label_count else 0)
    label_blob = raw[labels_at:labels_at + label_size]

    checked = failed = 0
    for i in range(label_count):
        if entry_size == 0x20:
            _sid, h1, h2, _pad, loff, _link = struct.unpack_from("<iIIIqq", raw, base + i * 0x20)
        else:
            _sid, h1, h2, loff, _link = struct.unpack_from("<iIIii", raw, base + i * 0x14)
        end = label_blob.index(b"\0", loff)
        name = label_blob[loff:end]
        checked += 1
        if _crc(name * 2) != h1 or _crc(name * 3) != h2:
            failed += 1
    return checked, failed


def iter_gmds(arc_path):
    """Yield (entry_name, raw_bytes) for every GMD in an ARC."""
    data, entries = read_arc(arc_path)
    for e in entries:
        if e.type_hash == TYPE_GMD:
            yield e.name, extract(data, e)
