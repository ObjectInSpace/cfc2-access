# Quick Load exercise, 2026-10-08

The user completed the load exercise and reported: loading goes back to the
menu; exiting that menu put them after victory in the last-boss match, where
the quick save had been made. This is user evidence of this save's resulting
checkpoint. It supersedes the earlier Vs. screen expectation for this save;
it does not establish checkpoint producer timing or behavior in other modes.
The exact offline mode and audible changes were not reported.

## Capture and limits

The six-site diagnostic DLL was staged at a454b6e, SHA-256
76596752567f14f636b6eb547c3d0650a76a4f2aa31cc9b6b1e071fb38942896.
Process 17040 logged probe start at 14:02:14.855 EDT. Its window was 120
seconds. The log records the spoken Load Quick Save row at 14:04:24.830,
after that window. This is a speech-worker timestamp, not a native command
timestamp: worker delays prevent deriving exact action timing from it.

The user closed the game before the capture finished draining. The preserved
CSV contains 2,560 readable schema-2 records, one thread (31436), and 1,280
candidate pairs: 1,278 outer updates, one cleanup and one screen release.
There are no Quick Load, snapshot-load or root-registration events in the
saved portion. There are no unmatched boundaries, but no completion footer
either: dropped count is unknown and pairing is not certified reliable.
The saved events span 32.4202925 seconds from the first recorded event;
this is neither time since launch nor the full recording window.
Observed identities are game 2 / ROM 5 and game 1 / ROM 3.

This run does not establish Quick Load nesting, address reuse, signed result,
secondary-to-primary repair completion or safe acquisition. The absence of a
load event in an incomplete, time-limited capture is not absence of mutation.

## Worker stall found during collection

The log repeatedly records GMD discovery scans taking approximately 8,016 ms
and ending with zero candidates and an exhausted budget. Source inspection
locates the chain: mod.c calls gmd_text_ready on every worker poll while menu
hooks are active; unresolved gmd_text_ready calls gmd_find; gmd_find starts
again at the minimum application address with an 8,000-ms budget.
Failure leaves discovery unresolved, so the next poll repeats the scan.

The same worker handles speech and collection_probe_tick. The probe drains
at most 128 events per tick, so these scans delay both ordinary output and
completion of the buffered CSV even after native recording stops. During
collection the on-disk CSV grew from a header to successive 128-event batches.
Removing the probe marker does not stop or accelerate the active process.

Next prerequisite before another capture: implement and test bounded,
resumable message discovery that preserves eventual lookup and language
validation without blocking the output worker for seconds. Merely spacing
eight-second scans farther apart does not bound an individual stall; merely
shortening a scan that restarts at the beginning can starve later regions.
No scanner fix, replacement deployment or new live exercise is claimed here.

## Retained evidence and cleanup

- Original CSV: cfc2_boundary_17040_168946296.csv; SHA-256
  c9878ceaae27f43a059b939dc37b808fcd0ef5faa80add619a3a5fbae9382a2b.
- Deterministic gzip: tools/fixtures/collection-boundary-quicksave-20261008-incomplete.csv.gz;
  SHA-256 8e56dd899e26a602d58235cfef2bf24f860226ce4760c0c60afe6448f3c35413.
- Local CSV, copied log and parser report: build/quicksave-live-20261008.
- The probe opt-in marker was removed and its absence verified. The game
  was confirmed closed after the user's report. The diagnostic DLL remains
  installed, with the probe disabled for future launches.

Reproduce by decompressing the retained fixture and running
tools/collection_probe_report.py with --out REPORT.json. The parser regression
requires this actual incomplete capture to retain unknown loss and unreliable
pairing despite every saved entry having a matching exit.
