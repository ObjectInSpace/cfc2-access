# CVS1 listening check and optional in-match capture

1. **Listen.** CVS1 character select now speaks each highlighted character by
   name. The names come from the collection's own data, so there is nothing to
   walk or picture.
2. **Optional capture.** Labelled snapshots during a match let Claude find
   CVS1's health, power gauge, position, stance and clock offline, as was done
   for CVS2. You do not need to read or name anything.

## Part 1: listen

Installed 2026-10-13 from `5f23826`: DLL SHA256
`9a3d12209543d6d40102603fc5cc9f7f279eeb802d954ad35226b4f6f535f791`.
The previous DLL, which CVS2 was accepted on, is kept beside it as
`dinput8.dll.pre-cvs1-select-20261013.bak` (SHA256 `827ed7a8...`). To roll
back, close the game and copy the backup over `dinput8.dll`. The game log
`CFC2Access.log` should show `cvs1-select: admitted ROM 1 (cvs1u), shell
game version 1` on reaching CVS1.

With the new DLL installed, go to CVS1 Arcade character select and move
around normally. Each highlighted character should be spoken once by name.
Choosing a character and highlighting the same one for the next team slot
should speak it again. Tell Claude about any wrong name, a repeat, or silence.

Groove, ratio and EX choices are not spoken yet.

## Part 2 (optional): in-match snapshots

Tool: `tools/collection_state_capture.py --game cvs1`. It only reads: no hook,
no memory writes. It refuses to run unless the reviewed CVS1 code and data
match. Screenshots are off, and fullscreen is fine.

With the game running, in PowerShell from `C:\Users\amock\CFC2 Access`:

```powershell
Get-Process CapcomFightingCollection2 | Select-Object -ExpandProperty Id
python tools\collection_state_capture.py 12345 --game cvs1 --out build\captures\cvs1-match-1 --label "match states" --language "English shell; US version" --minutes 25
```

Replace `12345` with the number the first command prints, and use a new
folder name each time. Alt-tab back to the game; NVDA says "Capture started".
Start a match and press F9 once for each state, in order. After each press,
NVDA says what was captured and what comes next.

1. Your full health, both fighters standing apart, round clock running.
2. Your health visibly lower: take a hit, then wait for the action to settle.
3. You crouching (hold down).
4. You in the air mid-jump (press F9 while jumping).
5. You knocked down.
6. Your power gauge full, or at least one level stocked.
7. Your power gauge empty after spending it.
8. You in the left corner.
9. You in the right corner.
10. The game paused on the pause menu.

If a moment is missed, just carry on. When done, press Ctrl+C in PowerShell
or let it time out. Tell Claude the folder name and anything that went
differently.

## What the evidence will and will not establish

- Snapshots locate fields by labelled differences. Each field still needs
  confirmation against the code that writes it before anything ships.
- The capture's press log does not see every controller, so this session is
  not used to validate the cursor analyzer.
