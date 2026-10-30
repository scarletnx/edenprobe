# The touch suite

## Index

* [The screen](#the-screen)
* [The cases](#the-cases)
* [Questions only a console answers](#questions-only-a-console-answers)
* [What Eden does](#what-eden-does)
* [Running it on Eden with the mouse](#running-it-on-eden-with-the-mouse)

The touch screen as a game reads it: a person taps targets drawn at known points, puts two fingers
down, drags one across, and the line says where and under which id the touches arrived. It is part
of [the pad family](Pad.md), right after the controller's prompts, so only the word `pad` or the
pad row of the start screen runs it: it needs a person too.

- **touch**, nine cases. The first is the touch screen answering; then a tap on a target at each
  corner and in the middle; two fingers at once; a finger dragged left to right; and the lift after
  the drag. One note gives the diameters and angles that arrived.

The program's own screens take touch as well as the buttons: see Touch in [Looking at it on the
console](../Console.md).

## The screen

The touch screen's coordinates are the frame's pixels, 1280 by 720, so a target drawn at a pixel is
where a finger on it reports. The screen draws one target at a time, an amber ring with a cross,
and for the drag its two ends on one row. The prompt and what to do sit between the top targets and
the middle one; under the middle one, what arrives right now (how many fingers, the first one's id,
place, diameter and angle) and the prompt before in its result's colour. Each finger on the screen
is drawn as a green square where the machine put it, which shows a misplaced mapping at a glance.
Nothing written ever covers a target: the support suite checks every one.

The screen decides nothing: every verdict comes from the states the suite read with libnx's
`hidGetTouchScreenStates`, and a machine with no screen runs the prompts with only the log, where
`EDENPROBE TOUCH prompt n of 9: ...` names each.

## The cases

Every prompt waits ten seconds for a finger (the first, thirty) and then for every finger to come up
again, at most twenty seconds more; nothing by then and its case is skipped as `unanswered`. After
three unanswered in a row the prompts left are skipped too. A finger already down when a prompt
begins is not its answer, nor is it until lifted and put down again.

| case | tier | asks | passes when |
|---|---|---|---|
| a touch arrives before the first target | none | a touch anywhere, within 30 s | a touch arrived. If none did: on a docked console, or a touch screen that sends no state, or one whose sampling number never moves, every case is skipped as `unavailable` with that reason; otherwise the case is `unanswered` and the rest `depends` |
| a tap on the top left target lands within 48 pixels of it, and the top right, bottom left, bottom right and middle | position | a tap on the cross at (96,96), (1184,96), (96,624), (1184,624) and (640,360) | where the finger was last before it came up is within 48 pixels of the cross, about five millimetres on the console's panel; the line gives the offset either way, where it came down and its id |
| two fingers at once arrive as two touches with different ids | exact | two fingers down together | one state held two touches, and their ids differ |
| a finger dragged left to right moves right under one id | position | one finger from the mark at (160,560) to the one at (1120,560) | one finger throughout, one id, starting in the left third and ending in the right, at least half the screen apart, never back more than 16 pixels, over five states or more; the line says whether the first state carried the Start attribute and the last the End one |
| a lifted finger leaves no touch behind | exact | nothing: measured from the drag's lift | every state in the half second after the lift holds no touch |

The note after the suite:

```json
{"type":"note","text":"touch: 214 finger states read; diameter 90..90 across and 90..90 down, angle 0..0; ids 0"}
```

The rules (the prompts and targets, what a prompt collects, the judges, the words on a line) are
`source/touchref.c`, held by the reference suite to states made to order; the console half, which
reads the touch screen and draws, is `source/tests/t_touch.c`; the screen is `UiComposeTouch` in
`source/ui.c`.

## Questions only a console answers

- **Q1.** Where a fingertip aimed at a cross lands: the five target lines give the offsets on a
  real panel, which says whether 48 pixels is right.
- **Q2.** The diameters and the angle a finger reports, and how they change with the finger: the
  note.
- **Q3.** How the ids are numbered: from nought for the first finger, again from nought for the next
  touch, or counting up across touches. The target lines and the two fingers' line say.
- **Q4.** Whether the first state of a touch carries the Start attribute and the last state, still
  counted, the End one: the drag's line.
- **Q5.** Whether the touch screen sends states, with nought touches, while the console is docked,
  or none at all: the first case's line on a docked console.

## What Eden does

Lines of rEden 75c12299a6 (`src/`); read, not run.

- **G1, the mouse is the touch screen.** Unless native mouse emulation is on (it is off by default,
  `common/settings.h:830`), the left button and the pointer are a touch
  (`hid_core/frontend/emulated_console.cpp:28-32`). The desktop window turns a press into one
  (`yuzu/bootmanager.cpp:462-479`), a move into its new place (`:481-511`) and a release ends it
  (`:513-521`); a real touch screen's points come through `:551-573`, one touch each.
- **G2, the coordinates.** The desktop frontend clips the pointer to the picture inside the window
  and scales it to nought to one across it (`core/frontend/emu_window.cpp:21-41`); the touch driver
  multiplies by 1280 and 720 and cuts to whole pixels
  (`hid_core/resources/touch_screen/touch_screen_driver.cpp:87-91`,
  `hid_core/resources/touch_screen/touch_types.h:18-19`). A click lands on the pixel under it, give
  or take one in a scaled window, and one in the black bars lands on the picture's edge. The
  command line frontend scales against the whole window instead, with no bars taken off
  (`yuzu_cmd/emu_window/emu_window_sdl3.cpp:99-109`), so a window that is not 16 by 9 shifts every
  touch; and a click on its last column or row comes out at 1280 or 720, which the touch resource
  then drops as off the screen
  (`hid_core/resources/touch_screen/touch_screen_resource.cpp:473-484`).
- **G3, the ids.** A touch takes the first free slot and reports the slot as its id
  (`emulated_console.cpp:213-256`, the id at `:254`): the mouse is always id 0, and a second finger
  comes only from a touch screen, the Android frontend, or a touch from button map
  (`emulated_console.cpp:48-74`), a host button that presses a fixed point. With the mouse alone the
  two fingers case fails with "never two at once: the most in one state was 1".
- **G4, the diameter and the angle.** Settings, 90, 90 and 0 unless changed
  (`frontend_common/config.cpp:194-199`), put on every touch
  (`touch_screen_driver.cpp:92-94`): the note reads 90..90 and 0..0.
- **G5, Start and End.** A touch's first state carries Start, the next keeps its place without
  reading it, and the last carries End while still counted (`touch_screen_driver.cpp:49-71`); the
  drag's line should say yes to both.
- **G6, docked or not.** Nothing turns the touch screen off when the emulated console is docked:
  the handheld input is enabled once (`hid_core/resource_manager.cpp:213`) and nothing in
  `hid_core/` reads the docked setting, so the mouse touches either way, where a docked console's
  screen is off. The "touch screen enabled" setting is kept and shown
  (`frontend_common/config.cpp:192-193`, `yuzu/configuration/configure_input_advanced.cpp:141`)
  and read by nothing that makes a touch.
- A state is written every 4 ms with a new sampling number
  (`touch_screen_resource.cpp:20`, `:437-438`), so the first case finds a live sensor.
- Read and not exercised by any case: the update scales x by the magnification's y and moves y by
  the offset's x (`touch_screen_resource.cpp:465-468`). Both are 1 and 0 unless the system service's
  SetTouchScreenMagnification sets them (`:290-300`), which a game does not call.

What a mouse on Eden should give: the first case, the five targets when the cross is clicked, the
drag and the lift pass; two fingers fails as G3 says; the note reads diameter 90..90 across and
90..90 down, angle 0..0, ids 0.

## Running it on Eden with the mouse

The mouse is the touch screen as long as the emulator's own mouse emulation is off, which it is in
a new settings directory. `make run` starts the command line frontend in a window of 1280 by 720
(`yuzu_cmd/emu_window/emu_window_sdl3_vk.cpp:29-32`), so a click lands where it is drawn as long as
the window keeps that size:

```sh
make run MODE="quick pad" TIMEOUT=600
```

The pad prompts come first. A person with a controller bound answers them (see [The pad
family](Pad.md#running-it-with-a-persons-controller)); a person with only the mouse lets the first
one wait out its thirty seconds, after which the rest of the pad family is skipped as `depends` and
the touch prompts begin. Click the middle of each cross, hold the button down across the drag, and
let go between prompts; the two fingers case needs a touch screen or a touch from button map.
