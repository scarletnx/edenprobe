# The blit2d suite

## Index

* [The setup](#the-setup)
* [The cases](#the-cases)
* [How a case passes](#how-a-case-passes)
* [The open questions](#the-open-questions)
* [What Eden does](#what-eden-does)
* [The judge, pinned](#the-judge-pinned)

The 2D engine, class 0x902D, copying a block linear surface into a window of another at half and
at two thirds size, from each sample origin. Part of the graphics family, after dma, so it runs by
default and is timed like the drawing suites: a warm-up and eight rehearsals before the verdict
pass, each opening a device and surfaces of its own.

- **blit2d**, five cases. One says the device came up with both surfaces laid out as the methods
  describe them (none tier); the four after it make one copy each and judge every pixel of the
  destination twice (exact tier). If the first fails, the four are skipped as `depends`.

The graphics library sends this class only for its own blits, with an origin it picks, so the
cases write its methods raw. What they catch: a scaled copy sampling the wrong texel, a copy that
writes more than its window when the window starts and ends inside a gob's 16 byte runs, and a copy
an emulator keeps in a cache of its own instead of the memory it was asked to write.

## The setup

- **The source**, 128 by 32 A8B8G8R8, block linear in blocks two gobs tall, 16384 bytes. Each
  texel is the little endian word `0x10000000 | y << 12 | x`, written by the processor at the
  address the block linear rule gives (`CohBlockLinearOffset`), so every pixel of a copy says which
  texel it took.
- **The destination**, one per case so no case reads another's copy: 64 by 16, the same format and
  blocks, 4096 bytes, filled with 0xEE by the processor before the copy.
- **The window**, 21 by 9 at (5, 3): 189 pixels inside, 835 outside. Each of its rows starts at
  byte 20, four bytes into the second 16 byte run of a gob, and ends at byte 104, eight bytes into
  the seventh; its rows, 3 to 11, cross the gob border at row 8. A copy that writes whole runs,
  whole gobs or whole blocks changes pixels outside it.
- Both surfaces are the library's images (`DkImageFlags_CustomTileSize` with
  `DkTileSize_TwoGobs`, and `DkImageFlags_Usage2DEngine`) in the processor-visible image block,
  uncached on the processor's side. The first case checks that the library laid them out in the
  sizes the rule gives, 16384 and 4096 bytes, since the methods describe them by the rule.

The words, `Blit2dRawWords` in `source/blit2dref.c`, 38 of them for subchannel 3, where the library
binds the class when it makes a queue (deko3d `source/maxwell/helpers.h` and `gpu_base.cpp`,
`setupEngines`), in the order its own blits write them (`source/maxwell/gpu_transfer.cpp`,
`Blit2DEngine`). Method numbers are words; NVIDIA's `cl902d.h` has them in bytes, four times over.

| words | method | values |
|---|---|---|
| 1 | SET_OPERATION, 0x0AB, inline | 3, SRCCOPY |
| 1 | SET_CLIP_ENABLE, 0x0A4, inline | 0 |
| 5 | SET_SRC_FORMAT, 0x08C, and the three after it | 0xD5 (A8B8G8R8), 0 (block linear), 0x10 (blocks two gobs tall), 1 (depth) |
| 5 | SET_SRC_WIDTH, 0x092, and the three after it | 128, 32, the address high word first |
| 6 | SET_DST_FORMAT, 0x080, and the four after it | 0xD5, 0, 0x10, 1, 0 (layer) |
| 5 | SET_DST_WIDTH, 0x086, and the three after it | 64, 16, the address high word first |
| 1 | SET_COMPRESSION, 0x0B5, inline | 1, as the library writes it |
| 1 | SET_PIXELS_FROM_MEMORY_SAMPLE_MODE, 0x223, inline | the origin in bit 0, 0 centre and 1 corner; the filter in bit 4, 0 point |
| 13 | SET_PIXELS_FROM_MEMORY_DST_X0, 0x22C, and the eleven after it | 5, 3, 21, 9; du/dx and dv/dy in 32.32, low word first (2.0 is 0 and 2, 1.5 is 0x80000000 and 1); the source origin, 0.0 and 0.0. The twelfth, PIXELS_FROM_MEMORY_SRC_Y0_INT at 0x237, launches the copy |

What the library sets once per queue stays as it left it: the clip off, the corral size and the
alpha factor (`gpu_transfer.cpp`, `setupTransfer`). Word 0x090 is never written: where the
destination has its layer, the source has TWOD_INVALIDATE_TEXTURE_DATA_CACHE (`cl902d.h`), and no
layer at all.

A case records the pass the drawing suites open (`GpuBeginPass`, which drops the texture, shader
and descriptor caches), the words, and a full barrier, then submits and drains: `dkQueueWaitIdle`,
whose fence asks for the caches to be flushed (deko3d `source/dk_queue.cpp`, `Queue::waitIdle`).
Then the two reads, in this order: the processor copies the 4096 raw bytes out of the destination,
then the copy engine copies the destination into rows (`GpuReadBackImage`, `CopyToReadback` in
`source/gpu.c`). The raw bytes first, because the copy to buffer is itself a read an emulator may
answer from its cache, and might write the cache back to memory as it does.

## The cases

| case | origin | du/dx and dv/dy | pixel (5 + i, 3 + j) takes, under Q1 A and Q2 A | MD5 of the 4096 bytes |
|---|---|---|---|---|
| blit2d_corner_half | corner | 2.0 | texel (2i, 2j) | `84fd46bdbeba5e70f758996dd896cf84` |
| blit2d_center_half | centre | 2.0 | texel (2i + 1, 2j + 1) | `1654028909d43464ff4556fbf1802371` |
| blit2d_corner_2_3 | corner | 1.5 | texel (floor(1.5i), floor(1.5j)) | `3ee33409edbf6bf600caa6435dccd6ea` |
| blit2d_center_2_3 | centre | 1.5 | texel (floor(1.5i + 0.75), floor(1.5j + 0.75)) | `6560d24df99ce3fb20477313332d5d18` |

Under Q1 B the two origins trade places: each half case gives the other's image, and each 1.5 case
the other's. Under Q2 B both 1.5 cases give `ff261b6480a54b7fd8b2ff3479cb7352`, x texels 0, 2, 3,
5, 6, 8, 9, 11 and y texels 0, 2, 3, 5, 6, 7, 9, 10; at 2.0 Q2 changes nothing. The destination as
filled is `85a0bd9e5c984f5d1786e9309dd7930e`.

## How a case passes

A case passes when one combination of answers to Q1 and Q2 explains every pixel of both reads:
the 189 inside the window and the fill on the 835 outside. Each read is judged against all four
images (`Blit2dJudge`). On a pass the line gives the answers that fit and the raw read's MD5:

```text
fits Q1 A, Q2 A: raw MD5 3ee33409edbf6bf600caa6435dccd6ea, all 189 right and nothing outside the window changed in both reads
```

On a failure it gives, for each read: what the read holds (the fill with nothing written, the
answers it fits, or no answer's image), how many window pixels are right against the answers named
at the end, the first wrong pixel row by row with its value and the texel that value is (or the
fill, or no texel) and the texel wanted, how many pixels outside changed, and for a read no answer
explains, the texels the window's first eight pixels across and down took. Eden, as read below:

```text
raw MD5 85a0bd9e5c984f5d1786e9309dd7930e; raw: the destination as filled, nothing written; copy to buffer: fits Q1 either, Q2 B, 189 of 189 right, nothing outside the window changed; counted against Q1 A, Q2 B
```

A failing case leaves a picture of the read that went wrong, 64 by 16 in bits: the copy to buffer,
or the raw bytes put in rows when the copy to buffer was right and they were not.

## The open questions

- **Q1.** What the origins mean when the copy scales: A the corner origin samples the window's
  pixel n at n du and the centre origin at (n + 1/2) du, as the table; B the other way round. The
  two half cases answer it together. A console that gives texel 2i in both, as the other side's
  emulator does, makes the corner cases fit A and the centre cases fit B: the note then says the
  origin bit changes nothing.
- **Q2.** A scale with a fraction: A the engine steps du/dx in 32.32 pixel by pixel, as the table;
  B what Eden does (G2 below). The 1.5 pair answers it; a third rule fails both, with its texels on
  the line.

One note after the suite adds up what the passing cases answer, and when a case failed, what the
copies to buffer of the failing ones answer:

```json
{"type":"note","text":"blit2d: the passing cases answer Q1 A, Q2 A"}
```

## What Eden does

Read in reden at 75c12299a6, `src/video_core/` unless said otherwise, not run. Every case should
fail on the raw read while its copy to buffer fits, the half cases Q1 A and the 1.5 cases Q2 B:

```json
{"type":"note","text":"blit2d: no case passed; the copies to buffer answer Q1 A, Q2 B"}
```

- **G1, the scaled copy goes to the graphics unit.** The copy runs on the write of word 0x237
  (`engines/fermi_2d.cpp:43-45`); a du/dx or dv/dy other than 1.0 sets `must_accelerate`
  (`:84-88`); `AccelerateSurfaceCopy` comes first (`:112-115`), and with `must_accelerate`
  `GetBlitImages` never gives up (`texture_cache/texture_cache.h:1772-1784`), so the copy becomes a
  draw into the cache's image of the destination (`renderer_vulkan/vk_texture_cache.cpp:1270-1273`
  calls `BlitColor`, `renderer_vulkan/blit_image.cpp:661-677`): viewport and scissor on the window
  (`:402-426`), the span in texels (`:428-442`, with no source size at `:734`), and a nearest
  sampler on unnormalised coordinates, clamped to a border (`:283-302`), sampled at each pixel's
  centre (`host_shaders/full_screen_triangle.vert:30-35`, `blit_color_float.frag`).
- **The raw read.** Nothing writes that image back to memory: the copy marks it modified
  (`texture_cache.h:712-713`, `:2504-2519`); a download is queued only for an image copied to a
  buffer (`:1093-1097`) and for a render target bound over one that asked for it (`:2629-2634`);
  and the drain's cache flush is read and dropped (`engines/maxwell_3d.cpp:588-592`). Unless the
  processor's read itself makes Eden write the image back, the raw bytes are the fill.
- **G2, the corner origin and whole texel ends.** With the corner origin the start moves back by
  `(du_dx >> 33) << 32`, half of du/dx cut to a whole texel (`fermi_2d.cpp:79-82`), and both ends
  of the source span become whole texels (`:93-96`), so the scale drawn is (end - start) / width,
  not du/dx. At 2.0 that cancels against the centre sample: start -1, end 41, texel 2i for the
  corner; start 0, end 42, texel 2i + 1 for the centre. Each of those samples lands exactly on the
  edge between two texels, so one the host's interpolation brings in a hair low takes the texel
  before. At 1.5 it does not cancel: start 0, end floor(1.5 x 21) = 31 across and 13 down, 31/21
  and 13/9, the same for both origins.
- **G3, the software copy is not reached.** `engines/sw_blitter/blitter.cpp:146-238` runs only when
  `AccelerateSurfaceCopy` refuses, which G1 does not with a scale (the null renderer's accepts and
  does nothing, `renderer_null/null_rasterizer.cpp:88-92`). Were it reached, the corner start of
  -1 would go to the unswizzle as an unsigned origin (`:205-207`) and the copy would sample at
  each pixel's corner (`:31-47`), texel 2i - 1; and with a fraction in dv/dy the same loop adds the
  row's fraction times the width to the column it reads (`(src_y * src_width + src_x) >> 32`).
- Eden reads word 0x090 as the source's layer, one structure serving both surfaces
  (`engines/fermi_2d.h:85-104`), and logs a copy whose value there is not nought
  (`fermi_2d.cpp:65`); no case writes it.

## The judge, pinned

`tools/blit2d-expect.py` works the cases out apart from `source/blit2dref.c`: there a gob is a
permutation of address bits and the rules are exact fractions, in the C the arithmetic of
`CohBlockLinearOffset` and 32.32 steps; the command words come from `cl902d.h`'s byte offsets, the
C's from deko3d's word numbers. It prints the texels each rule picks, the MD5 of every case under
every combination, and whether the hashes agree with the table above: all six
do. `tools/test_blit2d_expect.py` holds the C table to what the script prints.

The `reference` suite has seven cases on it, in both builds: the source and the filled
destination, the sixteen images and the 38 words against the script's table; then the judge on
reads made to order: each case's own image passes and names its answers, a copy kept in a cache
fails on the raw read with both reads named, a pixel written outside the window or a third rule
fails with its texels, and the note from passing cases or from the copies. Mutations of the
origin's half step, Eden's start and sample, the block height, the compression and address words,
which reads must fit, the window bounds, the outside count, the answers' wording, the texel a value
is and the note each fail it.
