# Looking at it on the console

One binary, two ways to run it, and a file on the memory card that chooses.

**Detailed** is the default, because whoever starts this on a console is standing in front of the
screen. Every suite runs, both report channels get every line, the summary is counted and the report
is closed, and only then does the run go on the screen, a group at a time. A group is a suite,
gathered under its family: the judge (selfcheck, support, reference), cpu, pad (pad, touch),
graphics (clear to blit2d), fs, kernel (sched), display (binder), video (vic, nvdec), audio, service
(hwopus, am, fsp, account, ldn, set, csrng, apm, nfp, aoc, time, pl, profile, am-state, am-self, am-functions, auddev, fsdir, am-audio, am-channel, am-storage, mii, am-mode, bsd-local, ipc, hid, time-snap, vi, renderer, ngc, am-lock, set-sys, time-zone) and net (nifm to friend). Three screens, one inside the other:

- **The overview** comes first, on the first group that failed. Its heading carries the run's counts
  and names the groups that failed, as many as fit. Below it, every group under its family with its
  passes, failures and skips in columns and a bar of them in proportion, a failed group's name in
  red; under the list, the selected group's failures by name, or its skips when nothing failed. A
  case the program ended inside is counted apart, as the summary counts it: in violet, in a fourth
  column that only a run with such a case has, and listed under the list when its group failed
  nothing.
- **A group** lists its cases, each marked `PASS`, `FAIL` or `SKIP` in that result's colour, with
  `pic` at the end of the row when the case kept pictures. It opens on its first failure. Under the
  list: the selected case's whole name, as much of its detail as fits, wrapped, and what opening it
  will show.
- **A case** that compared pixels opens on its three pictures side by side, labelled `drawn`,
  `expected` and `judged`, each 64 by 64 target scaled up with nearest neighbour, a key naming the
  four colours of `judged`, and the detail under them. Any other case, and a drawing case that
  failed before it read anything back, opens on its whole name and its whole detail.

The pictures answer what a verdict cannot, whether a failure looks like a rendering fault or like
the judge being wrong; the rest is for finding the case that failed in a run of eight hundred
without a computer beside the console.

**Quick** is what a script wants: every suite, both channels, and the program exits, in about six to
seven seconds under eden-cli (6.4 to 7.3 in most runs at a load of 4 to 9, emulator start included),
several times that when the machine is busy. It is the same run up to the report closing.

**The running screen.** While the suites run, a run with a screen shows how far it is: every
detailed run, and a quick one the start screen chose. It names the step under way and its number
among the run's steps (`step 17 of 34: net`, the steps done drawn as a bar), the pass of a timed
suite (`pass 3 of 10: a rehearsal, timed`), the case under way with its number in the pass and, from
the second pass on, how many the pass has (`case 12 of 40`), the suite a net case belongs to, the
report's counts so far, how long the run has been going and its folder. A launch that goes on with
a run counts the steps it already did. It is drawn by the processor into the program's window when
a case starts or ends, at most twice a second, and at once when a step begins; never inside a timed
interval, and not while the listening sequence has its own screen up. A case that takes long, a
net service waiting out its timeout, holds the screen still until it ends. A quick run from the mode
file draws nothing, as before, so a script's run and the times it reports are what they were; the
word `noprogress` turns it off in any run.

The screens hold every case the report recorded, kept in memory as its line went out, exactly as a
reader of the line gets it back, plus the pictures the judge looked at. Nothing is read back from
the memory card, so a green run and a run that wrote nothing are both worth looking at, and `images`
changes what is on the card, not on the screen. A quick run keeps and allocates nothing for any of
it. The store holds 4096 cases, almost five times a full run; a run past that, or one that ran out
of memory keeping them, says on the overview how many of the report's cases the screens hold.

The order is the safeguard: the viewer refuses to start while a report is open, so both modes leave
the same verdicts and the same images, line for line. The report differs only in the note near the
top that says which mode ran and why, and in the case below. The screen is a second opinion for a
person, never an input to one.

**The start screen** is where a person chooses what runs, with no mode file to write. Every new
detailed run opens on it: no mode file, a file without `quick`, or `quick` with X held at launch. A
cursor walks its rows, each starting as the mode file leaves it:

| row | what it turns |
|---|---|
| begin the run | where the cursor starts: A here begins the run, as plus or minus does from any row, or the begin box at the bottom right |
| graphics, fs, audio, net, pad | the families, each on or off; the judge and the cpu suite run whatever is chosen, and pad, the controller then the touch screen, which waits for a person, right after cpu (see [The pad family](families/Pad.md)) |
| listen | the listening sequence before the audio suite (see [The audio family](families/Audio.md)); turning it on turns audio on, turning audio off turns it off |
| quick | leave when the report closes, with no results screens |
| notime | each drawing suite once, with no timing passes |
| may crash | the cases the may-crash table names in the families chosen (the word `fatal`); Y turns it from any row |
| crash list | the crash list's cases again (the word `retry`); X turns it from any row |
| save | write the choice into `sdmc:/edenprobe/mode`, so the next launch starts from it |

Up and down move the cursor, coming round, and A turns the row under it. Under the rows, the cases
that may crash in the families chosen and the cases on the crash list are listed by name; while
either choice is on with a case to run, a warning says what it means: the program may end, and
started again it goes on after the case that ended it. Nothing pressed for ten seconds begins the
run as the file had it, and any press keeps the screen. What was chosen is in the mode line
(`detailed, chosen on the start screen`, `the cases that may crash, chosen on the start screen`,
`saved to the mode file by the start screen`) and in the journal, so a launch that goes on with the
run after a crash keeps it whether or not it was saved. Saving writes the words the screen chose,
the file's label kept and words nobody understood dropped; saved with quick on, the next launch
leaves without the screen, and X held at launch brings it back. The screen decides the mode, as
holding X does, and reads no case and writes no line.

**Nobody there.** A detailed run whose screen sees no button pressed for ten seconds after the
report closed leaves by itself, and appends one line after the summary:

```json
{"type":"note","text":"detailed mode: nothing pressed for 10 seconds after the report closed, left by itself"}
```

That is for a script that forgot the mode file, which must not leave an emulator running until
something kills it; ten seconds because on a console the person is reading the overview when the
count starts. A press counts, not a connected controller, because an emulator presents a controller
as connected whether or not anyone holds it. The countdown shows on the bottom line, and any button
stops it for the rest of the run. The first eight frames are not a press, nor navigation: a
controller arriving with the window reports its whole mask as newly pressed. An edge that holds
both Y and X changes neither choice, because a pad that connects after those frames reports the
same whole mask once; A in that edge still begins the run when the cursor is on begin the run, and
anywhere else A in an edge that also moves the cursor turns nothing. Two presses, one button each,
still choose both.

**The mode file**, `sdmc:/edenprobe/mode`, chooses the same for a script, a run with no one in
front of it, or a choice that should stay: see [The mode file](ModeFile.md).

**Holding X** as the program starts is the other way into the detailed mode, for a card that says
`quick` and no way to change it. The pad is asked only then, once it turns up: a frame or two with a
controller attached, twelve frames at most. The report is opened before any of this, so a machine
that cannot read the file or reach the pad still leaves a report saying how far it got.

On the screens, each of which names its buttons on its bottom line:

| button | overview | group | case |
|---|---|---|---|
| up, down | the group before or after | the case before or after | the case before or after |
| left, right | the first group of the family before or after | the group before or after, on its first failure | the case before or after |
| A | opens the group | opens the case | |
| B | | back to the overview | back to the group |
| R or ZR, L or ZL | the next or previous group that failed | the next or previous failure in the group | the next or previous failure in the group |
| X | | | the pictures: all three, then each alone |
| plus or minus | quit | quit | quit |

And on the start screen: up and down move the cursor, A turns the row under it or, on begin the run,
begins it, plus or minus begins from any row, Y turns the cases that may crash on or off and X the
crash list's cases again, from any row. Y and X in the same edge change neither. On the screen that
asks whether to go on with a run (see [When the program ends in the middle](Crashes.md)), up and
down choose and A confirms; an edge that holds A and a direction does nothing.

The listening sequence has its own screen, up while it plays, and that screen names its buttons
the same way. Y repeats the take that is playing. X skips to the next part. Neither is read by
the results screens: Y is unused there, and X cycles pictures only on a case, which is not open
during the sequence; the start screen has closed before any suite runs. A hold of X at launch was
already read, and a hold is not a press. Plus, minus and the other buttons do nothing on this
screen, so they cannot quit the sequence.

The pad family's screen is up while its prompts run. Every button is what it asks about, so none of
them does anything there but answer: a prompt for a button takes whatever arrives, a question takes
up for yes and down for no on the d-pad, or a tap on its yes or no box, and nothing quits it; each
prompt runs out by itself. The touch suite's screen takes no tap but the ones it measures.

Every list comes round at either end. A direction held down acts again after twenty frames and then
every four, so the reference suite's three hundred and thirty one cases do not take a press each;
what repeats is the direction pressed, whatever else is held, and only after a press counted past
the first eight frames. Going back lands on the group or case that was open, and the view chosen
with X stays from one drawing case to the next. A frame is composed on the processor only when
something moved or the countdown ticked, so a still screen costs an emulated machine nothing.

**Touch.** Held in the hands, every screen above takes the touch screen as well as the buttons, and
the buttons do what they did. A tap is one finger put down and lifted again: it counts when it is
lifted, and only if it went down and came up on the same row or the same box, so a finger that
slides off a row chooses nothing. A tap on a row moves the cursor there; a tap on the row the cursor
is on does what A does there: on the start screen it turns the row, or on begin the run begins; on
the screen that asks whether to go on it takes that choice; on the overview it opens the group, and
in a group the case. A row is as tall as a line of text, about two millimetres, so a tap that lands
on the next row only moves the cursor, which the second tap then shows is wrong before anything is
chosen. A bar of boxes at the bottom right holds what has no row: begin on the start screen, quit
on the overview, back in a group, and on a case prev, next, view (when it kept pictures) and back.
The countdown and the button line stop short of the bar. A list longer than the screen keeps the
cursor in its middle, so tapping the last row shown walks it along. The first eight frames are not
a tap, as they are not a press; a finger already on the screen when a screen opens counts only once
lifted and put down again; a second finger makes a touch no tap; and a finger put down keeps a
screen as a press does. The decisions are `source/ui.c`'s (`UiTouchStep`, `UiStartTap`,
`UiResumeTap`, `UiTapStep`, `UiPadTap`), which the support suite checks; `source/viewer.c` only
reads the touch screen. A docked console's touch screen is off, and the buttons are the only way
there.
