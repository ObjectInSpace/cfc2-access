# Port task packet template

Copy this file to `docs/port-tasks/<task-id>.md` when preparing actual work.
Replace every placeholder. This template is not an implementation assignment.

## Identity and scope

- Task ID and title:
- Kind: research / shared-system development / adapter implementation / review.
- Status: draft / ready / in progress / awaiting evidence / ready for review /
  complete.
- Expected executor: Astra or Opus; this is routing, not a capability restriction.
- Base commit and prerequisite commits/packets:
- Game, ROM identities, mode and language:
- Feature IDs from `collection-feature-inventory.md`:
- Concrete user-visible result:
- Files/symbols expected to change:
- Explicit exclusions and adjacent work owned elsewhere:

## Established evidence

- Executable/dump/archive identities and hashes:
- Native entry, branch discriminator and lifecycle:
- State owner, pointer chain, bounds and invalidation:
- Field offsets, widths, signedness and endianness:
- Readers/writers and phase/mode guards, with instruction references:
- Hook ABI and original-call forwarding, if relevant:
- Display/update consumer proving timing or visibility:
- External references used as leads; identify what CFC2 tracing confirmed:
- Relevant unknowns and their impact:

Use “not relevant” with a reason when appropriate. Never replace a required
unknown with an assumed value. Separate code-traced facts from live findings.

## Shared integration contract

- Existing adapter/session/observation/presentation symbols to use:
- Required validity, epoch, freshness and ownership behavior:
- Thread/callback context and bounded execution requirements:
- Text policy or audio semantics selected from common implementations:
- Reset, pause, switch, load and backend-failure behavior:
- Game-specific exceptions, their reasons and regression cases:

## Fixtures and acceptance

- Real fixture paths, hashes, capture conditions and semantic provenance:
- Synthetic fixtures and the edge cases they establish:
- Expected outputs for normal behavior:
- Negative cases that must produce no incorrect output:
- Exact test/build commands and expected successful result:
- Required live run card, including player actions and observations:
- Performance/latency or listening acceptance, where relevant:

Do not count synthetic data as proof of game semantics or installation as proof
that a rare hook path executed. A missing live check remains explicitly open.

## Execution steps

1. Verify dependencies and evidence relevant to the task.
2. Implement the smallest coherent change through the shared interfaces.
3. Run the specified checks and investigate any failures from their cause.
4. Update evidence and status; commit with validation and remaining limits.

Replace or elaborate these steps with the actual bounded task sequence.

## Escalation and independent work

- Specific contradictions or unknowns requiring review:
- Evidence to collect before returning the question:
- Work that remains safe/useful while that question is unresolved:

## Completion record

- Result commit(s):
- Behavior delivered:
- Commands actually run, exit codes and results:
- New traces/fixtures and their provenance:
- Deviations from the packet and why:
- Offline status:
- Live-validation status:
- Open limitations:
- Reviewer findings and resolutions, if applicable:
- Next concrete action:
