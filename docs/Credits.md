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
