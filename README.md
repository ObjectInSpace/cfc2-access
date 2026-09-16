# CFC2 Access

Screen reader accessibility mod for **Capcom Fighting Collection 2** (Steam
appid 2400430). Speaks menu and gameplay state to NVDA, with braille and SAPI
fallback via UniversalSpeech.

This is an active research project — the mod works for the MTFramework shell
(menus, move lists, training) and in-game support is under active development.
See [CLAUDE.md](CLAUDE.md) for full technical detail and current status.

## Install

1. Copy `dinput8.dll` from this release into the game's install directory:
   `Steam\steamapps\common\CAPCOM FIGHTING COLLECTION 2\`
2. Launch the game via Steam as normal.

## Status

- MTFramework shell (menus, options, move lists): working.
- In-game arcade emulation (CVS1, CVS2, CFJ, Z3U, PWS1, PWS2, MJUS, STG2):
  in progress — see `docs/` for design notes.

## Source

Source, build script (`build.ps1`), and design docs are in this repository.
