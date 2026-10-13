# CVS1 name and in-match capture run card

Purpose: one play session that answers the CVS1 questions blocking the port.

1. Which character each native selection ID is. The tool pairs every ID with a
   screenshot, and Claude reads the name from the picture.
2. Where health, power gauge, position, stance and the clock live. Labelled
   whole-state snapshots are diffed offline, as was done for CVS2.

You do not need to read the screen or name anything. The tool speaks through
NVDA when you press F9.

Tool: `tools/collection_state_capture.py --game cvs1`. It only reads. No hook,
no memory writes, no DLL change. It refuses to run unless the reviewed CVS1
code and data match. It also records your direction presses (keyboard arrows
or a controller) and a compressed trace of the game state, so this session
doubles as the test of the cursor-finding analyzer used for later games.

## Before you start

- In Options, PC Settings, set Display to Windowed. If the screenshots come
  out black, the tool says "Warning: the screenshot is unusable".
- Note the shell language and which CVS1 version you pick. The packet
  currently covers the US version, `cvs1u`.
- Do the CVS2 listening checklist first if you like
  (`cvs2-shared-audio-listening.md`). Its DLL is already installed.

## Start the tool

Launch the game through Steam. Then in PowerShell, from `C:\Users\amock\CFC2 Access`:

```powershell
Get-Process CapcomFightingCollection2 | Select-Object -ExpandProperty Id
python tools\collection_state_capture.py 12345 --game cvs1 --out build\captures\cvs1-session-1 --label "roster walk then match states" --language "English shell; US version" --minutes 25
```

Replace `12345` with the number the first command prints. Use a new folder
name each time. Alt-tab back to the game. The tool says "Capture started".

## Part 1: walk the roster (Player 1 side)

- In CVS1 Arcade, go to character select.
- Before moving, stay still on one character for about five seconds. That
  quiet stretch is the analyzer's control.
- Move over every character, one step at a time, waiting about one second on
  each. Cover every row and both groups (Capcom and SNK).
- Each new highlighted character is pictured automatically and silently.
- You do not need to confirm a character, but you can.

## Part 2: in-match snapshots

Start a match. Press F9 once for each state below, in this order. After each
press the tool says what it captured and what comes next.

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

If a moment is missed, just carry on. A wrong label can be corrected
afterwards if you say which one went wrong.

## Finish

Press Ctrl+C in the PowerShell window, or let it time out. Tell Claude the
folder name, the CVS1 version and language, and anything that went
differently. Nothing needs to be named by sight.

## What Claude does afterwards

Run `python tools\cursor_candidates.py build\captures\cvs1-session-1`. CVS1's
cursor is already traced, so this is the analyzer's live control: it must rank
the selection byte (root+0x3ED0) and the group position bytes (around
root+0x3F5C) at the top. If it does not, the analyzer is not used on another
game until the reason is understood.

## What the evidence will and will not establish

- A name is accepted only when the screenshot shows it clearly, with ROM,
  branch, side and language recorded. Unclear IDs stay unmapped.
- Snapshots locate fields by labelled differences. Each field still needs
  confirmation against the code that writes it before anything ships.
