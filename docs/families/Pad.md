# The pad family

What a game receives from a controller in a person's hands, and from the touch screen. The screen
names one thing at a time, a person does it, and the line says what arrived: the same controller on
two machines gives two reports that compare line by line. Only the word `pad` runs it (see [The mode
file](../ModeFile.md)), or its row on the start screen, since it waits for a person; it runs right
after the processor, so nobody waits for the families that need no one.

- **pad**, twenty six cases. The first is the controller answering; the sixteen buttons; the
  controller they came from; the eight player slots and the handheld; each controller in turn; the
  sticks let go and pushed to their edges; and a vibration felt or not.
- **touch**, nine cases, after the controller's: targets at the corners and the middle, two fingers,
  a drag and its lift. See [The touch suite](Touch.md).

## The screen

The listening screen's layout, with the prompt large: six times the font's 8 by 16 cell, or as large
as fits. Under it, what to do, the seconds left, and what the controller sends right now: the
buttons held, by the names below, both sticks' values with a box each that follows the stick, and
the slots connected with their styles. The prompt before stays at the foot in its result's colour,
so a person sees what came of a press before the next one. Home and Capture are left out: the system
takes them. A question is answered on the d-pad, up for yes and down for no, never on A or B, which
are what the family asks about, or with a tap on the yes or no box the screen puts at its bottom
right while the question waits; the line then says `yes, tapped` or `no, tapped`. A prompt for a
button takes nothing from the touch screen.

The screen decides nothing: every verdict is taken from the bits the suite read, and a machine with
no screen runs the prompts with only the log, where `EDENPROBE PAD prompt n of 26: ...` names each.

## The cases

Every prompt waits ten seconds (the first, thirty); nothing by then and its case is skipped as
`unanswered`. After three unanswered in a row the prompts left are skipped too, since nobody is
there. A press is every bit that goes down in its first tenth of a second, so one button that
arrives as two bits is caught, and a button held from the prompt before counts only once it is let
go and pressed again. A stick pushed past the service's threshold sets a direction bit, which is
never a press.

| case | tier | asks | passes when |
|---|---|---|---|
| a controller answers before the first prompt | none | any button, within 30 s | a press arrived; the line names its slot and style. Without it every case after is skipped as `depends` |
| the button labelled A arrives as A alone, and fifteen more | exact | A, B, X, Y, L, R, ZL, ZR, plus, minus, both sticks pressed in, the four d-pad directions, by the label printed on the button | exactly the bit asked for, and no other, arrived |
| the controller that answered is player 1 with the full key style | exact | nothing | the slot that answered is No1 and its style has FullKey; an answer from the handheld controls is skipped, since the case asks about an external controller |
| every connected slot of the eight players and the handheld has a style | exact | nothing | at least one slot is connected and each connected one has a style; the line names all nine |
| each controller pressed in turn arrives in one player slot | exact | A on every controller, one after the other; 5 s with nothing ends it | every press arrived in one slot only; the line names the slots in order |
| both sticks let go read nought on both axes | exact | thumbs off, 2 s, then 1 s measured | the largest value on every axis of both sticks is 0 |
| the left stick at its edge reaches full scale on both axes, and the right | level | the stick to its edge, turned slowly; 3 s measured from when it first passes half way | the largest x and the largest y each reach 32767 within 2%; the line says whether the edge is a circle or a square |
| a vibration at half amplitude is felt, and at full | level | a second at 160 Hz and 320 Hz on both bands, then nought; felt? | yes; the line carries the value the system holds while it ran |
| a vibration sent nought stops | level | did it stop when its second ran out? | yes; the line carries the value held after nought |

The rules (the prompts, a press, the judges, the words on a line) are `source/padref.c`, held by the
reference suite to answers worked out by hand; the console half, which reads each of the nine slots
on its own and draws the screen, is `source/tests/t_pad.c`.

## What Eden does today

Lines of rEden 75c12299a6 (`src/`).

- **G1, A, B, X and Y by position.** `input_common/drivers/sdl_driver.cpp:1003-1040`, the default
  binding of an SDL gamepad: A to `SDL_GAMEPAD_BUTTON_EAST`, B to SOUTH, X to NORTH, Y to WEST. On a
  controller whose A is the bottom button, the one labelled A arrives as B, B as A, X as Y and Y as
  X.
- **G2, input in the background.** `sdl_driver.cpp:673` turns on
  `SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS`: the controller works with the window out of focus.
  The family does not control focus; it is said here only.
- **G3, dead zones.** `input_common/input_mapping.cpp:105-111` (a stick mapped whole: range 1.0,
  dead zone 0), `:128-131` (two axes mapped one by one: range 0.95, dead zone 0.15) and `:178-181`
  (range 1.0, dead zone 0.20). Which one a controller gets decides the rest case and the edge cases.
- **G4, the vibration.** `sdl_driver.cpp:790-815` turns an amplitude into `(a + a^0.35) / 2` of full
  scale (`(a + a^0.5) / 2` for a linear type, `a` for a controller with HD rumble), and `:186-210`
  lowers it above 140 Hz on the low band and 200 Hz on the high: at 160 Hz by 0.95, at 320 Hz by
  0.83. The half amplitude case reaches SDL as 0.61 and 0.53 of full scale, the full one as 0.95 and
  0.83.
- An emulator shows a slot as connected whether or not it sees a host controller for it: the first
  case asks for a press for that reason, and its skip line names the slots it found.

## Questions only a console answers

- **Q1.** The largest value a stick at its edge gives, and whether the edge is a circle (radius
  32767) or a square: the edge cases' lines.
- **Q2.** Whether a stick let go gives exactly 0 or a little noise: the rest case's line.
- **Q3.** With two or more external controllers, the ids and styles each gets: the slots case and
  the each-controller case.

## Running it with a person's controller

`make run` starts the emulator from its defaults, which bind the keyboard, so the family's report is
worth comparing only after the person's controller is bound. `tools/eden-controls.py` copies the
`[Controls]` section of the desktop frontend's settings into the command line frontend's, and only
the keys both have:

```sh
make run MODE="quick cpu" XDG=/somewhere/pad-xdg     # once: the emulator writes its settings
tools/eden-controls.py ~/.config/eden/qt-config.ini /somewhere/pad-xdg/config/eden/sdl2-config.ini
make run MODE="quick pad" XDG=/somewhere/pad-xdg TIMEOUT=600
```

The controller has to be the one the settings were made for: a binding names the controller's GUID,
and another controller plugged in instead answers nothing, which the first case then says.
