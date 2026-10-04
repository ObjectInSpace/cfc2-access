# PWS2 actor and display ownership continuation

Offline investigation, 2026-10-04, from b01dc6f. Reproduce with:

```powershell
python tools/pws2_hud_audit.py --output docs/pws2-hud-manifest.json
python tools/test_pws2_hud_audit.py
python tools/test_pws2_ownership_capture.py
```

The separate manifest retains nine instruction-aligned spans and nine pinned
numerical operands. The actor and transform-target manifests are unchanged.
This establishes a native gauge/display ownership relationship, not a live
HUD label, controller assignment, health range contract or audio permission.

## Fixed actor index and layout index diverge

R is the root pointer loaded from slot 0x14262f0b0. The complete publisher at
0x140afa010..0x140afa45c branches on byte R+0x5300e. Its zero branch
walks four fixed records, requires nonzero actor+0x401c, and passes the same
index in ECX and EDX to 0x140afb400 and three companion publishers.

The nonzero branch instead passes fixed index in ECX and a separate compacted
ordinal in EDX. It excludes records with zero actor+0x401c or actor+2 equal
to 12 or 13. EDI advances for every fixed record; ESI advances only after an
included record's publisher calls. Thus fixed actors 1 and 3 alone can be
assigned layout ordinals 0 and 1. Allocation failure does not prevent ESI from
advancing. This is a publication-time relationship, not proof of dynamic
relayout after elimination. The visible meaning of the branch remains open.

0x140afb400..0x140afb695 preserves ECX in EBP and EDX in ESI. It stores
the layout ordinal at object+1 and fixed actor index at object+2, and publishes
callback 0x140afab70 at object+8. It creates six subordinate resources.
Their first five +0x80 fields receive 159*layout-280; the sixth receives
159*layout-240. These are local resource components, not normalized screen
coordinates or an established left/right HUD mapping.

The nonzero publisher branch also constructs special objects. For its
three-resource object, object+1 is 0xff and object+2 receives a separately
selected index; callback is 0x140af9a90. The selection scans actor+2 for
12/13 with later matches replacing earlier matches. If none matches, ESI
retains the prior compacted count. The body does not establish a safe fallback
index in that case. Do not map this special object using its +1 value or infer
the gameplay meanings of 12/13.

## These callbacks belong to general-pool display objects

0x140aefc30..0x140aefc98 forwards ECX=8 to 0x140a4bc00 with EDX=1,
then dispatches a resource initializer through a pointer table. It returns null
on allocation failure or releases the object on initializer failure. The
initializer's transitive contract remains outside this increment.

0x140a4bc00..0x140a4bcfb takes the free object at R+0x407f0 and appends
it to the selected list through head/tail arrays R+0x40750/R+0x407a0.
For index 8 those are R+0x40790/R+0x407e0. The earlier pool-rebuild trace
establishes that these are 0x4c8-byte general-pool objects, separate from
the four 0x42b8-byte actors. Existing actor-processing caller evidence invokes
list-8 callbacks; the retained later-list-eight draw body consumes its resources.

This closes one display callback publication path. It does not prove that
fixed actors themselves use callbacks at +8. The known actor-processing loop
calls 0x140b14950 directly. A shared header layout is not proof of identical
dispatch for the two pools.

## Actor value feeds gauge animation

In callback state object+0x28=3, 0x140afab70 uses signed object+2 to read
R+0x40818+0x42b8*owner+0x1dc. It indexes seven four-element float arrays
by that same owner, starting at R+0x5327c and ending at R+0x532dc:

- +0x5327c is a delay initialized to 60 on a changed value, decremented by 1.
- +0x5328c receives the current actor value.
- +0x5329c retains a value that subsequently steps toward the current value.
- +0x532ac records the prior value used to choose the direction.
- +0x532bc/+0x532cc/+0x532dc feed different resource components.

The update writes resource+0x98 for object resources +0xe8/+0xf0/+0xf8
using (value/1000)*71/100+0.29, in the native scalar operation order. A
separate component at resource+0x80 uses object+1 for placement while still
using object+2 for the value. The special callback 0x140af9a90 uses the same
actor/array indexing but different resource arithmetic: value*0.885/1000+0.115.
The manifest pins the actual float32 constants, not their rounded decimal text.

This is strong static evidence for a health-gauge candidate and a direct
actor-to-display join. It is not proof of final visible geometry, exact screen
pan, every gauge mode, or a universal maximum of 1000. The initializer prefix
at 0x140a669b0..0x140a66aa8 writes actor+0x1dc=1000.0, but other writers
and mode-specific limits still need review. The arrays include animation
history, so they must not be treated as seven independent gameplay gauges.

These native callbacks do not validate the signed owner index before accessing
actors/arrays. A reader must validate 0..3, reject nonfinite values, and prove
current publication and visibility rather than copy that unchecked behavior.
State 0/1/2 performs delayed appearance and initial scaling; a published object
does not immediately imply a visible gauge or an eligible audio cue.

## Capture and shared integration consequences

Ownership capture schema 2 adds bounded raw reads of R+0x5300e and
R+0x5327c..R+0x532eb inclusive. It checks the HUD manifest's code spans
alongside lifecycle and actor spans. It does not follow general-pool pointers,
read display objects, decode ownership, or certify draw timing. Matching metadata
still cannot exclude torn data, ABA reuse or same-address reconstruction.

The next draw-stage probe needs both identities: fixed actor index and the
published display object's layout index. Retain callback identity, object
address, object state/visibility fields and lifecycle revision with them.
An actor sample alone cannot reconstruct which display object was published.
The special callback is a separate capture case. Capture resources at their
actual list-8 draw consumer to resolve local +0x80/+0x98 through the matrix
path; do not normalize the local layout constants directly to audio pan.

For the preserved audio_scene API, participant slot/incarnation must remain
stable when layout changes. Only gauges[].hud_pan follows proven display
placement; screen_pan follows independently proved actor projection. The
session router must own epoch/sequence and invalidation. Do not call
audio_scene_begin from these native callbacks or derive epochs from root
addresses. The shared router and the stage probe remain unimplemented.

Next offline edges: review the publisher's caller at 0x140a6e60e within
0x140a6dc30, resource initialization and list-8 matrix construction; trace
actor+0x1dc damage/range writers. Then prepare the reviewed hook ABI and test
bounded capture, drop/overflow, repeated-pass and lifecycle invalidation before
deployment. Live exercises need ordinary and compacted layouts, a missing early
actor, the special-object branch, damage to distinct owners and camera movement.

## Validation

Four HUD-audit tests, eight ownership-capture tests, seven actor-audit tests and
eleven metadata-capture tests passed. HUD controls reject changed numerical
operands, an owner/layout publication mutation and a partial instruction span.
Capture controls preserve raw display values and reject unreadable display state.
Capture CLI help passed. The preserved audio suites passed 35 scene checks and
44 mixer checks. The first audio invocation could not access the installed
compiler in the sandbox; the same runners passed with compiler access.
The transform-target manifest reproduced unchanged, all nine HUD code spans
passed the capture verifier against the offline dump, and `git diff --check`
passed.

No runtime sources changed, and no DLL build, deployment, attachment, live HUD
observation or listening acceptance occurred.
