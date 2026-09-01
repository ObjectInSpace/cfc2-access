"""
Test analyze_dump.py against a SYNTHETIC dump with known planted artifacts.

Why bother: analyze_dump only ever runs against a dump that costs a game launch
to produce, and a detector that silently finds nothing looks exactly like a
clean image. Planting known artifacts and requiring them to be found is the
positive control -- without it, "no hits" would be unfalsifiable.

Usage:  python tools/test_analyze_dump.py
"""

import json
import os
import struct
import subprocess
import sys
import tempfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))


def crc(data):
    return (zlib.crc32(data) & 0xFFFFFFFF) ^ 0xFFFFFFFF


def crc_table():
    """The standard reflected CRC-32 table the analyzer looks for."""
    tbl = []
    for i in range(256):
        c = i
        for _ in range(8):
            c = (c >> 1) ^ (0xEDB88320 if c & 1 else 0)
        tbl.append(c)
    return b"".join(struct.pack("<I", c) for c in tbl)


def build_dump(tmp, labels):
    """Write a fake dump containing artifacts the analyzer must find."""
    dump = os.path.join(tmp, "cfc2_dump")
    os.makedirs(dump, exist_ok=True)

    planted = {}

    # --- code section: bury two label-hash immediates in filler -------------
    # Filler is low-entropy so it reads as "code", and must not accidentally
    # contain the values we plant.
    code = bytearray(b"\x48\x8b\xcc\x90" * 4096)      # 16 KB
    label_a = sorted(labels)[0]
    label_b = sorted(labels)[len(labels) // 2]
    ha = crc(label_a.encode() * 2)
    hb = crc(label_b.encode() * 3)

    # Plant them the way a compiler actually emits a baked-in constant, as the
    # imm32 of `mov ecx, imm32` / `mov edx, imm32`. Planting bare dwords would
    # test nothing real: the analyzer deliberately requires an immediate
    # operand slot, because an unfiltered 4-byte search over 30 MB of code is
    # swamped by chance matches.
    code[0x400] = 0xB9                                 # mov ecx, imm32
    struct.pack_into("<I", code, 0x401, ha)
    code[0x800] = 0xBA                                 # mov edx, imm32
    struct.pack_into("<I", code, 0x801, hb)
    planted["hash1_label"] = label_a
    planted["hash2_label"] = label_b
    planted["hash1_addr"] = code_live_addr = 0x140001000 + 0x401

    # NEGATIVE CONTROL. A third label's hash planted as a bare dword in the
    # middle of an instruction stream, with no immediate-operand opcode before
    # it -- exactly the accident that produced 25+ bogus SOUND_MJUS_20 hits on
    # the real dump. The analyzer must NOT report it. Without this check, a
    # filter that silently stopped filtering would still pass every other test.
    label_c = sorted(labels)[len(labels) // 3]
    code[0xA00] = 0x90                                 # nop, not an opcode
    struct.pack_into("<I", code, 0xA01, crc(label_c.encode() * 2))
    planted["must_not_report"] = label_c

    # the CRC polynomial immediate
    struct.pack_into("<I", code, 0xC00, 0xEDB88320)

    code_live = 0x140001000
    with open(os.path.join(dump, "sec_00001000_code.bin"), "wb") as f:
        f.write(code)

    # --- data section: GMD magic, label table padding, CRC table -----------
    data = bytearray(b"\x00" * 0x8000)
    data[0x100:0x104] = b"GMD\x00"
    struct.pack_into("<I", data, 0x104, 0x00010302)

    # 40 label entries of 0x20 bytes with the 0xCDCDCDCD pad at +0x0C
    for i in range(40):
        base = 0x1000 + i * 0x20
        struct.pack_into("<I", data, base + 0x0C, 0xCDCDCDCD)

    tbl = crc_table()
    data[0x4000:0x4000 + len(tbl)] = tbl

    # a literal label string
    lit = sorted(labels)[1].encode() + b"\0"
    data[0x6000:0x6000 + len(lit)] = lit
    planted["literal_label"] = sorted(labels)[1]

    data_live = 0x140100000
    with open(os.path.join(dump, "sec_00100000_data.bin"), "wb") as f:
        f.write(data)

    with open(os.path.join(dump, "manifest.txt"), "w", encoding="utf-8") as f:
        f.write("# synthetic test dump\n")
        f.write("# file, rva, live_address, size, distinct_bytes, exec, protect\n")
        f.write("sec_00001000_code.bin, 0x1000, 0x%x, %d, 120, code, 0x20\n"
                % (code_live, len(code)))
        f.write("sec_00100000_data.bin, 0x100000, 0x%x, %d, 90, data, 0x04\n"
                % (data_live, len(data)))

    return dump, planted


def main():
    data_dir = os.path.join(os.path.dirname(HERE), "data")
    src = os.path.join(data_dir, "text_english.json")
    if not os.path.exists(src):
        raise SystemExit("Need %s first: python tools/extract_gmd.py" % src)
    with open(src, encoding="utf-8") as f:
        labels = json.load(f)

    failures = []
    with tempfile.TemporaryDirectory() as tmp:
        dump, planted = build_dump(tmp, labels)

        # The analyzer needs a data dir; reuse the real one.
        proc = subprocess.run(
            [sys.executable, os.path.join(HERE, "analyze_dump.py"),
             "--dump", dump, "--data", data_dir],
            capture_output=True, text=True)
        out = proc.stdout + proc.stderr

        if proc.returncode != 0:
            failures.append("analyzer exited %d" % proc.returncode)

        checks = [
            ("planted hash1 label reported", planted["hash1_label"] in out),
            ("planted hash2 label reported", planted["hash2_label"] in out),
            ("CRC table found", "CRC-32 table" in out and "VERIFIED" in out),
            ("CRC polynomial found", "poly reversed" in out),
            ("GMD v2 header found", "GMD v2 header" in out),
            ("label table found", "possible GMD label table" in out),
            ("literal label found", planted["literal_label"] in out),
            ("code section listed", "sec_00001000_code.bin" in out),
            ("live address mapped",
             ("0x%x" % planted["hash1_addr"]) in out),
            ("NEGATIVE: bare dword not reported as a hash",
             planted["must_not_report"] not in out),
        ]

        print("analyze_dump.py self-test")
        print("-" * 50)
        for name, ok in checks:
            print("  %-32s %s" % (name, "PASS" if ok else "FAIL"))
            if not ok:
                failures.append(name)

        if failures:
            print("\n--- analyzer output ---")
            print(out)

    print("-" * 50)
    if failures:
        print("%d/%d checks FAILED: %s"
              % (len(failures), len(checks), ", ".join(failures)))
        return 1
    print("all %d checks passed" % len(checks))
    return 0


if __name__ == "__main__":
    sys.exit(main())
