# CVS1 movement window 02

Captured using `ddefd10` tool code from PID 29680 after the user reported
CVS1 character selection open. The assistant requested Right once, a pause,
then Left once without confirmation. The user confirmed both inputs were
made with the game focused and heard character movement, but did not know
the highlighted character. Names remain unknown.

The earlier window is a steady control: the user said they did not think
they made its requested moves. Do not combine its action label with this
window's confirmation.

## Evidence

Original: `build/cvs1-selection/live-movement-20260928-02.jsonl`.
Lossless committed copy:
`tools/fixtures/cvs1-movement-20260928-02.jsonl.gz`.

- 240 samples, 2026-09-28 06:19:32.399233 through 06:20:32.327900 UTC.
- Original bytes: 2,170,834.
- Original SHA-256:
  `5dadfd9b773e497e1e7f221708df6aad6016a1edcd5a4a69183262e882a2368c`.
- Capture process completed with exit 0. Startup signature/data checks passed.
- 239 samples report `outside_scope`; one reports `changed`; no read errors.
- No asset reads, game writes, hooks, deployment or speech implementation.

Native mode stayed 0 and initial phase bytes stayed 2,1,1,6,6. Thus this is
branch-A state evidence, outside the branch-B asset gate. The tool retained
the state without expanding that gate.

Side 0 stayed owner 0, slot 0, state 4 and activity 0. Its selected byte and
actor byte agreed at each reported transition. Zero-based sample indices:

- Sample 0, 06:19:32.399233: selected=actor=1.
- Sample 3, 06:19:33.151939: selected=actor=0.
- Sample 11, 06:19:35.157835: selected=actor=16.
- Sample 19, 06:19:37.163473: selected=actor=0, retained through the end.

Side 1 retained selected=−1, actor=1, state/activity=0. Sample 26's
before/after mismatch is in the phase block; its selected/actor values agree
across the bracket. Keep this transition record rather than treating the
whole window as atomic.

The user confirmed two requested inputs, while the capture has three value
transitions. Exact input timestamps were not recorded; do not label each
transition Right or Left, infer a wrap order, or infer a character from ID 16.
The evidence supports changes in these native fields during a user-confirmed
movement window. It does not establish a one-to-one input/event mapping,
physical-controller identity, character names or final graphic visibility.

## Next work

Trace branch A's active handler and actor state 4 against this sequence;
retain the special-choice and confirmation uncertainties. Reuse these raw
fixtures when testing a future reader, with unknown names and conservative
phase handling. A later single-direction capture with explicit timing can
resolve direction/order if the native writer trace does not settle it.
No further inputs were requested after this window finished.
