# Looking at it on the console

One binary, two modes, chosen by the start screen or the mode file.

- **Detailed** (the default): every suite runs, the report is closed, and then the results go on the screen, a group
  at a time.
- **Quick**: every suite runs and the program exits when the report closes. For scripts.

Both leave the same verdicts and the same images, line for line: the viewer refuses to start while a report is open.
The report differs only in the mode note near its top and in the line a detailed run adds when nobody is there.

## The results screens

A group is a suite, gathered under its family: the judge (selfcheck, support, reference), cpu, pad (pad, touch),
graphics (clear to blit2d), fs, kernel (sched), display (binder), video (vic, nvdec), audio, service (hwopus to
acc-app) and net (nifm to friend). Three screens, one inside the other:

- **The overview** opens on the first group that failed. Its heading has the run's counts and the failed groups' names.
  Below, every group with its passes, failures and skips in columns and a bar in proportion, a failed group in red;
  under the list, the selected group's failures, or its skips when nothing failed. Cases the program ended inside are
  counted in a fourth column, in violet, that only such a run has.
- **A group** lists its cases marked `PASS`, `FAIL` or `SKIP`, with `pic` when the case kept pictures, and opens on its
  first failure. Under the list: the selected case's name, its detail wrapped, and what opening it shows.
- **A case** that compared pixels opens on its three pictures, `drawn`, `expected` and `judged`, each 64 by 64 scaled up
  with nearest neighbour, a key for `judged`'s four colours, and the detail. Any other case opens on its name and its
  whole detail.

The screens hold every case the report recorded, kept in memory as its line went out, and the pictures the judge
looked at; nothing is read back from the card, and `images` changes only what goes on the card. The store holds 4096
cases; a run past that, or one that ran out of memory, says on the overview how many the screens hold. A quick run
keeps nothing.

The look is ScarletNX's: its launcher's 8 by 16 VGA font and its YoRHa theme's colours. The screens a person chooses on
(start, resume, results) are on the theme's paper page with a faint grid, the row under the cursor inverted; the
screens drawn while the run goes (running, listening, pad, touch) are on its run screen's dark page. Every screen names
its keys on a band along its bottom, with the countdown above it and the touch bar's boxes at its right. See
[Credits](Credits.md).

## The running screen

A run with a screen (every detailed run, and a quick one the start screen chose) shows how far it is: the step and its
number (`step 17 of 34: net`, with a bar), the pass of a timed suite (`pass 3 of 10: a rehearsal, timed`), the case and
its number in the pass (`case 12 of 40`, the total from the second pass on), the net suite a case belongs to, the
counts so far, the time the run has taken and its folder. It is drawn when a case starts or ends, at most twice a
second, and when a step begins; never inside a timed interval, nor while the listening sequence has its own screen. A
case that waits out a timeout holds it still. A quick run from the mode file draws nothing; `noprogress` turns it off
in any run.

## The start screen

Every new detailed run opens on it: no mode file, a file without `quick`, or `quick` with X held at launch. Each row
starts as the mode file has it:

| row | what it turns |
|---|---|
| begin the run | the cursor starts here: A begins the run, as plus or minus does from any row, or the begin box |
| graphics, fs, audio, net, pad | the families; the judge and cpu always run, and pad (the controller, then the touch screen, which waits for a person) right after cpu ([The pad family](families/Pad.md)) |
| listen | the listening sequence before the audio suite ([The audio family](families/Audio.md)); on turns audio on, audio off turns it off |
| quick | leave when the report closes, with no results screens |
| notime | each drawing suite once, with no timing passes |
| may crash | the cases the may-crash table names in the families chosen (the word `fatal`); Y from any row |
| crash list | the crash list's cases again (the word `retry`); X from any row |
| save | write the choice into `sdmc:/edenprobe/mode` for the next launch |

Under the rows, the cases that may crash in the families chosen and those on the crash list, by name; while either
choice is on with a case to run, a warning says the program may end and, started again, goes on after that case.
Nothing pressed for ten seconds begins the run as the file had it. What was chosen goes in the mode line (`detailed,
chosen on the start screen`, `the cases that may crash, chosen on the start screen`, `saved to the mode file by the
start screen`) and in the journal, so a launch that resumes after a crash keeps it whether or not it was saved. Saving
writes the words chosen, keeps the file's label and drops words nobody understood; saved with quick on, the next
launch leaves without the screen, and X held at launch brings it back.

**Holding X** at launch is the other way into the detailed mode, for a card that says `quick`. The pad is asked once it
turns up, twelve frames at most. The report is opened first, so a machine that cannot read the file or reach the pad
still leaves a report.

## Nobody there

A detailed run that sees no button pressed for ten seconds after the report closed leaves by itself and appends one
line after the summary:

```json
{"type":"note","text":"detailed mode: nothing pressed for 10 seconds after the report closed, left by itself"}
```

A press counts, not a connected controller: an emulator presents one whether or not anyone holds it. The countdown
shows above the keys, and any button stops it for the rest of the run.

## Buttons

| button | overview | group | case |
|---|---|---|---|
| up, down | the group before or after | the case before or after | the case before or after |
| left, right | the first group of the family before or after | the group before or after, on its first failure | the case before or after |
| A | opens the group | opens the case | |
| B | | back to the overview | back to the group |
| R or ZR, L or ZL | the next or previous group that failed | the next or previous failure in the group | the next or previous failure in the group |
| X | | | the pictures: all three, then each alone |
| plus or minus | quit | quit | quit |

- **Start screen:** up and down move the cursor, A turns the row or begins the run, plus or minus begins from any row,
  Y turns the may-crash cases and X the crash list's, from any row.
- **Resume screen** ([When the program ends in the middle](Crashes.md)): up and down choose, A confirms.
- **Listening screen:** Y repeats the take that is playing, X skips to the next part; nothing else does anything, so
  nothing quits the sequence.
- **Pad screen:** every button is what a prompt asks about. A button prompt takes whatever arrives, a question takes up
  (yes) or down (no) on the d-pad or a tap on its box, and each prompt runs out by itself. The touch suite's screen
  takes only the taps it measures.

Rules every screen shares:

- The first eight frames are neither a press nor a tap: a controller arriving with the window reports its whole mask as
  newly pressed.
- An edge that holds both Y and X changes neither choice; in an edge that also moves the cursor, A turns nothing (on
  begin the run it still begins). An edge with A and a direction does nothing on the resume screen.
- Every list comes round at either end. A held direction repeats after twenty frames, then every four:
  the reference suite's three hundred and sixty nine cases do not take a press each.
- Going back lands on the group or case that was open; the picture view chosen with X stays from one case to the next.
- A frame is composed only when something moved or the countdown ticked.

## Touch

Every screen takes the touch screen as well as the buttons. A tap is one finger down and up on the same row or box,
counted when it lifts; a finger that slides off chooses nothing, a second finger makes it no tap, and a finger already
down when a screen opens counts only once lifted and put down again. A tap on a row moves the cursor there; a tap on the
cursor's row does what A does. A bar of boxes at the right of the band holds what has no row: begin on the start
screen, quit on the overview, back in a group, and prev, next, view (with pictures) and back on a case. A list longer
than the screen keeps the cursor in its middle. A finger down keeps a screen as a press does.

The decisions are `source/ui.c`'s (`UiTouchStep`, `UiStartTap`, `UiResumeTap`, `UiTapStep`, `UiPadTap`), which the
support suite checks; `source/viewer.c` only reads the touch screen. A docked console's touch screen is off.
