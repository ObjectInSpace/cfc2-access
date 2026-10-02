# PWS2 transform target continuation — 2026-10-02

Continues [the actor trace](pws2-actor-trace.md). This is offline evidence from
the same pinned code dump. No live attachment, camera ownership, HUD ownership
or audible acceptance is established. Existing ownership-capture and shared
audio edits in the checkout were preserved separately.

Reproduce the eight retained spans and eleven data ranges with:

```powershell
python tools/pws2_transform_target_audit.py --output docs/pws2-transform-target-manifest.json
```

The recorder verifies the existing whole-code SHA-256 and instruction-aligned
span ends. The manifest retains bytes and hashes, including switch data.
This supplements the actor manifest; it does not change its span count.

## Actor coordinates feed a script-selected target

The gate at 0x140aece00 enters 0x140aece20 when script byte +8 masked with
0xf0 equals 0x10. The latter complete body ends at 0x140aee000 exclusive.
Its script pointer is transform+0x2d0. After preparing interpolation fields,
it dispatches on script dword +0xc masked with 0xf. The two retained tables
at 0x140aee000 and 0x140aee03c resolve the following selectors:

- 5, 6, 7 and 8 select actor records 0, 1, 2 and 3 respectively.
- 9 aggregates records 0 and 1; 10 aggregates records 2 and 3.
- 15 aggregates all four records.
- 14 uses a separate resource-derived vector path for all four records.

Each direct actor contribution requires a nonzero byte at actor+0x401c.
It reads the three floats at actor+0x3c, +0x40 and +0x44. For aggregate paths,
the helper divides the component sums by the count of included records when
that count is nonzero. With no included records the initialized sum remains
zero. It then adds the vector at transform+0x128 and writes transform+0xec.
This establishes those actor fields as coordinates consumed by this transform
target computation. Inclusion still does not mean human-controlled or alive.

Selector 14 follows actor+0xe0, adds 0x34 and calls 0x14099ef80, a thunk to
0x1409bad20. The wrapper calls 0x140f331c0 and 0x140f31f80, then tail-calls
0x140f33130 with ECX=1. Its transitive resource/vector semantics remain open;
do not substitute actor+0x3c or follow this pointer in a live reader yet.

Selectors 1 and 2 instead dereference transform pointers +0x240 and +0x248;
one publisher is established below. Selector 4 reuses transform+0xf8. Selectors
0 and 11–13 fall back to the prepared offset vector. Selector 3 combines the
two pointed-to vectors, but the instruction at 0x140aed28d writes its second
computed component to [r14], the same address used for the first component.
Do not silently model this branch as an ordinary componentwise midpoint.
The corresponding high-nibble branch has the same store pattern at
0x140aed941. These are instruction observations, not a diagnosed game bug.

## Targets are distinct from final basis vectors

The high nibble of script+0xc selects the source for transform+0xbc. The body
then dispatches on script+8's low nibble through 0x140aee164. Case 0 calls
0x140aec780 to update transform+0xf8 from +0xec, then +0xc8 from +0xbc.
Cases 1, 2, 4 and 5 also involve helpers 0x140aee180 or 0x140aee1f0, which
construct derived targets through shared matrix helpers. Cases 3 and 6 build
bounded actor-derived point arrays and call 0x140aea760; that callee remains
unreviewed. Therefore this trace does not declare all vector modes resolved.

The complete 0x140aec780 body selects on its first argument's low nibble.
Mode 1 copies three components. Modes 2–4 independently step components
toward their targets and clamp when the step magnitude reaches the remaining
distance. Mode 2 divides target-minus-current by a signed integer parameter
plus one; mode 3 divides target-minus-baseline by the supplied signed duration;
mode 4 divides target-minus-baseline by a signed component parameter plus one.
Other modes leave the output unchanged. Zero denominators and nonfinite values
are not guarded here; this is not a safe general-purpose interpolation API.

The established matrix writer later consumes +0xc8 and +0xf8. This closes a
code path from selected actor coordinates through script target preparation
and interpolation to the basis inputs. It does not establish which scripts
run during ordinary play or which pass should drive spatial audio.

## Next acquisition

The complete mode-0 dispatcher establishes one pointer publisher. Its fallback
at 0x140ae89b7 runs initialization when transform byte +0xa is 0 or 1, then
sets that byte to 2. It copies initial vectors from 0x1410ad560/0x1410ad570
to +0xc8/+0xf8, clears +0x180, and publishes pointers 0x14173e990 and
0x14173e9a0 at +0x240/+0x248. It calls 0x140a5bee0 with the transform's
index and -1. This fallback is reachable for out-of-range signed selectors
and several remapped selectors. It does not prove that these are the only
publishers, that pointed-to globals are immutable, or that this mode is active
in ordinary play. The manifest retains the four vector data ranges as dump
observations, not runtime constants.

Trace other publishers/global writers and script selection for ordinary
play; review 0x140aea760 and shared matrix effects; connect actor publication
to HUD records. Preserve actor identity, transform identity and pass identity
as separate values in stage-specific captures. Production audio eligibility
still requires session freshness, ownership and listening evidence.

Validation: manifest generation passed and a second generation compared equal
to the saved JSON. All seven existing actor-audit tests passed; `git diff
--check` passed for the tracked working changes. No runtime source was changed
by this increment, and no DLL build, deployment, live capture or listening
check was performed. Existing uncommitted shared-audio changes remain separate.
