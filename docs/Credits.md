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
  and `sinewave.glsl`, which `order_copy.comp.glsl` follows in shape.

- **astcenc**, https://github.com/ARM-software/astc-encoder (Apache 2.0): ARM's reference ASTC
  codec. None of its code is in this repository; `source/astcblocks.c` carries hashes of what its
  decoder (commit 9fbec680) makes of each block the astc suite samples, and `source/astcref.c`
  follows its reading of two points the specification leaves to the decoder: bits past the end of
  an integer sequence read as nought, and an HDR endpoint mode in the LDR profile is the error
  colour on its own partition only.
