"""Sabotages for the Alpha 3 alternate-copy layout (src/z3u_branch.h): each
wrong entry must fail the Alpha 3 suite that covers it. Restores the header
afterwards, also on error. Run from anywhere: python tools\sabotage_z3u_branch.py"""
import os, shutil, subprocess, sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
H = ROOT + r"\src\z3u_branch.h"
CASES = [
    ("no +1 name remap", "return id >= 0x26u ? id + 1u : id;", "return id;", "run_z3u_select_test.ps1"),
    ("alternate health at the ordinary offset", ".o_hp = 0x90u", ".o_hp = 0x88u", "run_z3u_frame_test.ps1"),
    ("alternate ID limit left at 38", ".id_limit = 42u", ".id_limit = 38u", "run_z3u_select_test.ps1"),
    ("alternate motion byte unshifted", ".o_motion = 0x5Au", ".o_motion = 0x52u", "run_z3u_frame_test.ps1"),
    ("alternate clock at the ordinary offset", ".clock = 0x129u", ".clock = 0xF1u", "run_z3u_frame_test.ps1"),
    ("alternate camera at the ordinary offset", ".camera = 0xE4EAu", ".camera = 0xC55Au", "run_z3u_frame_test.ps1"),
    ("alternate objects at the ordinary base", ".obj_off = 0x1F8u", ".obj_off = 0x1B0u", "run_z3u_frame_test.ps1"),
    ("ROM 17 not mapped", "rom == z3u_alternate.rom_us)", "rom == 99u)", "run_z3u_select_test.ps1"),
]
shutil.copy(H, H + ".bak")
bad = 0
try:
    for name, a, b, runner in CASES:
        src = open(H, encoding="utf-8").read()
        assert a in src, name
        open(H, "w", encoding="utf-8", newline="").write(src.replace(a, b, 1))
        r = subprocess.run(["pwsh", "-NoProfile", "-File", ROOT + "\\tools\\" + runner],
                           capture_output=True, text=True, cwd=ROOT)
        failed = r.returncode != 0 or "PASS" not in r.stdout
        print(("FAILS as it should" if failed else "STILL PASSES  <-- not covered"), "--", name)
        bad += not failed
        shutil.copy(H + ".bak", H)
finally:
    shutil.copy(H + ".bak", H)
sys.exit(bad)
