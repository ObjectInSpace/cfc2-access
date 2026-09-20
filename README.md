# CFC2 Access

Screen reader accessibility mod for **Capcom Fighting Collection 2** (Steam
appid 2400430). Speaks menu and gameplay state to NVDA, with braille and SAPI
fallback via UniversalSpeech.

This is an active research project — the mod works for the MTFramework shell
(menus, move lists, training) and in-game support is under active development.
See [CLAUDE.md](CLAUDE.md) for full technical detail and current status.

## Install

1. Copy **all four DLLs** from this release into the game's install directory:
   `Steam\steamapps\common\CAPCOM FIGHTING COLLECTION 2\`
2. Launch the game via Steam as normal.

The four files are `dinput8.dll` (the mod itself), `UniversalSpeech.dll` (the
speech layer), `nvdaControllerClient.dll` (NVDA), and `ZDSRAPI.dll` (ZDSR).
Nothing else needs installing.

⚠ **Copy all of them, not just `dinput8.dll`.** UniversalSpeech loads
`nvdaControllerClient.dll` by that exact name, and without it speech **silently
falls back to SAPI5** while NVDA is running — you get a voice, so it sounds
like it worked, but it ignores your configured voice, rate, punctuation level
and braille display. The mod logs a warning when it detects this; see
`CFC2Access.log` in the game directory if speech sounds wrong.

## In-match audio (CVS2)

Everything during a match is an audio cue; speech is used only on character
select. Each player has a persistent beacon whose stereo position follows the
fighter across the screen, and the two beacons are told apart by how fast they
pluck — so they stay distinct even when both fighters are near the centre.
Health and super are continuous tones, low and hollow for health, soft and
high for super. The round clock ticks.

### Volume keys

Each cue family has its own level, so anything in the way can be turned down
without touching the rest:

- **F5** health
- **F6** super meter
- **F7** beacons
- **F8** clock tick

The key steps the level **up**, shift+key steps it **down**, and both wrap
around 0 / 25 / 50 / 75 / 100. Everything starts at 100, so **one press
mutes**. The new level is spoken, and it is saved to `cfc2_audio.ini` next to
the game, so it survives a restart.

P1 and P2 share one level per cue — they are the same cue heard from two
sides, and turning one down would make the stereo position lie about which
side is which.

## Status

- MTFramework shell (menus, options, move lists): working.
- CVS2 in-game: working — character select speech, health and super gauges,
  position beacons, and the round clock.
- The other seven arcade games (CVS1, CFJ, Z3U, PWS1, PWS2, MJUS, STG2):
  in progress — see `docs/` for design notes.

## Source

Source, build script (`build.ps1`), and design docs are in this repository.
