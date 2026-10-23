# Power Stone 1 health and resource trace, 2026-10-23

The existing October 22 memory capture supports further work without another
visual capture, as requested by the user. This increment identifies health
through damage, recovery and gauge consumers, and retains collectible/form
resource candidates. It does not install hooks or enable match audio.

## Reproduce

```powershell
python tools/pws1_health_audit.py --check
python tools/test_pws1_health.py
python tools/pws1_health_decode.py tools/fixtures/pws1-observations-20261022/state-12.bin.gz
```

Fourteen instruction-aligned spans are hash-pinned in
`docs/pws1-health-manifest.json`. Full instructions regenerate into
`build/pws1-health-audit.json`. Five tests cover manifest reproduction,
captured health loss/reset and resource changes, signed floor/range handling,
separate current/recovery/display values, and root-size refusal.

## Health is a signed word, not a float

For actor `R+0x18428+index*0x7890`, current health is signed int16 at
`+0x7790` (absolute root offsets +0x1FBB8 and +0x27448). Adjacent words
`+0x7792` and `+0x7794` are recovery target and trailing display state.
They must not be reported as separate resources or substituted for health.

The damage routine `0x1409BE300..0x1409BE82E` reads an attack descriptor,
applies conditional multipliers, truncates the result to an integer, subtracts
from the current word, and writes it back. Negative branches choose 0 or -1
according to descriptor flags and context options. The later signed-negative
test routes to a native event. A reader must preserve the signed value; -1
must never become a large unsigned health value. The descriptor and mode
branches have not all been given gameplay labels.

`0x140991360..0x1409913CC` increments current health toward +0x7792 on a
counter and separately decreases +0x7794 toward +0x7792 on alternating updates.
The fixed-actor caller at `0x140993804..0x14099384C` supplies each actor.
Thus even within a round, health need not be monotonically decreasing.

Recovery at `0x140986317..0x14098639A` adds 24/48/96 depending on the
selected item type, caps current health at 240, and raises the two adjacent
values as needed. A conditional both-actor refill at
`0x140993775..0x1409937D0` writes 240 to all three words. Another
context-option path at `0x1409B2412..0x1409B2460` adds four, caps at 240
and copies to the adjacent words. These establish the traced refill cap;
they do not prove exhaustive writer coverage or every mode's rules.

The gauge code at `0x1409AAE1C..0x1409AB003` starts with fixed actor zero,
reads health and +0x7794, emits segments with a maximum segment width of
48, subtracts 48 from the remaining values, and repeats. Its outer loop
advances by 0x7890 and visits two actors. This joins gameplay health to a
gauge consumer independently of numeric correlation or a screenshot.
Its layout branch and animation do not establish final screen positions.

Retained snapshots 7 through 13 show:

| Snapshot | Actor 0 health | Actor 1 health |
|---|---:|---:|
| 7 | 240 | 240 |
| 8 | 156 | 240 |
| 9 | 156 | 240 |
| 10 | 32 | 240 |
| 11 | 32 | 240 |
| 12 | 12 | 240 |
| 13 | 240 | 240 |

Snapshot 13 contains a reset/refill; the plan's round-end label does not
independently prove its cause. The inspection decoder preserves raw signed
values and produces a display candidate only for -1..240, flooring -1 at zero.
It does not infer a match or permission to speak from valid numeric values.

## Collectible and form-resource candidates

`0x140986150..0x140986260` checks actor gates, searches object type 5,
checks collection eligibility, increments byte actor+0x77EC, and sets a bit
in actor+0x77ED using the collected object's +0xE value. Collection is
refused at count three or higher in this path. These are the strong stone
count/identity candidates; object-type construction and HUD icons remain
to be traced before assigning stone colors or complete semantics.

`0x1409ECB50..0x1409ECBBF` invokes that collector and checks the count
against three. The corresponding branch at `0x1409ECD34..0x1409ECD50`
requests action 0x17 through `0x140987220`. Do not infer that reaching
three means the transformation animation has completed.

`0x140986470..0x140986497` sets actor+0x77AD to one, calls model setup,
and initializes the signed word actor+0x77B0 to 2400. The model setup
`0x1409E3410..0x1409E34D0` uses +0x77AD together with fighter ID to choose
scale and select between two model-record sets. A periodic path at
`0x1409ECD50..0x1409ECD76` decrements the resource under a global gate.
Other move-specific writers also modify it; it must not be presented as an
unconditional wall-clock timer or given a universal maximum from one writer.

For fixed actor 1, snapshots 9, 11 and 12 contain respectively:
`(count=3, bits=7, form=1, resource=2307/2292/1349)`. Snapshots 7 and 8
have `(1,2,0,0)`, snapshot 10 `(2,3,0,0)`, and snapshot 13 `(1,2,0,0)`.
The decoder retains these raw observations without declaring transformation
completion, visible gauge eligibility, controller ownership or audio range.

## Next implementation boundary

Camera acquisition/model origin are recorded in `pws-camera-route.md`.
Selection decoding already exists, but both games still need runtime
acquisition and execution-liveness checks before their adapters can emit.
For PWS1 match audio, trace ordinary match substates and shell pause ownership,
finish the resource display join, then acquire actor fields and the stored
view on the frame thread with the correct draw projection. Reuse shared
health/resource/beacon channels after those gates are concrete.

The runtime review found the published PWS1 update callback at `0x140973090`:
it calls `0x140F2FEC0`, then `0x140974100`, and tail-calls `0x140973670`.
`0x140974100` calls the shared dispatch thunk `0x140036B70` and tail-calls
`0x140973300`. Do not assume the selection outer dispatcher `0x140976B50`
is the published per-frame callback. The existing STG2 adapter illustrates
root/ROM bracketing and `frame_liveness` before selection emission, but PWS1
still needs its own reviewed hook ABI, signature guards and pause behavior.
No runtime adapter was added by this research increment.

PWS2 remains a separate four-actor integration: its health is a float at
actor+0x1DC, and its display layout ordinal can differ from fixed actor index.
None of the PWS1 offsets, caps or two-actor assumptions transfer to it.
