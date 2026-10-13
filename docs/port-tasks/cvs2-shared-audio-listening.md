# CVS2 listening check: shared selection, audio and transitions

Status: deployed 2026-10-13. The user reported the same day that CVS2
"works well" after playing it. That is a general acceptance, not an
item-by-item pass of the checklist below. Any later-reported issue reopens
the specific item. This covered the selection integration from `a800114`.

## What is installed

- Deployed DLL SHA256: `827ed7a83a6a49d3dad59da88624120bbb9bd038953fe5e6b3df02fe9b26ea19`.
  Built from the validated tree at `cd3e09a`. It differs from the validated
  build's hash only in 7 PE timestamp and checksum bytes, because builds are
  not byte-reproducible.
- Previous DLL, kept beside it: `dinput8.dll.pre-shared-audio-20261013.bak`,
  SHA256 `f5fa279ec1c47fb9c251c64beb7f62ce77e52be3f101aed01c28997596a135aa`.
  To roll back, close the game and copy the backup over `dinput8.dll`.
- Offline validation: `tools/fixtures/shared-audio-transition-validation-20261013.json.gz`.

## Listening checklist

Play CVS2 normally. Say which items behaved differently from before.

1. Arcade character select: names are spoken as you move, including for a
   second and third team slot. Groove and ratio screens are spoken too.
2. Versus and Training select: the same, including Player 2's cursor.
3. Match start in Arcade: you hear "You: groove, team, ratios. Opponent: ...".
4. Match start in Training: it now says "Player 1: ... Player 2: ..." instead
   of "You". This is deliberate. Training gives no evidence of which side you
   control.
5. The next Arcade opponent: their team is introduced at the start of that
   match. That is new behavior; before, only the fighter's name was spoken.
6. A new round with a different fighter: just that fighter, with its ratio.
7. Health tone on damage, and silence at full health after a KO and in the
   next round. The KO itself must not sweep the tone.
8. Super meter tone as it fills and when a super is spent. Tell me if the
   meter is now silent anywhere it used to sound. It is now limited to the
   match itself.
9. Beacons: continuous during the fight, panned with each fighter, higher when
   airborne, lower crouching or knocked down.
10. Clock ticks at 30, 15 and from 5 down to 1 in Arcade. No ticks in Training,
    on the win-quote screen, or when a new round resets the clock.
11. Pause: the beacons stop within about half a second and resume when you
    unpause. Health and meter still work after resuming.
12. Exit to the menus: every tone stops. Re-enter CVS2 and confirm the cues
    and announcements all return.
13. F5 to F8 volume keys still step, are spoken, and persist after a restart.

## What this does not cover

Other games are unchanged. Nothing here makes a lifetime or concurrency claim
beyond the epoch routing described in `docs/collection-porting-interfaces.md`.
