# Credits

- **switch-examples**, https://github.com/switchbrew/switch-examples: `source/viewer.c` reads the
  pad the way `hid/read-controls` does (`padConfigureInput`, `padInitializeDefault`, `padUpdate`,
  `padGetButtonsDown`, `padGetButtons`) and drives a linear, double buffered framebuffer the way
  `graphics/simplegfx` does. Its README marks the examples with the Creative Commons Public Domain
  Mark 1.0; the repository has no licence file of its own.
- **libnx**, https://github.com/switchbrew/libnx: the library every call to the console's services
  goes through. ISC licence, in its `LICENSE.md` (copyright 2017-2018 libnx Authors).
- **deko3d**, https://github.com/devkitPro/deko3d: the graphics library the drawing suites use. zlib
  licence, in its `LICENSE` (copyright 2018-2020 fincs).

- **deko3d's internals and its compute example**: `source/gpu.c` `GpuRawMethods` builds a one-entry
  control list in the shape of deko3d's `source/dk_ctrlcmd.h` (`CtrlCmdHeader`,
  `CtrlCmdGpfifoEntry`), and `source/rawref.c` encodes method headers the way its
  `source/maxwell/command.h` does, with the method numbers of `source/maxwell/engine_3d.def`
  (deko3d 350f2b0, zlib licence). The order suite's pattern, a dispatch, a primitives barrier and
  a draw of what it wrote, is switch-examples' `deko_examples/source/Example09_SimpleCompute.cpp`
  and `sinewave.glsl`, which `order_copy.comp.glsl` follows in shape. `source/blit2dref.c` writes
  the 2D class's methods with the numbers of `source/maxwell/engine_2d.def`, on the subchannel
  `source/maxwell/helpers.h` names for it, in the order `source/maxwell/gpu_transfer.cpp`
  (`Blit2DEngine`) writes the library's own blits.
- **NVIDIA's open-gpu-doc**, https://github.com/NVIDIA/open-gpu-doc (MIT, copyright 2003-2004
  NVIDIA Corporation, in the header itself): `tools/blit2d-expect.py` takes the 2D class's method
  offsets from `classes/twod/cl902d.h` (9fdf5c4), to check the C's words against a second source.
  None of its text is copied.

- **astcenc**, https://github.com/ARM-software/astc-encoder (Apache 2.0): ARM's reference ASTC
  codec. None of its code is in this repository; `source/astcblocks.c` carries hashes of what its
  decoder (commit 9fbec680) makes of each block the astc suite samples, and `source/astcref.c`
  follows its reading of two points the specification leaves to the decoder: bits past the end of
  an integer sequence read as nought, and an HDR endpoint mode in the LDR profile is the error
  colour on its own partition only.

- **libvpx's test vectors**, https://storage.googleapis.com/downloads.webmproject.org/test_data/libvpx/
  (BSD-3-Clause, copyright 2010 The WebM Project authors): `source/vp9jobs.c` carries the frames of
  six of them (`vp90-2-03-deltaq`, `vp90-2-02-size-66x66`, `vp90-2-07-frame_parallel`,
  `vp90-2-09-lf_deltas`, `vp90-2-20-big_superframe-01`, `vp90-2-10-show-existing-frame`) and the MD5
  list libvpx publishes beside each; `tools/vp9-jobs.py` names each vector's SHA-256.
- **FFmpeg**, https://ffmpeg.org (LGPL-2.1-or-later), release 9.0.2: `tools/vp9-ref.c` links its VP9
  decoder, built from source by `tools/vp9-jobs.py build-ref`, and reads its state after each
  frame; the tables in `source/vp9jobs.c` and the defaults in `tools/vp9defaults.py` are what it
  printed. `tools/vp9-jobs.py` parses the uncompressed header the way `libavcodec/vp9.c`
  `decode_frame_header` does, keeping the same fields stale, so its setup matches the one built
  from that state. The compressed header is read after the VP9 specification (v0.6, 6.3 and 9.2).
- **nvtegra**, the homebrew FFmpeg backend for the console (averne, ffmpeg-devel, May 2024,
  GPL-2.0-or-later, with NVIDIA's `nvdec_drv.h` and `clc5b0.h` under MIT). Read: the patches for
  the ioctls, the hardware definitions, the common code, the hwcontext and its clock scaling, the
  common decode code and the VP9 hwaccel. Followed: `tools/vp9-jobs.py` fills the VP9 picture setup
  and probability table field by field the way `nvtegra_vp9.c` does (`prepare_frame_setup`,
  `update_probs`, `init_probs`, `set_tile_sizes`, the prediction mode order), but for the two
  chroma quantizer bytes, which follow NVIDIA's names (see [The nvdec suite](families/Nvdec.md),
  Q4); `source/vp9ref.c` lays out the buffers with the sizes of its `decode_init` and orders the
  command words as its `prepare_cmdbuf` does, and surfaces are sized and read block linear as its
  `hwcontext_nvtegra.c` allocates and copies them; `source/nvdec.c` opens the channel as its
  device init does on the console (the submit timeout, the clock asked of the multimedia service
  at module 5 and at the official driver's highest value). The structures' offsets come from
  `nvdec_drv.h` and the methods' from `clc5b0.h`, both checked against reden's `codec_types.h`
  and `nvdec_common.h`.
- **The VP9 bitstream specification**, v0.6 (the WebM Project): the compressed header (6.3), the
  boolean decoder (9.2) and the update rules `tools/vp9-jobs.py` reads it with, and the superframe
  index (Annex B).
- **MD5**, Alexander Peslyak (Solar Designer), 2001, public domain,
  https://openwall.info/wiki/people/solar/software/public-domain-source-code/md5: `source/md5.c`
  and `source/md5.h`, vendored unchanged under a header that says where they came from. The
  devkitPro portlibs have no MD5 to link instead. The reference suite holds it to RFC 1321's test
  suite, whole and in pieces, and to a million a's.
- **libnx's display and kernel sources**, https://github.com/switchbrew/libnx (ISC): read for how
  `viCreateLayer` makes a managed layer, how a window and its framebuffer are made and torn down,
  and how `Mutex` blocks in `svcArbitrateLock` with its owner marked. `source/tests/t_binder.c`
  serialises transaction 17 as `parcelTransact` (`display/parcel.c`) serialises a parcel, so it can
  keep the whole reply.

- **ScarletNX**, https://github.com/scarletnx/ScarletNX (GPL-3.0-or-later), at commit
  8efbf9c44dd8f1b4368ced2b87179bb09a124795: the screens wear its launcher's look. `source/ui.c`
  takes the colours of its YoRHa theme (`src/gui/themes/yorha/theme.ini`: the menus' `[colors]`
  and the run screen's `[run]`), the grid its backdrop draws and the texture of its run screen,
  and draws rows, keys and the foot's band the way its `src/gui/main.c` does (`ui_draw_row`,
  `hints`, `foot`); none of that code is copied. Two files are its data, unchanged:
  - `source/fontvga.h` is its `src/common/utilities/font.c`, the 8 by 16 VGA BIOS font of SeaBIOS
    (`vgasrc/vgafonts.c`, `vgafont16`, commit 81ec9ec0bcf45df11fb7f98339ec9036b546fca0), from
    Joseph Gil's `fntcol16.zip`, whose individual fonts are public domain.
  - `source/icons.h` holds eleven of its menu icons from `src/gui/icons.hex` (play, image, folder,
    activity, wifi, gamepad, log-out, gauge, bug, rotate-ccw, save): ScarletNX's drawings in the
    VGA font's stroke, each after the Lucide 1.61.0 icon of its name, https://lucide.dev. The
    twelfth, headphones, is edenprobe's, drawn the same way after Lucide's. Lucide's licence, which
    covers what they take from its icons:

    ```
    ISC License

    Copyright (c) 2026 Lucide Icons and Contributors

    Permission to use, copy, modify, and/or distribute this software for any
    purpose with or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
    MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
    ```

    Of these, Lucide's log-out and headphones are derived from the Feather project, under its own
    licence:

    ```
    The MIT License (MIT)

    Copyright (c) 2013-present Cole Bemis

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.
    ```
