# The nvdec suite

## Index

* [What a VP9 job is](#what-a-vp9-job-is)
* [The setup and the table](#the-setup-and-the-table)
* [How a job is handed over](#how-a-job-is-handed-over)
* [The surfaces](#the-surfaces)
* [How a case passes](#how-a-case-passes)
* [The cases](#the-cases)
* [The open questions](#the-open-questions)
* [What Eden does](#what-eden-does)
* [The judge, pinned](#the-judge-pinned)

The second suite of [the video family](Video.md): the video decode engine, NVDEC, given VP9 jobs
built ready from six of libvpx's test vectors, one job per frame, and every image it decodes read
back off its surface and hashed against the MD5 list libvpx publishes for the vector. Only the word
`video` runs it, with the vic suite (see [The mode file](../ModeFile.md)), since no console has yet
shown that its jobs never hang.

- **nvdec**, seven cases. One says the channel opened, the buffer was mapped and what clock the
  engine got (none tier); the six after it each decode a whole vector and judge every image exactly.
  If the channel does not open, the six are skipped as `depends`.

## What a VP9 job is

The engine does not parse a VP9 stream. For each frame it reads a picture setup of 0x100 bytes
(NVIDIA's `nvdec_vp9_pic_s`, which reden's `PictureInfo` describes, `codec_types.h:322-383`), a
probability table of 0xEA0 bytes (`nvdec_vp9EntropyProbs_t`, reden's `EntropyProbs`,
`codec_types.h:385-464`) and the frame's body: everything after its uncompressed and compressed
headers. The table holds the probabilities the frame decodes with, which are the saved context it
starts from with its compressed header's updates applied; after a frame that adapts, that context
depends on everything the frame before it decoded, so only a decoder can say what it is. A frame
that shows an earlier one again (`show_existing_frame`) is no job: the case reads that frame's
surface itself.

The program carries the jobs ready, never a decoder. `tools/vp9-jobs.py` writes them into
`source/vp9jobs.c` from the vectors, running FFmpeg 9.0.2's VP9 decoder (`tools/vp9-ref.c`, built
from source by the script) one frame at a time and reading its state after each:

```sh
tools/vp9-jobs.py build-ref ffmpeg-9.0.2.tar.xz DIR   # checks the tarball's SHA-256, builds the helper
tools/vp9-jobs.py measure DIR/vp9-ref VECTORS         # the visibility table below
tools/vp9-jobs.py generate DIR/vp9-ref VECTORS        # source/vp9jobs.c and tools/vp9defaults.py
tools/vp9-jobs.py check                               # the committed jobs, with neither
```

Nothing the script works out alone goes into a job unchecked. It splits each WebM block's
superframe by its index (VP9 Annex B), parses every uncompressed header the way FFmpeg's
`decode_frame_header` keeps its fields (stale ones included, so the setup is the one built from
that state), and reads every compressed header with the specification's boolean decoder (6.3, 9.2);
each field must be FFmpeg's after the same frame, the updates applied to FFmpeg's saved context
must give FFmpeg's table on every byte the frame can read, and FFmpeg's images must be libvpx's
list. The vectors' SHA-256 are in the script; rebuilding the helper from the recipe gives the
committed files byte for byte. `tools/test_vp9_jobs.py` checks the committed file with neither
FFmpeg nor the vectors: every header parsed again from the frames the file carries, every setup,
tile list and surface worked out again, and the table of every frame whose context the script
knows by itself (37 of 85: the keyframes, the frame parallel case and the error resilient case),
and that a changed byte is caught.

**Why the visibility matters.** The engine is told whether the frame before was shown only in the
next job's setup (`LastShowFrame`), and the decoder needs it: VP9 uses the last frame's motion
vectors only when that frame was shown. FFmpeg, given each hidden frame rewritten as shown (the
setup has no other way to say it), against the same with the decoder told before each frame
whether the one before was shown, as `LastShowFrame` tells the engine:

| vector | the stream split | without visibility | with visibility |
|---|---|---|---|
| vp90-2-03-deltaq | 2/2 | 2/2 | 2/2 |
| vp90-2-02-size-66x66 | 10/10 | 1/10 (first wrong 2) | 10/10 |
| vp90-2-07-frame_parallel | 10/10 | 1/10 (first wrong 2) | 10/10 |
| vp90-2-09-lf_deltas | 30/30 | 1/30 (first wrong 2) | 30/30 |
| vp90-2-20-big_superframe-01 | 13/13 | 13/13 | 13/13 |
| vp90-2-10-show-existing-frame | 13/13 | 2/13 (first wrong 3) | 13/13 |

The error resilient vector never uses the last frame's motion vectors, so it cannot tell.

## The setup and the table

Both follow the homebrew FFmpeg backend for the console (nvtegra, ffmpeg-devel, May 2024,
`nvtegra_vp9.c`) field by field, from FFmpeg's state as it does:

- the setup: the stream length at 0x30, the bitstream decoder's control offset (the height to 64,
  times 912, in 256-byte units after the filter buffer) at 0x44, each reference's size and the
  surface's stride from 0x48, the frame's from 0x60, the flags at 0x68 (keyframe, last frame a
  keyframe, error resilient, `LastShowFrame` bit 4, intra only), the sign biases, the loop filter,
  the quantizer, the transform, filter and reference modes, the compound references, the tiles,
  the segmentation and the loop filter deltas. A reference slot that holds nothing yet gives
  nought, a keyframe no high precision, as there. The interpolation filter is the header's literal
  (smooth 0, regular 1, sharp 2, bilinear 3, switchable 4), which is how reden writes it back into
  the header it composes (`vp9.cpp:715`).
- the table: the keyframe mode tables, the segment tree and prediction probabilities (255 without
  a temporal update), and the frame's probabilities, with the prediction modes in the hardware's
  order (nvtegra's `pmconv`), the partition table's two halves (the keyframe one first, each 8x8
  first), and three coefficient probabilities in every four bytes. `ref_pred_probs` is nought:
  nvtegra fills it from a line buffer of its decoder, which is no probability, and neither the
  official driver nor reden reads it.

One pair of bytes differs on purpose. NVIDIA's header names 0x74 `qpChAc` and 0x75 `qpChDc`, and
reden reads them as the chroma AC and DC deltas (`codec_types.h:336-337`); nvtegra writes the DC
delta into the first and the AC delta into the second. Only a stream whose two chroma deltas
differ can tell, so the jobs follow the names, and vp9_two_frames, whose keyframe has a DC delta of
1 and an AC delta of 2, runs both ways (Q4).

## How a job is handed over

`source/nvdec.c` takes nvtegra's path on the console: nvdrv, nvmap and the fence service; the
channel `/dev/nvhost-nvdec` with a submit timeout of a second (which the official driver sets on the
decoder's channel only), its syncpoint, and the engine's clock asked of the multimedia service at
its highest (module 5, as nvtegra asks it, which libnx's header names `Nvenc`); one buffer of 1.25
MiB from `nvMapCreate`, not cacheable for the processor, mapped into the engine once with
`nvioctlChannel_MapCommandBuffer`. A refused timeout or clock does not stop the suite; the bring-up
line says so. Every address the engine is given is the mapping plus an offset, shifted right by 8:

| offset | what | size |
|---|---|---|
| 0x0000 | the picture setup | 0x100 |
| 0x0100 | the engine's status (`nvdec_status_s`) | 0x100 |
| 0x0200 | the command words | 0x400 |
| 0x0600 | the probability table | 0xEA0 |
| 0x1500 | the tile sizes, with the halfwords 0x37A (9) and 0x37B (1) nvtegra sets once | 0x700 |
| 0x1C00 | the counts the engine leaves (`nvdec_vp9EntropyCounts_t`) | 0x3400 |
| 0x5000, 0x5400 | two segment maps, one read and one written | 0x400 each |
| 0x5800 | the frame's body and 16 bytes of nought | 0xC000 |
| 0x11800, 0x19000 | two motion vector buffers, one written and one read | 0x7800 each |
| 0x20800 | the filter buffer (the height to 64, times 988) | 0x4D300 |
| 0x6E000 | up to five surfaces of the case's size | 0x28800 each at most |
| 0x138800 | 4 KiB no job is told about, which must still hold 0xEE after a pass | 0x1000 |

The sizes are nvtegra's for the largest case, 352 by 288. The command words (`Vp9Commands` in
`source/vp9ref.c`), each method through the host interface's METHOD0 and METHOD1 as the vic suite
sends them, in nvtegra's order, with each method's place in reden's `NvdecRegisters`
(`nvdec_common.h`):

| method | register | value |
|---|---|---|
| SETCLASS 0xF0 | | the decoder's class |
| 0x200 SET_APPLICATION_ID | 0x80 `set_codec_id` | 9, VP9 |
| 0x400 SET_CONTROL_PARAMS | 0x100 | 9, ERR_CONCEAL_ON (bit 6), GPTIMER_ON (bit 4) |
| 0x40C SET_PICTURE_INDEX | 0x103 | the job's number in the pass |
| 0x404, 0x408, 0x424 | 0x101, 0x102, 0x109 | the setup, the body, the status |
| 0x5C0, 0x5C4, 0x5D0 | 0x170, 0x171, 0x174 | the table, the counts, the tile sizes |
| 0x5D4, 0x5D8 | 0x175, 0x176 | motion vectors written and read: the two buffers swap every job |
| 0x5C8, 0x5CC | 0x172, 0x173 | segment map read and written: they swap after a job that updates the map |
| 0x5DC | 0x177 | the filter buffer |
| 0x430 + 4n, 0x474 + 4n | 0x10C + n, 0x11D + n | surface n's luma and chroma: last, golden, altref, then the frame's own |
| 0x300 Execute | 0xC0 | AWAKEN |
| NONINCR(0, 1), INCR_SYNCPT | | OP_DONE with the syncpoint |

Then `nvioctlChannel_Submit` with that one increment and `nvFenceWait` for at most a second: a job
that does not finish stops the pass and fails its case with the syncpoint and the value it never
reached, and the next case goes on.

## The surfaces

Each frame decoded gets a surface the generator chose: the lowest one no reference slot holds and
the frame before did not write, so nothing still needed is overwritten and the frame before can be
read again; a reference slot that holds nothing yet is the frame's own surface, as nvtegra hands
it. A surface is NV12 as nvtegra allocates one for the engine: rows of a multiple of 64 bytes, the
height a multiple of 32, the interleaved chroma plane right after the luma one, block linear in
blocks two gobs tall (the setup's tile format 0 and gob height 0). Reading one back copies it out of
the uncached buffer, takes each 16-byte run where the block linear rule says it is
(`CohBlockLinearOffset` with blocks of two gobs, which the reference suite holds to nvtegra's own
copy walk), crops it to the picture and splits the chroma into U and V: the MD5 of Y, U and V in
that order is the one libvpx's list has. Every surface holds 0xEE before a pass, so an image that is
still all 0xEE was never written.

## How a case passes

A case runs its frames in decode order, a job each, reading every image when its turn comes (right
after the job's fence, or when a frame shown again comes up) and again after the next job. It passes
when every image the vector shows is right at its turn (Q1 A), or else every image read a job later
was right then (Q1 B), and the pass did not stop; a case with a second form passes when either
form does, and the line says which. The line counts the images right, names the first one wrong
with what is wrong with it (never read and why, nothing written there, or which of Y, U and V
differ from FFmpeg's, with the engine's status when it reported an error), and says how many hidden
frames' surfaces held FFmpeg's picture at their fence (Q2):

```text
10 of 10 images right at their fence; hidden frames written 1 of 1 (fits Q1 A, Q2 A)
0 of 10 images right at their fence, 0 of 9 a job later; first wrong image 1 (frame 0): nothing was written there; hidden frames written 0 of 1
form A: 2 of 2 images right at their fence; form B: 0 of 2 images right at their fence, 0 of 1 a job later; first wrong image 1 (frame 0): Y right, U wrong, V wrong (fits Q1 A, Q4 A)
```

The lines above are what the judge says of reads made to order: every image right, nothing written
(Eden's, see below), and vp9_two_frames with the chroma deltas right only one way.

One note after the suite adds up what the passing cases answered, and names a question two cases
answered apart:

```json
{"type":"note","text":"nvdec: the passing cases answer Q1 A, Q2 A, Q3 A, Q4 A"}
```

No picture is left beside a failure: what each image must be is a hash, not pixels.

## The cases

The frames count from 0 in decode order, superframes split. SHA-256 by its first 16 digits.

| case | vector (SHA-256) | size | frames | hidden | images | what it measures | Eden |
|---|---|---|---|---|---|---|---|
| vp9_two_frames | vp90-2-03-deltaq.webm (c1ef6ca3eea22a53) | 352x240 | 2 | none | 2 | the job without a hidden frame; the keyframe's chroma deltas both ways (Q4) | nothing written |
| vp9_hidden_altref | vp90-2-02-size-66x66.webm (749e4260de357b27) | 66x66 | 11 | 1 | 10 | a hidden altref, contexts that adapt; below 144 by 144 (Q5) | nothing written |
| vp9_frame_parallel | vp90-2-07-frame_parallel.webm (b101f5f71920b893) | 352x240 | 11 | 1 | 10 | a hidden frame, no adaptation: every table known without a decoder | nothing written |
| vp9_hidden_lf_deltas | vp90-2-09-lf_deltas.webm (651fc818fea9687f) | 352x240 | 32 | 1, 15 | 30 | two hidden frames, loop filter deltas updated in fifteen frames | nothing written |
| vp9_error_resilient | vp90-2-20-big_superframe-01.webm (08c30e4e653e6f0b) | 352x288 | 20 | 1 to 7 | 13 | seven hidden frames out of one superframe, every frame error resilient | nothing written |
| vp9_show_existing | vp90-2-10-show-existing-frame.webm (21d60d305ed05f06) | 352x288 | 17, 8 shown again | 1, 2, 5, 10 | 13 | frames shown again; LastShowFrame both ways (Q3, two jobs differ) | nothing written |

## The open questions

Each has two answers. The cases' lines and the note say which fits.

- **Q1.** When the image is on its surface: A at its own job's fence; B only after the next job, as
  Eden delivers it (G1).
- **Q2.** A hidden frame's job: A writes the surface the job names, which then holds the picture;
  B does not.
- **Q3.** `LastShowFrame` after a frame shown again: A says whether the last frame decoded was
  shown, as the specification (7.2.6) and FFmpeg (`vp9.c:1605-1626`) have it; B is set, as if
  showing a frame again counted. vp9_show_existing runs both.
- **Q4.** Setup bytes 0x74 and 0x75: A the chroma AC delta then the DC one, as NVIDIA's header names
  them and reden reads them; B the other way round, as nvtegra writes them. vp9_two_frames runs
  both.
- **Q5.** A frame below 144 by 144: nvtegra refuses VP9 below that size before it asks the engine
  anything. vp9_hidden_altref, 66 by 66, passing says the engine decodes it; failing alone, with the
  other five passing, says the limit is the engine's.

## What Eden does

Read in reden at 75c12299a6. Every case should find nothing written: read in the code, not run.

- **G1, a frame of delay.** `src/video_core/host1x/codecs/vp9.cpp:437-448`: the frame of job N is
  kept and job N decodes frame N-1, its `show_frame` taken from job N's `LastShowFrame` (`:442`).
  The image of frame N exists only after job N+1, and the last frame of a stream is never decoded.
- **G2, every new frame shown.** `src/video_core/host1x/codec_types.h:375`: `show_frame = true`
  when the setup is read; only the frame G1 keeps gets the right value.
- **G3, probabilities against the table before.** `vp9.cpp:455` and the compressed header after it
  write each probability as an update against `prev_frame_probs`, the engine table of the last
  shown frame that was not a keyframe; FFmpeg applies an update to the context it adapted after the
  frame before, and a probability with no update keeps its adapted value. The table is the engine's
  only where adaptation changed nothing (frame parallel, error resilient). Read, not measured.
- **G4, the compositor takes the frame from the decoder's queue.**
  `src/video_core/host1x/codecs/decoder.cpp:38` and `:53-54` (a hidden frame gets no output
  offsets, `src/video_core/host1x/ffmpeg.cpp:443`), `src/video_core/host1x/host1x.h:87` and
  `src/video_core/host1x/vic.cpp:126-127`. Nothing on that path writes the surface in the game's
  memory: `decoder.cpp`, `ffmpeg.cpp`, `vp9.cpp` and `nvdec.cpp` make no write to guest memory, only
  `vic.cpp` does. So every surface keeps its 0xEE, and every case says "nothing was written there"
  at its first image.

The rest of the way opens: `/dev/nvhost-nvdec` (`core/hle/service/nvdrv/nvdrv.cpp:152`) answers
Submit, GetSyncpoint, SetSubmitTimeout and MapBuffer (`devices/nvhost_nvdec.cpp:29-38`), the
multimedia service's Initialize, SetAndWait and Get are stubs that succeed
(`core/hle/service/mm/mm_u.cpp:59-62`), and the decoder is made by SET_APPLICATION_ID
(`video_core/host1x/nvdec.cpp:41-42`). The bring-up should pass; the frames each decoded go into a
queue of at most 200 (`host1x.h:124`) that no VIC job here reads.

## The judge, pinned

The `reference` suite holds `source/vp9ref.c` and the generated jobs to values worked out apart from
them, in both builds: every job fits the buffer and its surface is the one its setup names, applying
the patches gives the tables the script wrote (by FNV), the surface layout of two sizes by hand, the
question each case is built both ways for and the bytes the second form changes, a job's command
words word by word against values worked out by hand from `clc5b0.h` and reden's register places,
the next job's swapped buffers, the block linear reading against nvtegra's copy walk restated, a
picture written block linear read back as its planes' MD5s, and the judge on reads made to order
(all right, nothing written, a job late, the chroma wrong, a job that never finished, two forms,
and the note). Mutations of the block height, the patching, the motion vector swap and the chroma
swap each fail it. The MD5 itself is held to RFC 1321's suite (see [Credits](../Credits.md)).
