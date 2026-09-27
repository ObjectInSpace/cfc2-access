# CVS2 story and credits reader

This reader follows native rendering, including character epilogues after the
secret bosses, boss/interlude dialogue, and closing-ceremony pages. It does not
guess the ending from character select or announce the whole script early.

## Evidence

- `0x140701170(int entry, int palette, float *position)` renders a story entry.
  It reads the story resource at native context `[1] + 0x9D8000`. The resource
  header supplies an entry index and a per-entry line-count table.
- Each line is copied into an 80-byte stack buffer. At `0x14070121E`, the
  renderer calls `0x140700EA0(char *line, int x, int y)` to substitute winning
  team names. At `0x14070128B`, it draws the resulting buffer. The reader
  captures the buffer after substitution, inside the story-renderer call.
- Epilogue actor `0x1407392E0` computes `48 + 3 * character + page` and calls
  the wrapper `0x140701110`, which reaches the shared renderer. Entries 48–191
  are the three pages for each of 48 character IDs. This is the game's actual
  selection, including its choice of team member.
- Credits dispatcher `0x140739290` is reached through phase `02 01 04 02`.
  Subphase 0 initializes the roll; subphase 1 calls updater `0x14073A700`.
  Setup `0x14073A480` assigns `0x1412A0B70` to native state `R+0x21650`.
- The updater consumes an encoded sprite stream. Bytes below `0x7F` select
  glyphs, `0x7F` advances a space, `0x80..0xEF` control rendering, `0xF1`
  triggers animation, `0xFE` ends a line, and `0xFF` ends the stream.
  Instructions `0x14073ABB6` and `0x14073ABEA` advance the cursor through the
  line and past its delimiter. Only delimiter boundaries are accepted by the
  polling reader, so an intermediate character-loop pointer is not spoken.

## Runtime behavior

`src/cvs2_story.c` installs two signature-checked MinHook detours after native
unpacking. Story speech does not depend on the optional audio-cue setting.
Detours capture complete pages using guarded copies and a bounded queue; they
never call speech or log from the render thread. Lock contention retries on a
later draw. Repeated draws of the same entry are suppressed while continuously
displayed. A gap of 1.5 seconds permits that entry to be read again; consequently
resuming after a long rendering pause can repeat a page.

The existing validated CVS2 state snapshot feeds the credits cursor and drains
the queue on the speech worker. Rejected speech is retained for retry. Speech
is appended so a later story page or credit line does not interrupt the prior
one. A slow speech rate may trail the credits. Leaving CVS2 clears pending
internal events. Other existing menu announcements may interrupt backend speech.

The native credit stream is copied from its fixed, bounded range and checked
against its title prefix and final terminator. All 172 text-bearing lines are
decoded. Letters, digits, spaces, periods, parentheses and hyphens are supported;
unverified decorative glyphs are replaced by spaces. Names containing those
glyphs retain their letters, but their decoration is not announced. Story bytes
use Windows CP932 conversion, which preserves English ASCII; other-language
endings and custom name encodings have not been live-validated.

## Verification and limits

`tools/run_cvs2_story_test.ps1` exercises synthetic renderer calls, multi-line
page assembly, post-substitution names, repeated frames, entry changes, reset,
speech failure/retry, and native credits cursor replay including incomplete
lines, skipped polls, and attachment mid-roll. It uses the actual 2,482-byte
native credits stream from `0x1412A0B70..0x1412A1521`, preserved in
`tools/fixtures/cvs2-credits-stream.bin`.

Fixture SHA256:
`59E1F4795696899762AF3F19D266D8044C233E8C040C97F6BBBC733838D51F5B`.

The regression suite and full DLL build passed. These tests are offline and do
not establish live hook ABI, boss-clear timing, or audible credit queuing. A
live secret-boss ending/credits run remains unverified. Existing character,
groove, ratio, gauge, transition, and win-quote regressions also passed.

Installed DLL SHA256:
`662C91B961512E5272D093D0F78155F4EE37793054DF6E7C4FC8DBDDAE15F4CF`.
Steam startup smoke check on 2026-09-26 confirmed both new hooks installed
(`14:07:29.550` in `CFC2Access.log`) and the game remained responsive. This
validates installation/signatures, not execution of the rare ending path.
Previous DLL: `build/dinput8-before-story-reader-20260926-140713.dll`.

Historical claims that CVS2 has no native string-rendering path are obsolete;
both the win-quote and story readers now trace such paths directly.
