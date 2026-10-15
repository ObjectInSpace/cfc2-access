# CFC2 Access

Screen reader accessibility mod for **Capcom Fighting Collection 2** (Steam
appid 2400430). Speaks menu and gameplay state to NVDA, with braille and SAPI
fallback via UniversalSpeech.

This is an active research project — the mod works for the MTFramework shell
(menus, move lists, training) and in-game support is under active development.
Full technical detail and current status are in the source repository:
https://github.com/ObjectInSpace/cfc2-access

## Install

1. Copy **all four DLLs** from this release into the game's install directory:
   `Steam\steamapps\common\CAPCOM FIGHTING COLLECTION 2\`
2. Launch the game via Steam as normal.

The four files are `dinput8.dll` (the mod itself), `UniversalSpeech.dll` (the
speech layer), `nvdaControllerClient.dll` (NVDA), and `ZDSRAPI.dll` (ZDSR).
Nothing else needs installing, and there is nothing to switch on: every
feature below is active as soon as the game starts. The two UniversalSpeech
licence files are included for reference and do not need copying.

If you installed an earlier version, any `cfc2_audio.on` or `cfc2_guitext.on`
files left in the game folder are now ignored and can be deleted.

⚠ **Copy all of them, not just `dinput8.dll`.** UniversalSpeech loads
`nvdaControllerClient.dll` by that exact name, and without it speech **silently
falls back to SAPI5** while NVDA is running — you get a voice, so it sounds
like it worked, but it ignores your configured voice, rate, punctuation level
and braille display. The mod logs a warning when it detects this; see
`CFC2Access.log` in the game directory if speech sounds wrong.

## In-match audio (CVS2, CVS1, Capcom Fighting Evolution, Street Fighter Alpha 3)

Everything during a match is an audio cue; speech is used only on character
select. Each player has a persistent beacon whose stereo position follows the
fighter across the screen, and the two beacons are told apart by how fast they
pluck — so they stay distinct even when both fighters are near the centre.
Health and super are continuous tones, low and hollow for health, soft and
high for super. The round clock ticks at 30, 15 and each of the last five
seconds. The same cues and keys work in every supported game.

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

- MTFramework shell (menus, options, move lists): working, including each
  game's EX Settings page.
- CVS2 in-game: working — character select speech, health and super gauges,
  position beacons, and the round clock. Match start and new fighters are
  announced; in Training these say "Player 1" and "Player 2", since the game
  gives no sign of which side you control.
- CVS1 in-game: character select speaks each highlighted character by name.
  In a match: health tones, position beacons, pitch rising and falling
  through a jump, and silence on pause, all tested in play; the power gauge
  tone and clock ticks are not yet confirmed in play. The groove screen
  ("Capcom Groove" / "SNK Groove"), the speed menu after character select
  ("Normal" / "Turbo") and the order screen ("Ryu first") are spoken.
  On-screen system text is spoken, except the "Free Play" credit display.
  Not yet: ratio and EX choices, and a pitch change for crouching or being
  knocked down.
- Capcom Fighting Evolution in-game: character select speaks each
  highlighted name, and match audio has the same cues as CVS2. On-screen
  system text is spoken.
- Street Fighter Alpha 3 in-game: character, ism and speed select are
  spoken, match audio has the same cues, and the opening screens, pre-fight
  story and on-screen system text are spoken. This works with the game's
  **Additional Characters** setting on or off; with it on, Eagle, Maki, Yun
  and Ingrid are named on the select screen.
- CVS2 text: win quotes, story pages, credits and on-screen prompts are
  spoken, and the **`` ` ``** key (the key left of 1) reads the current text
  again, in every supported game. In the menus it repeats the last thing the
  mod said.
- The other four arcade games (PWS1, PWS2, MJUS, STG2): in progress, not yet
  playable with the mod. The plan for porting the CVS2 features to them is in
  `docs/collection-port-plan.md` in the repository.

## Source

Source, build script (`build.ps1`), and design docs are in the repository:
https://github.com/ObjectInSpace/cfc2-access

`.\build.ps1 -Deploy` builds and copies the mod into the game folder;
`.\build.ps1 -Package 0.5.1` builds the release zip from a clean checkout.
