# Game packet audit, 2026-10-13

Step 5 of the [Opus execution plan](opus-shared-selection-execution.md),
audited against `9ecc31c`. The shared interfaces each packet maps onto are
implemented and documented in `../collection-porting-interfaces.md`:
`selection_channel`, `transition_channel`, `match_audio` and `text_channel`.

## Result per title

- [CVS1](cvs1-packet.md): BLOCKED for speech on the native ID-to-name map.
  Selection acquisition is traced and has live fixtures. The rest needs
  bounded discovery. Next: a labelled capture that pairs each selected ID
  with a window screenshot.
- [CFJ](cfj-packet.md): bounded discovery needed. Root and update traced
  only. It has its own context slot.
- [Z3U](z3u-packet.md): bounded discovery needed, on two runtime branches,
  with the branch-selecting option unresolved.
- [MJUS](mjus-packet.md): bounded discovery needed. Root and update traced
  only. Three-member teams with assists.
- [STG2](stg2-packet.md): bounded discovery needed. Root and update traced
  only. Its context placement differs from CVS2.

## What the audit found

- No packet is ready to implement without new evidence. For the four
  root-only titles, the gap is everything below the published root: phase,
  player records, text and camera. That is expected at this stage, and is
  not a blocker in itself.
- The shared interfaces already cover what the packets need. That includes
  four-member teams (CVS1), one-member sides (Z3U, STG2) and unknown ownership.
  The one exception is four-owner audio, which belongs to the later Power
  Stone phase.
- The only blocker is CVS1's name map. Each packet needs per-ROM name maps,
  and none may be filled from roster order or a reference list.
- Regional and language gaps stay visible: every title has Japanese and
  English ROM identities, and no name map is validated for any of them.
