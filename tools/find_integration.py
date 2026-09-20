"""Find the fighter position field by its DEFINING BEHAVIOUR, not its shape.

THE RULE THIS OBEYS (CLAUDE.md, 4-for-4 vs 0-for-4): read the answer out of
the instructions. Every position candidate this project derived from capture
diffs has failed -- B+0x246 (health), B+0x0D2/0x0D8 (the pointer artifact).

WHAT SEPARATES POSITION FROM EVERY LOOK-ALIKE
---------------------------------------------
A coordinate is INTEGRATED: each frame it accumulates a velocity that is
itself a variable held in the same struct.

    mov  eax, [rbx + VEL]        ; a variable, not an immediate
    add  [rbx + POS], eax        ; accumulate into the position

That is a read-modify-write where the ADDEND IS MEMORY FROM THE SAME BASE
REGISTER. It is exactly what a timer is NOT (timers do `sub ..., 1` -- an
immediate), and what an animation id copy is NOT (a `mov` from a pointed-to
record). The trace already used this distinction to reject timers.

So: enumerate every add/sub into [reg+disp] within the player struct whose
SOURCE operand is a register recently loaded from the SAME base register.
This is a structural query over instructions, not a value filter.

THE POSITIVE CONTROL -- it runs automatically and EXITS NON-ZERO on failure.

The known answer is the motion routine at 0x140708F50, which integrates
B+0x54 from B+0xA8 and B+0x58 from B+0xAC (docs/cvs2-state-access-trace.md).
If this scan stops finding that pair, the tool is broken and nothing it says
about a new field can be believed.

WARNING: This project has shipped a tool that printed a warning while every
assertion stayed green. A control that only prints is not a control -- if the
code detects a problem, something must FAIL. Hence the exit code.

Usage: find_integration.py [--window N]
"""
import sys, os
from collections import defaultdict

sys.path.insert(0, r"C:\Users\amock\CFC2 Access\tools")
from analyze_dump import read_manifest, DEFAULT_DUMP
from capstone import Cs, CS_ARCH_X86, CS_MODE_64, x86

LO, HI = 0x1406A0000, 0x140740000
STRUCT_MAX = 0x678          # the player struct size, from the trace
WINDOW = 8                  # instructions to look back for the velocity load

secs = read_manifest(DEFAULT_DUMP)
code = [s for s in secs if s.kind == "code" and s.live <= LO < s.live + s.size][0]
md = Cs(CS_ARCH_X86, CS_MODE_64)
md.detail = True

data = code.data
base = code.live


def root_reg(ins, reg):
    """Canonical name for a register, ignoring width.

    `edx` and `rdx` are the same storage: a write to one clobbers the other.
    Tracking them separately would let a stale 64-bit entry survive a 32-bit
    overwrite, which is the same class of bug as the width misreading that
    produced the refuted B+0x0D8 'pointer as coordinate' result.
    """
    n = ins.reg_name(reg) or ""
    if n.startswith("r") and n[1:].isdigit():          # r8..r15
        return n
    if n.startswith(("r", "e")) and len(n) == 3:       # rax/eax -> ax
        return n[1:]
    if len(n) == 2 and n[1] in "lh":                   # al/ah -> ax
        return n[0] + "x"
    return n

# Anchor windows at every add/sub byte-pattern candidate rather than sweeping:
# decode short windows starting at many offsets and keep only what confirms.
# To keep it honest we decode from each offset in the range but NEVER treat a
# failed decode as coverage -- we count how much decoded.
results = defaultdict(list)
decoded = 0
attempted = 0

start_off = LO - base
end_off = HI - base

# Walk in short anchored windows. Each window restarts the decoder, so one
# undecodable byte costs only that window, not the rest of the range.
#
# WARNING: WINDOWS MUST OVERLAP, and the positive control is what caught this.
# The velocity load and the integration that uses it are adjacent
# instructions, so a window boundary falling between them hides the pair
# entirely: `recent` resets at every window start. The Y site at 0x140708F6B
# -- which is plainly there in the disassembly -- was MISSED for exactly that
# reason, while the X site 21 bytes earlier was found.
#
# LEAD_IN re-decodes the tail of the previous window so the loads are in hand
# before the window's own instructions are judged. Results are still only
# recorded inside [wstart, wstart+step) so nothing is double-counted.
#
# WARNING: A boundary-blind scan does not look broken -- it looks like a
# smaller answer. Without the control this would have shipped as "six
# candidate offsets" with no sign that any were dropped.
step = 64
LEAD_IN = 48
for wstart in range(start_off, end_off, step):
    lead = min(LEAD_IN, wstart - start_off)
    chunk = data[wstart - lead:wstart + step + 32]
    # reg -> (base_reg, disp) of the load that LAST wrote it.
    #
    # WARNING: A register must be INVALIDATED when anything else writes it, and
    # the positive control is what exposed this. In the real motion routine
    # `edx` is loaded with the X velocity, then reused three instructions later
    # for the acceleration step, then reloaded with the Y velocity. A list that
    # only appends kept the STALE X entry, so the Y integration at 0x140708F6B
    # matched the wrong velocity and was rejected -- while the X site 21 bytes
    # earlier passed, which is what made the gap look like a boundary problem.
    #
    # WARNING: Stale register state does not produce an obvious error. It
    # produces a WRONG PAIRING that still looks like a plausible finding --
    # precisely the failure mode this whole file exists to avoid.
    recent = {}
    attempted += 1
    try:
        for ins in md.disasm(chunk, base + wstart - lead):
            decoded += 1
            if ins.address >= base + wstart + step:
                break
            # Lead-in instructions supply context (the velocity loads) but are
            # judged in their own window, so results are not double-counted.
            in_window = ins.address >= base + wstart
            ops = ins.operands

            # Record loads: mov/movzx/movsx  reg <- [breg + disp]
            is_load = (ins.mnemonic in ("mov", "movzx", "movsx", "movsxd")
                       and len(ops) == 2
                       and ops[0].type == x86.X86_OP_REG
                       and ops[1].type == x86.X86_OP_MEM
                       and ops[1].mem.index == 0)

            if is_load:
                # The destination is freshly defined by this load, so record it
                # AFTER invalidating, never before.
                recent.pop(root_reg(ins, ops[0].reg), None)
                recent[root_reg(ins, ops[0].reg)] = (ops[1].mem.base,
                                                     ops[1].mem.disp)
                continue

            # The integration: add/sub  [breg + disp] <- reg.
            #
            # Checked BEFORE invalidation: `add [rcx+0x58], edx` reads edx and
            # writes memory, so edx is still valid here. Invalidating first
            # would discard the very entry being looked up.
            if (ins.mnemonic in ("add", "sub")
                    and len(ops) == 2
                    and ops[0].type == x86.X86_OP_MEM
                    and ops[1].type == x86.X86_OP_REG
                    and ops[0].mem.index == 0):
                disp = ops[0].mem.disp
                held = recent.get(root_reg(ins, ops[1].reg))
                if (in_window and 0 < disp < STRUCT_MAX and held
                        # same base register => velocity from the SAME struct
                        and held[0] == ops[0].mem.base
                        and 0 < held[1] < STRUCT_MAX):
                    results[disp].append(
                        (ins.address, ins.mnemonic, ins.op_str, held[1]))

            # Anything else that writes a register invalidates what we thought
            # that register held.
            try:
                _, written = ins.regs_access()
            except Exception:
                written = []
            for w in written:
                recent.pop(root_reg(ins, w), None)
    except Exception:
        continue

print("decoded %d instructions over %d anchored windows" % (decoded, attempted))
print()
print("INTEGRATION SITES: [base+POS] += [base+VEL], both inside the player struct")
print("=" * 72)
if not results:
    print("  NONE. That is not automatically a negative: an integration done")
    print("  through a register-held offset, or via SSE, carries no matching")
    print("  displacement pair and cannot be found this way.")
for disp in sorted(results):
    sites = results[disp]
    vels = sorted(set(s[3] for s in sites))
    print("  POS +0x%03X   %d site(s)   velocity from: %s"
          % (disp, len(sites), ", ".join("+0x%03X" % v for v in vels)))
    for addr, mnem, ops, sdisp in sites[:6]:
        print("      0x%X  %-4s %s   (vel +0x%03X)" % (addr, mnem, ops, sdisp))

# ---- POSITIVE CONTROL ------------------------------------------------------
# The documented fighter-motion routine must appear, or the tool is broken.
print()
print("POSITIVE CONTROL -- the documented motion routine at 0x140708F50")
print("=" * 72)
failures = []


def control(pos, vel, addr):
    sites = results.get(pos, [])
    if not any(s[3] == vel and s[0] == addr for s in sites):
        failures.append("  FAIL: +0x%03X += +0x%03X at 0x%X not found"
                        % (pos, vel, addr))
        return
    print("  ok: +0x%03X += +0x%03X  at 0x%X" % (pos, vel, addr))


control(0x54, 0xA8, 0x140708F56)     # X += X velocity
control(0x58, 0xAC, 0x140708F6B)     # Y += Y velocity

if failures:
    print("\n".join(failures))
    print("\n  The scanner no longer reproduces a known-good result.")
    print("  Fix it before believing anything it reports about a new field.")
    raise SystemExit(1)
print("  control passed")
