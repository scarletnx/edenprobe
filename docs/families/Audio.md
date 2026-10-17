# The audio family

The audio family asks the audio renderer, with its parameters written field by field, so it can set
what libnx's driver leaves out: a voice's sample rate conversion quality, effects, and a circular
buffer sink. It judges what the renderer made by recording it: an aux effect on a submix that
reaches the speakers at a gain of nought records the resampler and the limiter, and a circular
buffer sink records the final mix. Only a word runs it, `audio` or `listen`, since it makes sound;
see below.

- **audio**, twelve cases. An 11 kHz voice at the output rate keeps its level at high quality, and
  at default quality; a voice at half, 1.25 times and twice the output rate keeps the level its
  quality's conversion table gives, three ratios at each of the two qualities; a stereo circular
  buffer sink comes into use and records the final mix, the left channel first and the right second;
  a tone through the limiter switched off keeps its level, and in mode 1 comes out at the level of
  the limiter's rule, the threshold over the detector.

## How it asks the renderer

The renderer is opened through libnx's `audren` service at 48 kHz and at the revision libnx asks for
(4 on any firmware from 4.0.0; the report's first audio note gives the number), and the update is
written by `source/audio.c` rather than libnx's driver, which has no field for a voice's sample rate
conversion quality, no effects and no circular buffer sink. The layout is fixed for the run: a
final mix of two buffers feeding the device sink and two circular buffer sinks; a submix of four
buffers feeding the final mix at a gain the family sets, with a limiter and then an aux effect on
it. The aux records the submix's first two buffers into a ring the program reads, and returns into
the other two, which go nowhere.

Every tone loops over a whole second and fills each 240-sample frame with whole periods (1 kHz, 1.6
kHz, 11 kHz; the voices at other rates loop over 48000 of their own samples, whole periods too, and
come out at 10, 15 and 18 kHz), so once it has played 100 frames, every frame of a 40-frame ring
holds the same steady wave, which can be read in any order and even while the renderer writes it. A
level is one bin of a Fourier transform over the ring (`source/audioref.c`, pinned by the reference
suite). The resampler and limiter cases play into the submix at a gain of nought and are silent; the
circular buffer sink records the final mix, so its case plays at 0.005 of full scale, 46 dB down.

What each case catches, with Eden's numbers (reden `d304ea4164` against its audio fix branch):

- default quality at a ratio of one taking the table meant for ratios between 1 and 4/3, which
  low-passes every voice at the output rate: 11 kHz at 0.5 came out at 0.415 (-1.63 dB), and at
  0.498 with the tables swapped back. High quality is the control, 0.4995 on both.
- a stereo circular buffer sink that never comes into use, because the renderer compared the
  sink's state with a byte of the fourth channel: nothing recorded, the write offset left at 0. A
  renderer with the check right and the channels read three bytes early would record the left
  channel in both; the second case tells that apart.
- the limiter's processing mode read from past its parameters, so a limiter asked for mode 1 runs
  mode 0: 1 kHz at 0.25 came to 0.046 where the rule, threshold 0.1 over the detector, gives 0.157
  (-10.7 dB); with the mode read from its byte, 0.159. The rates are a hundredth: at a thousandth,
  a renderer that keeps the detector in 15 fractional bits drops the small steps near the average
  and lands 9% above the rule, which is not what this case asks. The limiter switched off is the
  control, and a note gives the level mode 0 reached.

**Through each table.** The renderer converts a voice to the output rate with one of three
tables per quality, picked by the ratio, the voice's rate over the output's: up to 1, below 1.3,
and above; 128 phases of 4 taps at default quality, of 8 at high. Each table is a windowed sinc of
three constants (the cut-off, a gain, and a Hamming window or a Kaiser one with its beta), found by
fitting the renderer's own tables: the six give every value to one unit of 1/32768, two in one
table (`source/audioref.c`, `AudioRefResampleTable`, its constants and the values it is held to in
the reference suite). With the tables the level of any tone at any ratio is a number to work out:
the family plays one tone per table and expects its level within 3%, and each line names what the
other two tables would give, so a renderer that picks the wrong table for a ratio is named by the
level it reached. The six were chosen where the three tables part by 10% or more; a renderer with
one table for every ratio fails four of them, whichever table it is.

| quality | ratio | tone | its table gives | the others give |
|---|---|---|---|---|
| default | 0.5 | 10 kHz | 0.3359 | 0.2270, 0.1065 |
| default | 1.25 | 15 kHz | 0.3850 | 0.4593, 0.2977 |
| default | 2 | 18 kHz | 0.3742 | 0.4981, 0.4396 |
| high | 0.5 | 10 kHz | 0.4023 | 0.1689, 0.0175 |
| high | 1.25 | 15 kHz | 0.4533 | 0.4997, 0.2503 |
| high | 2 | 18 kHz | 0.3693 | 0.4995, 0.4914 |

All six are at 0.5 of full scale. The model steps the phase as the renderer does, a 15-bit
fraction whose top 7 bits pick the phase (rEden 75c12299a6,
`audio_core/renderer/command/resample/resample.cpp:293` and `:843`; the table by ratio at
`:281-286` and `:831-836`). Under Eden all six came out at their table's level to the fourth
decimal, which is the model checked against a renderer that holds those tables.

What rests on reasoning rather than on a console run: that the console's mode 1 follows the rule
to 2% (Eden's mode 1 is a reciprocal estimate; Ryujinx divides), and that the console honours the
conversion quality and runs a limiter at revision 4. A console that ignored the quality would pass
the resampler cases all the same, since its default quality passes 11 kHz at a ratio of one.

Mode 0 has two answers, and no console has given one. Either the rule, the threshold over the
detector, as in mode 1: 0.157 for the case's tone. Or the threshold times 2 - a(2 - a r0), r0 the
detector's reciprocal estimated to 1/512 and rounded to 1/256: the two Newton steps Eden takes
(rEden 75c12299a6, `audio_core/renderer/command/effect/light_limiter.cpp:59` for the estimate,
`:94-96` for the steps). They converge on 2 - a rather than on 1/a, so the gain falls far below the
rule as soon as the detector passes the threshold (at 0.1001 it is 0.190 where the rule gives
0.999), and the tone comes out at 0.046, 10.7 dB under mode 1. `source/audioref.c` models both, the
reference suite pins them to values worked out apart, and the audio suite's note ends with which one
this machine's mode 0 is: `the rule`, `the two-step reciprocal` or `neither`. A console that says
the rule makes Eden's two lines one Newton step, r = r0 (2 - a r0).

## The listening sequence

**The listening sequence** (`listen` in the mode file, or the listen row of the start screen) is
about 40 seconds for a person, played before the cases through the final mix, so it is loud: peaks from 0.12 to 0.4 of full scale. Each part opens with
880 Hz beeps, one, two or three; inside a part, a low beep (440 Hz) comes before one take and a high
beep (1760 Hz) before the take it is compared with. The emulator's log carries an `EDENPROBE
LISTEN` line as each part starts.

- **a**, the resampler: four takes of noise bursts, a sweep from 4 to 16 kHz and a second of hiss,
  low beep default quality, high beep high quality. Where default quality low-passes, its takes are
  duller: 1.8 dB down over the noise, 3 dB over the hiss, and along the sweep from 0.2 dB at its
  start to 3.1 dB at 16 kHz. Where it does not, the four sound the same.
- **b**, the circular buffer sink: three low notes on the left, then three high notes on the right,
  recorded by a stereo circular buffer sink; then the recording's left channel on the left (low
  beep) and its right channel on the right (high beep). A sink that never comes into use leaves two
  silences, and a log line gives the recording's peak on each side.
- **c**, the limiter: four takes of three notes at 0.4 through the limiter at threshold 0.2, low
  beep mode 0, high beep mode 1. Where the mode byte is read, mode 1 is about 7 dB louder; where it
  is not, the four sound the same.

The screen shows the same things while they play, laid out as the running screen that comes before
and after it: the part and its name on the top row, then the step of the sequence (`step 3 of 16`)
with the take, a bar of the steps done in the running screen's colours, and what to listen for. Y
repeats that take. X skips to the next part, from the opening notes on to part a and from a part on
to the next, and from the closing notes it ends the sequence. With no press the sequence plays
straight through, and the `EDENPROBE LISTEN` lines are unchanged.

The recording only holds the phrase whole when the renderer runs at the speed of the sound it
plays. With the emulator's output engine set to null it renders as fast as it can, the ring wraps
while the phrase plays, and part b's recording is some turn of the phrase; the cases do not depend
on that, since they read steady tones. That same null engine is how to run the family without a
sound: `[Audio]` `output_engine\default=false` and `output_engine=3` in the emulator's
`sdl2-config.ini`.
