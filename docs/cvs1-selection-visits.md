# CVS1 selection visits and name-provenance limits

Offline continuation from 29c5771, 2026-10-06. Scope is the existing branch-A
CVS1 pilot, not branch B or a completed F02 adapter. The pinned acquisition
manifest now retains 67 spans and 19 explicit dispatch edges. All 28 CVS1
Python tests pass. No new process reads, input actions or name mappings.

## Slot initialization and advance

Actor-state table 0x141237dc0 entry 2 points to 0x1405d9ba0. Subject
to P+0x65 testing the actor's +0x58c mask, the body advances actor+4,
initializes actor+5/+6, sets R+0x32812+owner to 4, and resets
P+0xa0+owner to zero at 0x1405d9c39. It clears the per-owner 35-byte
availability region and P+0xa2+owner. Conditional paths either call a
preparation helper or advance actor state again and tail-call the existing
catalog setup at 0x1405d9900.

Entry 3 points to 0x1405d9d40. After its P+0xa2 timing gate, input-word
conditions can toggle P+0x76+owner between zero and one. Its later
0x360 input mask or expired R+0x32814 path advances actor+4, clears
P+0xa2+owner, copies the catalog selector to actor+0x62c, and calls
catalog setup. Physical buttons and the visible label of this earlier choice
are not inferred from those masks.

The state-4 handler was previously retained only through 0x1405da3bc.
The continuation is now retained through its return at 0x1405daf02. The
local commit path tests the 0x360 input mask against the owner input record
or allows the R+0x32814 zero path, then rejects an unavailable chosen ID.
It writes the selected byte to P+0x78+4*owner+slot at 0x1405da45f and
copies the selected group to P+0x98+4*owner+slot. Subsequent branches
select and validate a per-slot byte at P+0x80; those variants are not named.

At 0x1405dab7a it subtracts group+1 from R+0x32812+owner using byte
arithmetic. At 0x1405dab90 it increments P+0xa0+owner. Availability
and group-count updates follow. A positive remaining value at +0x32812
branches to the refresh calls at 0x1405daeb4. Otherwise the code clears
unused common-session team entries and advances actor state. The arithmetic
is established; this is not yet a complete user-facing ratio-rule decoder.

The candidate write scan in 0x1405d8000..0x1405dc000 found the slot-zero
store and increment above; both were checked against aligned function bodies.
That scan is a lead, not exhaustive writer coverage: other ranges, aliases,
bulk copies and indirect state replacement remain possible. No conclusion
about absence of cancellation follows from the scan.

## Consequence for shared selection visits

The slot is a useful component of a selection key: confirmation can advance
it while the selected ID stays the same. It is not a unique visit ID. The
same owner can return to slot zero, and shared snapshot restore can replace
state without changing its address. Retain owner generation and revision
separately from the eventual selection-visit identity.

Do not yet wire raw slot into the shared presenter's visit field as the
complete contract. Close cancellation/re-entry writers and the actual active
selection gates first. The new test pins slot commit, advance, reset and
catalog transition; it does not certify their visible timing or physical
input meaning. The scoped pilot still needs same-character/new-slot and
cancel/re-entry acceptance.

## Names: eliminate an unhelpful resource assumption

The already-retained catalog-object constructor 0x140610cf0 creates side-0
objects on list 4. On that branch, it sets R13D=1 at 0x140610d9f and
passes that fixed value in ECX to resource lookup 0x1405c3170 at
0x140610e8c. It stores the returned pointer at object+0x90. The catalog
ID is separately copied to object+0xc6 at 0x140610ebd.

Thus this specific resource lookup does not index its argument by the
selected native ID. It cannot by itself establish an ID/name mapping. This
does not exclude later ID-dependent rendering or a shared atlas. Follow the
actual consumer of that ID and its region/texture selection before labelling
the resource a name or portrait.

The constructor sets object+0x9c to render type 2 at 0x140610ea2.
The retained renderer's jump table at 0x1405e1eb0 sends type 2 to
0x1405e1baf, which calls 0x1405dfb70. That complete helper is now
retained through 0x1405dfe59 (exclusive). It applies object transforms
and flags, loads object+0x90 at 0x1405dfe21, then calls resource wrapper
0x140f36220 or 0x140f36200 depending on flags. This body does not
directly read object+0xc6. The constructor-to-draw route therefore still
does not supply a readable name or an ID-dependent texture region. Later
resource-pointer writes, resource contents and other objects remain leads;
absence of a direct ID read in this helper does not exclude those routes.

The branch-A choice writer's retained body contains availability calls and
the selected-byte store; those calls do not provide a readable name. The
confirmation callee 0x1405a79c0 also reads an eight-byte indexed entry
through R+0x32860, so it is a separate asset/command lead, not a certified
text-name function. Its transitive commands remain unclassified here.

The pinned image dump still has null CVS1 root and common-context slots,
as documented in the acquisition trace. No populated runtime atlas follows
from it. Catalog order, a shell roster and familiar IDs remain insufficient
name evidence. No entries are promoted to verified labels in this increment.
Next offline name work: trace later object+0x90 writes and other ID-dependent
asset consumers to locate the asset-region contract. Any unavailable payload
must remain a specifically named future capture requirement.
