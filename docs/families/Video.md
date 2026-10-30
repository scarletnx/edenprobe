# The video family

The video image compositor, the VIC, is the engine that converts, scales and composites video
frames. The family hands it input the processor wrote, not frames the decoder made, and reads back
what it left byte by byte; the decoder has a suite of its own, [nvdec](Nvdec.md). Only the word
`video` runs the family (see [The mode file](../ModeFile.md)), since no console has yet shown that
its jobs never hang; the start screen has no row for it, and a file that names it keeps it whatever
the screen changes.

- **vic**, twenty cases. One says the channel opened and the buffer was mapped (none tier); the
  nineteen after it each run one job, or a few, and judge every byte of the output. If the channel
  does not open, the nineteen are skipped as `depends`. The rest of this page is about it.
- **nvdec**, seven cases: the video decode engine on VP9 jobs built from six of libvpx's test
  vectors, every image hashed against libvpx's list. See [The nvdec suite](Nvdec.md).

## How a job is handed over

`source/vic.c` takes the path the homebrew FFmpeg backend (nvtegra, ffmpeg-devel, May 2024) takes
on the console: nvdrv, nvmap and the fence service; the channel `/dev/nvhost-vic` and its
syncpoint (`nvioctlChannel_GetSyncpt`); one buffer of 64 KiB from `nvMapCreate`, not cacheable for
the processor, so no flush or invalidate is needed either way, mapped into the engine once with
`nvioctlChannel_MapCommandBuffer`. Every address the engine is given is that mapping plus an
offset, shifted right by 8:

| offset | what |
|---|---|
| 0x0000 | the configuration structure, 0x610 bytes |
| 0x0800 | the command words |
| 0x1000, 0x2000 | slot 0's luma (256 by 16) and chroma (256 by 8) |
| 0x3000, 0x4000 | slot 1's |
| 0x5000 | the output, 16 KiB: an RGBA surface's 16 rows at a pitch of 1024, or NV12's luma and chroma |
| 0x9000 | 4 KiB nobody is told about, which must still hold 0xEE after every job |

The command words (`VicCommands` in `source/vicref.c`), each method through the host interface's
METHOD0 and METHOD1 registers as `INCR(0x10, 2)`, the method's offset over four, the value:

| words | what |
|---|---|
| SETCLASS 0x5d | the VIC's class |
| 0x704 SetControlParams | `(0x610 / 16) << 16`, plus GPTIMER_ON (bit 0) and FALCON_CONTROL (bit 8) |
| 0x708 SetConfigStructOffset | the structure |
| 0x720, 0x724 | the output's luma, and its chroma when the output is NV12 |
| 0x400 + 0x60 n, + 4 | slot n's luma and chroma |
| 0x300 Execute | `1 << 8`, AWAKEN |
| NONINCR(0, 1), INCR_SYNCPT | `1 << 8` (OP_DONE) with the syncpoint in bits 7..0 |

Then `nvioctlChannel_Submit` with that one increment, and `nvFenceWait` on the fence it hands
back, for at most a second: a job that does not finish fails its case with the syncpoint and the
value it never reached, and the next case goes on.

Where the facts come from: the structure's bit positions and the method offsets are libdrm's
`tests/tegra/vic40.h` and `vic40.c` (NVIDIA, MIT), which reden's `src/video_core/host1x/vic.h`
agrees with, and the INCR_SYNCPT condition's place is libdrm's `tegra/channel.c` (shift 8 for
version 0x21). What libdrm does not cover comes from nvtegra's patches: the NV12 chroma fields,
the control parameters' two bits, and the pipe's downsample of one (4 in U9.2), all of which it
sends on the console. Three things differ from libdrm's sequence on purpose, because nvtegra's is
the one known to run on a console: no SET_APPLICATION_ID (libdrm sends 1 at 0x200; nvtegra sends
none, and its class header names 0 the compositor's), the two control bits, and the downsample.

## The common setup

Surfaces of 16 by 16, pitch linear, 256 pixels to a row, as libdrm aligns them. The output is
filled with 0xEE before each job, and no case expects 0xEE anywhere the engine writes. One slot,
unless a case says two. Every field a case does not set is nought.

- **The output:** A8B8G8R8 (31), target rect 0..15 both ways (inclusive, as libdrm writes it), a
  background of nought, the alpha fill Opaque.
- **The slot:** enabled, the current field, progressive, soft clamp 0..1023, planar alpha 1023
  with ConstantAlpha set, source rect 0..15 both ways in 16.16, destination rect 0..15.
- **The slot's surface:** format 68 (`Y8___V8U8_N420`), pitch, cache width 64Bx4, luma 255 by 15
  and chroma 127 by 7, each minus one: the chroma plane in pairs and at half the height, the
  formula nvtegra uses.
- **The input:** a luma plane with Y = 16 + 14 x (16, 30, ..., 226) down every column, and a
  chroma plane of pairs with 90 in the even byte and 240 in the odd one, U and V by the first
  hypothesis of Q11.
- **The matrix**, where a case turns it on: BT.601 in S12.8, rows R, G and B of `{298, 0, 409,
  -228480}`, `{298, -100, -208, 138624}` and `{298, 516, 0, -283264}`, shift nought. Per channel
  in ten bits (Y, U and V shifted left by 2): the three products summed, shifted right, the
  constant column added, shifted right by 8, clamped to the soft clamp, and a byte is the top 8
  bits.

## How a case passes

Every case is the exact tier: the VIC's arithmetic is integer. What each byte must be is
`VicModel` in `source/vicref.c`, under one answer to each open question the case can see; a case
passes when one combination of answers explains every byte of every job, over the whole 16 KiB
output and not only the surface. The line then says which answers fit, `fits Q9 either, Q11 A`
when a case cannot tell one question's two answers apart. A failure says how many bytes are wrong
under the closest answers and the first of them, and, when the engine wrote nothing at all, that
first; a drawing-style picture of an RGBA job 16 wide is left beside it. One note after the
family adds up what the passing cases answered, and names a question two cases answered apart:

```json
{"type":"note","text":"vic: the passing cases answer Q2 A, Q7 A, Q11 A"}
```

When the console answers a question, its case can come to accept only that answer: a change of
rule, never a stored value.

## The cases

| case | the job, over the common setup | what it expects | open on |
|---|---|---|---|
| vic_passthrough | nothing changes | each pixel Y, 90, 240, 255 | Q11 |
| vic_chroma_order | slot format 67 | A: Y, 240, 90, 255; B: Y, 90, 240, 255 | Q8, Q11 |
| vic_order_argb | output A8R8G8B8 (32) | each pixel 240, 90, Y, 255 | Q11 |
| vic_order_x8 | output X8B8G8R8 (35), alpha fill SourceStream slot 0, planar alpha 512 | Y, 90, 240, then A: 128 or B: 255 | Q2, Q11 |
| vic_matrix_601 | the matrix, six jobs of one colour each | the table below | Q9, Q11 |
| vic_matrix_shift | the matrix's first three columns times 4, shift 2, colour 126, 128, 128 | A, the shift before the constant: 128, 128, 128; B, after it: 255, 26, 255 | Q3, Q9 |
| vic_soft_clamp | the matrix, clamp 64..940, SourceStream slot 0; 235, 128, 128 and 16, 128, 128 | 235 three times and 16 three times; alpha A: 235 or B: 255 | Q4, Q9 |
| vic_clamp_nomatrix | no matrix, clamp 64..940; Y 0 and Y 255 over U and V 128 | A, the clamp holds: 16 and 235; B: 0 and 255; U and V 128 in both | Q10, Q5, Q4 |
| vic_alpha_opaque | Opaque, planar alpha 512 and then 0 | the fourth byte A: 255 both times; B: 128 and 0 | Q5, Q11 |
| vic_planar_alpha | SourceStream slot 0, planar alpha 1023, 512, 0 | the fourth byte 255, 128, 0 | Q11 |
| vic_target_clip | target rect 4..11 both ways | 4..11 the image's columns 4..11; outside A: 0xEE, B: the background | Q7, Q11 |
| vic_target_clip_matrix | the above with the matrix and 126, 128, 128 throughout | 4..11 128, 128, 128, 255; outside as above | Q7, Q9 |
| vic_target_odd | target rect from column 5 | columns 5..15 the image's; 0..4 as outside | Q7, Q11 |
| vic_background | slot disabled, target rect 2..13, background R 1023, G 512, B 0, A 1023 | inside 255, 128, 0, 255; outside as above | Q7 |
| vic_background_ring | source and destination rect 4..11, the background above | 4..11 the image's columns 4..11, the ring 255, 128, 0, 255 | Q11 |
| vic_source_crop | source rect 8..15 across, destination and target rect 0..7 across | column k the image's 8 + k (Y = 128 + 14 k); 8..15 as outside | Q7, Q11 |
| vic_two_slots | slot 0 Y 60 throughout; slot 1 Y 180, source and destination 4..11; U and V 128 | 4..11 A: slot 1, B: slot 0; the rest slot 0 | Q6 |
| vic_pitch | output surface 100 wide, rects 0..15 | row 1 at byte 1024, not 512; columns 16..99 as outside; bytes 400..1023 of each row, past the surface, 0xEE | Q7, Q11 |
| vic_nv12_out | output 68, chroma 127 by 7 at 0x724 | Y, U and V out as they went in | none |

The colours of vic_matrix_601, YUV in and R, G, B out, worked out by `tools/vic-expect.py`:

| YUV | A, truncated | B, 128 added before the last shift |
|---|---|---|
| 235, 128, 128 | 254, 254, 254 | 255, 255, 255 |
| 16, 128, 128 | 0, 0, 0 | 0, 0, 0 |
| 126, 128, 128 | 128, 128, 128 | 128, 128, 128 |
| 81, 90, 240 | 254, 0, 0 | 254, 0, 0 |
| 145, 54, 34 | 0, 255, 1 | 0, 255, 1 |
| 41, 240, 110 | 0, 0, 254 | 0, 0, 254 |

Left out for now: scaling, where the filter decides the values (every source rect here is the
size of its destination rect), block linear output, interlaced input, and blending with factors.

## The open questions

Each has two answers, A and B, and every case open on it accepts either. Q1 has one: without the
matrix Y, U and V are copied into R, G and B, which every case without the matrix assumes.

- **Q2.** X8B8G8R8's fourth byte: A the alpha, B 255.
- **Q3.** The matrix shift: A before the constant column is added, B after.
- **Q4.** The soft clamp: A takes the alpha too, B leaves it.
- **Q5.** Alpha fill Opaque: A 1023 throughout, B the alpha the slots and the background left.
- **Q6.** Two slots over each other with blending off: A the higher slot on top, B slot 0.
- **Q7.** Outside the target rect: A untouched, B the background colour, alpha included.
- **Q8.** Format 67: A the chroma order the other way round from 68, B the same as 68.
- **Q9.** The matrix: A truncates, B adds 128 before the shift by 8.
- **Q10.** Without the matrix: A the soft clamp holds, B it does not.
- **Q11.** Format 68: A U in each pair's even byte (NV12), B V in it. New: nvtegra hands NV12 to
  the engine as format 67, `Y8___U8V8_N420`, which by NVIDIA's names is the other order; its
  transfer copies NV12 to NV12, so either reading of the bytes works for it, and the question
  stays open.

## What Eden does

Read in reden at 75c12299a6, `src/video_core/host1x/vic.cpp` unless said otherwise. With the
default NVDEC emulation (`Gpu` in reden, `common/settings.h:540`; only `Off` takes another path,
which paints the whole surface black on every job, 151-153) every case finds nothing written,
because of G1.

- **G1, input only from the decoder.** `Execute` (112-153) looks each slot's frame up in the
  decoder's frame queue by the slot's luma offset; with no frame the slot is not read, and when no
  job has ever decoded one the job returns at 149 without writing. Input the processor wrote never
  reaches the VIC.
- **G2, the whole surface rewritten.** `WriteABGR` (927 on) writes the output surface's width by
  height every job, from a buffer only resized (118), never cleared: outside the target rect and
  the slots goes stale data or nought. vic_target_clip, vic_target_odd and vic_background see it.
- **G3, no background.** The background fields (`vic.h:235-238`) are never read; libdrm's fill job
  (`vic40_fill`) is a target rect and a background colour and nothing else.
- **G4, one added to every rect edge above nought.** `Blend` (457-474) adds one to left, top, right
  and bottom; with inclusive right and bottom edges that lands, but a target rect from column 5
  starts at 6 (vic_target_odd).
- **G5, the source rect without its fraction.** libdrm writes the source rect in 16.16; `Blend`
  (461-464) takes the raw 30 bits. From column 8 the left edge is past 500000: with the matrix on
  nothing is drawn, without it `source_right - source_left` wraps (685) and the copy (689) reads
  about 4 MiB past the slot's buffer. vic_source_crop would end Eden once G1 is mended, so it goes
  into the may-crash table at that point, until G5 is mended too.
- **G6, no scaling and no blending.** A destination rect larger than its source is copied or cut,
  a slot with an alpha below the top does not blend (496), and the alpha fill mode (`vic.h:233`,
  0 Opaque at `vic.h:140`) is ignored: the alpha out is always the slot's planar alpha.
- **G7, the matrix adds the offset twice.** Both paths with the matrix take `src` already holding
  `source_left` and `dst` already holding the target's left edge, and index with `x`, which starts
  at `source_left` (SSE 551-561, plain 672-675). vic_target_clip_matrix sees it.

With the matrix on, Eden clamps all four channels, the alpha included (676-679; SSE 607-621), so
it answers Q4 A; without the matrix it clamps nothing (684-690), Q10 B. The channel is
`/dev/nvhost-vic` (`core/hle/service/nvdrv/nvdrv.cpp:160`), the class 0x5d
(`video_core/cdma_pusher.h:51`) and the host interface's methods `ThiMethod`
(`cdma_pusher.h:101-104`).

## The judge, pinned

The `reference` suite holds `source/vicref.c` to `tools/vic-expect.py`, which works the same
things out with no code in common: thirty cases, run in both builds. The BT.601 colours, the
shift's two orders and the clamp by value; the passthrough job's structure word by word against
values worked out by hand from vic40.h; every case's structure and every case's expected output
under each combination of its answers by hash; the command words of a one-slot job word by word
and of two slots and an NV12 output by hash; and the judge itself, on outputs made to order (each
case's own model passes, nothing written and one byte off fail, and say so). The script's packing
was checked once against libdrm's vic40.h structure, filled field by field and compiled. The tools'
tests hold the C table to the script's output. The commands:

```sh
tools/vic-expect.py         # the tables, the colours, and whether they agree with the asked table
tools/vic-expect.py --c     # the table source/tests/t_vicref.c holds
```
