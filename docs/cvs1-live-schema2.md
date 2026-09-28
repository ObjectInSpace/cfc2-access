# CVS1 schema-2 control and movement, 2026-09-28

Captured from PID 34768 using commit `641044b` after the user reported
character select open. Both captures completed with exit 0 and passed
startup signature/data checks. Identity was game 0, region 1, ROM 1/cvs1u.
No assets, game writes, hooks, deployment or speech changes.

Requested action: focus the game, tap Right and release, pause about five
seconds, then tap Left and release. User confirmed both taps with the game
focused. Action timestamps were not separately recorded; do not infer exact
timing or additional presses from the requested schedule.

## Retained evidence

Originals are under `build/cvs1-selection/`, with filenames
`live-control-schema2-20260928.jsonl` and
`live-movement-schema2-20260928.jsonl`. Lossless committed copies are
`tools/fixtures/cvs1-control-schema2-20260928.jsonl.gz` and
`tools/fixtures/cvs1-movement-schema2-20260928.jsonl.gz`.

- Control: eight samples, 17:27:29.695725 through 17:27:31.452724 UTC.
  Original 89,545 bytes; SHA-256
  `15789fe91af87b20b88f46725bba24ddc88526b40f463d1f86cba59873550d13`.
- Movement: 240 samples, 17:27:43.352841 through 17:28:43.319679 UTC.
  Original 2,577,793 bytes; SHA-256
  `609b01cc832543db68e4c7b88b4c765a5a625f0deb0a5e64f2c5d5e27d1968dd`.
- All control brackets matched. Movement had 239 matching brackets outside
  the branch-B gate and one changed bracket, sample 113, in the raw phase
  record. No read errors. Mode 0 and phase prefix 2,1,1,6,6 throughout.

## Navigation observations

Side 0 retained owner 0, slot 0, actor state 4/activity 0. Group remained 1;
counts were [10,16,6,3], enabled mask 15. Side 1 retained selected -1,
actor ID 1, and zero group counts. Control selected/actor ID was 0.

Movement transitions, zero-based sample indices, before observations:

- 0 at 17:27:43.352841: selected=actor=0, group-1 position 0.
- 107 at 17:28:10.200476: selected=actor=1, position 1.
- 111 at 17:28:11.204156: selected=actor=0, position 0.
- 114 at 17:28:11.958117: selected=actor=16, position 15.
- 118 at 17:28:12.961510: selected=actor=0, position 0 through the end.

All other group positions remained zero. This connects the native selection
changes to position changes within one group, including its 16-entry wrap.
It does not map IDs to character names or prove graphic visibility.

In both observations of sample 107, actor+0x5ba/+0x5bc were 0x400; in
sample 114 they were 0x800. All other sampled side-0 input words were zero,
including newly pressed +0x5be and staged repeat +0x5d8. The sampled delta
was zero. A 250 ms interval can miss brief pulses, so these zeros do not
exclude press or repeat events between observations.

The user subsequently clarified that they made four taps, not two. The
earlier account confused the requested two-tap schedule with the actual
input count. Four recorded transitions are consistent with four taps; there
is no remaining count discrepancy requiring an extra-transition investigation.
Individual action timestamps and directions were not recorded, so this does
not establish a per-tap correspondence or device-bit mapping. Matching
brackets do not prove atomicity, frame order, or absence of ABA.

## Next measurement and independent work

Retain this window as completed; no further actions were requested after it.
No additional movement request is needed to explain the transition count.
If a new measurement is needed for a different unresolved contract,
use one direction per window with explicit action timing and consider the
existing 100 ms minimum interval, still acknowledging missed pulses.
Character names and implementation packets remain unverified/draft.
