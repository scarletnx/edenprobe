# The game family

## Index

* [The scene](#the-scene)
* [What is judged](#what-is-judged)
* [What is timed](#what-is-timed)
* [What rests on reasoning](#what-rests-on-reasoning)

Every other family asks the machine one thing at a time. A game asks everything at once: it draws,
plays sound, reads its files, calls services and computes on several threads in the same frame,
and presents the frame when the display is ready for it. The game family is one scene that does
that, judges what each part produced against what the processor works out, and times every frame.
Only the word `game` runs it (see [The mode file](../ModeFile.md)), since no console has run it
yet; the start screen has no row for it, and a file that names it keeps it whatever the screen
changes. `frames=<n>` sets how many frames the scene runs, from 1 to 36000; without it, 600, ten
seconds at 60 a second.

- **game**, thirteen cases, six with `notime`. One says the scene came up (none tier): a graphics
  device, a way to the display (a swapchain, or the program's screen), the audio renderer and the fs
  family's big file, which the scene writes the first time as the fs suite does. Then the frames
  run, inside the case that judges their images; the four after it judge the sound, the reads, the
  services and the cores from what the frames left, and the seven last carry the frames' times. If
  the scene does not come up, its bring-up is skipped (it ended the program before), or its frames
  do not run, every case after is skipped as `depends`, saying which.

## The scene

Four threads over the three cores the program has, as a game spreads its work:

| thread | core, priority | each frame |
|---|---|---|
| main | 0, the program's own (44) | reads the pad, asks hid for its style set and the time service for the steady clock, steps a world of 1024 cells, writes the frame's fill quads, texels and 1024 uniform buffers into one of two slots, and hands the frame to the render thread and a read to the file thread |
| render | 1, 43 | takes an image from the swapchain, records and submits the frame, presents it, and hashes the copy of the target the frame made; in a run with a screen, shows that copy on the screen instead |
| audio | 2, 40 | keeps the audio renderer stepping, one update and a wait per renderer frame (5 ms), a tone looping on a voice |
| file | 2, 46 | reads the frame's 64 KiB of the big file and hashes it |

The main thread is put on core 0 alone for the frames and back where it was after, as the kernel
family does; the others are made on their cores. The main thread runs at most one frame ahead of
the render thread: it waits for a slot the render thread has finished with before writing the
next frame, so the frame loop goes at the pace the display gives the render thread.

**What a frame draws.** The target is 128 by 128, offscreen, with a depth buffer, and every frame
draws all of it, in one command list:

- the fill suite's work: eight quads over the whole target, each depth tested and written. The
  nearest is quad `frame mod 8`, which is the last one drawn in only one frame of eight, so a depth
  test that lets everything through leaves the wrong colour in seven. It shows in the top right and
  bottom left quarters;
- the texture suite's: a 16 by 16 texture copied from the processor's slot into the image at the
  start of the list, then sampled, nearest, over the top left quarter, four by four pixels a texel,
  every pixel centre far from a texel's edge. Its texels change every frame;
- the stream suite's: the bottom right quarter as 32 by 32 cells of two by two pixels, one draw
  each, each reading a 1 KiB uniform buffer of its own that the main thread rewrites every frame
  with the cell's word: its position in the world, its number and the frame.

The slots are memory the graphics unit caches, rewritten by the processor every other frame, so
each list starts by dropping the level two cache (the previous frame's wait for idle wrote it back
first). An emulator that serves a buffer or a texture from what it held before the processor's
write draws an old frame's word or texel, and the image says so. After the draws, the target is
copied out for the judge and drawn, as a square in the middle, onto the 1280 by 720 image the
swapchain gave; the list is submitted, the queue drained, and the image presented.

**Presenting.** The swapchain is deko3d's, on the program's default window, with two images, as a
game made with it presents. Taking an image blocks until the display gives one back, which is what
paces a frame to the display. Every image taken is presented, a frame whose submit faulted
included, so the swapchain has them all back when it is destroyed.

A run with a screen (a detailed one, or one the start screen made quick) has the program's screen
on that window from the start screen on, and there the scene leaves it alone: it shows each frame
through that screen, its copy of the target scaled up five times by the processor, and makes no
swapchain, so the frames are paced by the screen's own buffers. A note says so, since present to
present is then the screen's and not a quick run's. Under Eden both ways of sharing the window
failed: a device made after the screen was closed never finished its first submit, and a screen
made again after the swapchain was destroyed never came back, the run ending inside the scene
with no summary. The scene runs last, after the net family, so no suite makes a device after it.

**The sound** is the audio family's path: the renderer opened as that family opens it, a 1 kHz tone
at half of full scale looping over a second on voice 0, into the submix that reaches the speakers at
a gain of nought, so the scene is silent, and the aux effect on that submix recording it. The audio
thread steps the renderer until the frames are over and the voice has played 100 renderer frames
(24000 samples), whatever the frames took, so the aux ring holds the same steady wave on every
machine. It waits for those at most 5000 ms after the frames, as the audio family's own wait does;
past that the sound's case fails with the samples the voice had played, and the scene goes on to
its other cases. Anything the renderer reported against a parameter is a note, `game: the renderer
said: ...`, as in the audio family.

## What is judged

None of it depends on the time a frame took: a frame's number decides its image (the world is
stepped in integers, `source/gameref.c`) and its read, and the sound is read once it has settled.
A machine that runs the frames slowly gets the same verdicts as one that keeps up.

| case | passes when | tier |
|---|---|---|
| the scene comes up | the device, the shaders, the images, the scratch, the card's file, the renderer and the swapchain (in a quick run) all came up | none |
| *n* frames draw the image the processor works out, every frame | every frame's four quarters hash (FNV-1a, row by row) to what `gameref.c` gives for that frame, worked out before the first frame | exact |
| the audio thread's tone keeps the level the renderer's model gives | the aux ring's 1 kHz level is within 3% of what the audio family's model of the renderer's default quality table gives at a ratio of one (`AudioRefResampledLevel`) | level |
| the file thread's *n* reads of 64 KiB hold the file's bytes | every read succeeded, whole, and hashes to the chunk of the pattern the fs family writes (`source/fsref.h`); the reads visit every chunk once in 1024 frames | exact |
| hid and the steady clock answer on each of *n* frames | hid's supported style set is the same every frame and holds the standard styles the program set; the steady clock never fails, never goes back and keeps one source | exact |
| the main, render, audio and file threads run on the cores they were given | each thread, asked its core every frame (the audio thread every step), is on its own | exact |

A frame whose image is wrong fails the case with how many frames were, the first of them and which
quarters it got wrong, and its picture is that frame: what it drew against what it should have.
What reaches the display is not judged: the presented image is the compositor's once queued, and
nothing a program can read back.

## What is timed

Frame 0 is the warm-up: it pays for the device's first work, as a drawing suite's first case does,
and is left out of every measure, the way a drawing suite's warm-up pass is; the first note says what
it took. Over the frames past it, three times per frame, seven cases that carry them as their `us`,
which is what [Time per case](../TimePerCase.md)'s tools and a baseline read. The cases pass when
every frame was presented; their names carry the number of frames, so runs of two `frames=` never
compare. A run of one frame has none past the warm-up, and its seven are skipped saying so.

- **Present to present** is what a player feels: its mean, its 99th percentile and the longest. The
  render thread reads the system tick once each frame's present is queued, and a frame's time runs
  from the one before; the wait for the display is in the next frame's taking of an image, so on a
  machine that keeps up it is the display's period, about 16667 us at 60 Hz, whatever the frame
  cost. It moves only when frames are dropped, and the longest is a hitch as the baseline sees one.
- **The main thread's part** of a frame, from taking its slot to handing the frame over: the
  services, the world's step and the slot's writes. Mean and 99th percentile.
- **The render thread's part**, from the image in hand to the queue standing idle after the
  frame's submit: recording the list, the submit and the graphics unit's work, with no wait for the
  display in it. Mean and 99th percentile.

The two parts are what show a regression while there is room to spare: a machine that gets slower
moves them long before a frame stops fitting in 16.7 ms and present to present moves at all, and
which of the two moved names the path, the processor's or the graphics unit's. None of the three is
the graphics unit's busy time a drawing case carries, and `tools/perf.py` compares the scene's cases
one by one but leaves them out of its per-suite sums, where adding a mean to a percentile would mean
nothing.

The 99th percentile is the nearest rank, the ceiling of 99 in 100 of the frames timed. Under 100
frames past the warm-up that rank is the last: the 99th percentile is the longest frame, and says
nothing the longest does not. From 100 it leaves the slowest frame in a hundred out; at the default
600, the five slowest.

Two notes give the rest, the frames' and the work's:

```json
{"type":"note","text":"game: 600 frames in 10.07 s; frame 0, the warm-up, left out of the times (103895 us present to present, 365 us the main thread's part, 100599 us the render thread's); the 599 past it; present to present: mean 16642 us, 99th percentile 17192 us, longest 30548 us at frame 152, none over 33333 us, two frames at 60 Hz"}
{"type":"note","text":"game: past the warm-up; the main thread's part: mean 187 us, 99th percentile 311 us, longest 362 us at frame 153; the render thread's part, the graphics unit's in it: mean 1132 us, 99th percentile 1882 us, longest 7287 us at frame 152; the audio thread waited for 2016 renderer frames, the longest wait 11023 us, 1 over 10000 us; the file thread made 600 reads of 64 KiB, the longest 50 us"}
```

Those are ScarletNX's, headless on RADV: every frame at the display's period but frame 152, which
missed one, and the two parts together under a tenth of the period.

A frame over 33333 us is a hitch: more than two frames at 60 Hz, a stutter a player sees. The first
note names the first eight by number and counts the rest; the longest frame is the case a baseline
holds. With `notime` the seven timing cases and the two notes are left out and the scene runs the
same frames.

## What rests on reasoning

No console has run the scene. What it expects of one, besides what the other families already
check: that hid answers GetSupportedNpadStyleSet with the set the program gave, standard styles
included; and that a deko3d swapchain on the default window paces at the display.
