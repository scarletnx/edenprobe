# The nvn family

The graphics questions asked through NVN, the API a game draws with, so that a runtime which runs a game's NVN calls
itself is asked them too. Only the word `nvn` runs it (see [The mode file](../ModeFile.md)); the start screen has no
row for it, and a file that names it keeps it whatever the screen changes. Its one suite is `nvn`.

## Why a layer

A homebrew program has no NVN: each game carries its own driver. `source/nvnlayer.c` is one, built on deko3d: the
NVN functions a game gets by name from `nvnBootstrapLoader` and the `nvnDeviceGetProcAddress` it hands back, each
recording the deko3d commands that do what NVN says it does. The program exports `nvnBootstrapLoader` (the
Makefile's `--export-dynamic-symbol`), as a game's driver does, and a case looks up every function through it
(`NvnLoadProcs`), never by name.

- **On the console and in Eden**, the layer runs: its deko3d commands reach the graphics unit, and what the unit does
  with them is the answer the case holds.
- **Under ScarletNX**, the runtime finds `nvnBootstrapLoader` among the program's exported functions, as it finds a
  game's, and puts its own NVN in place of every function the case looks up; none of the layer runs past its name
  lookup. The same case then asks ScarletNX's NVN, the path every game takes, where the other drawing suites, which
  call deko3d, reach ScarletNX's engine methods path instead.

`nvnDeviceGetInteger` is written in assembly in the shape of the console's NVN drivers (a bounded switch through a
byte table, its cases setting w0), because ScarletNX reads a game's NVN API version from that code before the game
asks. The layer answers 55.8; ScarletNX answers 55.15 when it cannot read a version, so the version case tells
whether it read the program's.

## What a pass proves, and what it does not

The reference is the graphics unit running what the layer recorded, never the official driver.

- A pass on ScarletNX says its NVN gives the same pixels the console's graphics unit gives for the commands the layer
  chose for those NVN calls. Where the layer and the driver would choose the same commands, which is wherever NVN
  defines the result (a clear's bytes, a copy's texels, a blend's equation), that is the console's answer.
- It says nothing about what the driver alone decides and a game can see without the graphics unit: a texture's
  storage size and alignment, the command memory each call takes, the layouts of its objects, the transform it gives
  a present, the device's answers past the API version. Those come from deko3d's rules in the layer, so the console
  and Eden answer deko3d's, and ScarletNX answers the driver's; no case compares them.
- Where NVN leaves an order to the program, the layer takes the strictest: every order bit of
  `nvnCommandBufferBarrier` is deko3d's full barrier, so a case reads the ordered answer whatever it asked for.
- A choice NVN makes and the layer does not have yet (a format, a target, a flag, a view) is refused with its value
  in `NvnLayerError`, which the failing case's line carries; on ScarletNX that text is always empty.

## The cases

- **the program's NVN functions resolve** (none tier): the lookup gives every function the suite uses, sixty one with `nvnDeviceGetInteger`.
- **the device initializes** (none tier): `nvnDeviceInitialize` with no flags. If it fails, the rest are skipped as
  `depends`.
- **the device answers the layer's API version** (exact): items 0 and 1 of `nvnDeviceGetInteger` are 55 and 8.
- **a queue, its pools and a 64x64 target come up** (none tier): a queue, a command pool, a readback pool, an RGBA8
  target in a pool of its own, and a command buffer with its command and control memory.
- **clear to opaque red**, **clear to a mid channel value** (exact): the target cleared through
  `nvnCommandBufferClearColor`, ordered before `nvnCommandBufferCopyTextureToBuffer` by a barrier, copied to the
  readback pool and compared pixel by pixel, after the pool is filled with a byte no clear here makes. The clear
  suite's colours.
- **two copy textures and a staging pool come up** (none tier): two 64x64 RGBA8 textures in a pool of their own,
  and a processor-visible pool holding a picture of 64x64 texels, every texel four bytes of its own.
- **a picture copied into a texture and back keeps every texel**, **a texture copied into another keeps every
  texel**, **a buffer copied into another keeps every byte** (exact): `nvnCommandBufferCopyBufferToTexture`,
  `CopyTextureToTexture`, `CopyTextureToBuffer` and `CopyBufferToBuffer`, a barrier between each two, every byte of
  the readback pool judged after it is filled with a byte no texel holds.
- **a texture copied to a buffer with MIRROR_Y lands upside down**, **a texture copied into another with MIRROR_X
  lands mirrored** (exact): the copy flags a game mirrors with, which the layer gives deko3d as FlipY (the copy
  engine stepping the buffer backwards, the copy suite's FlipY) and FlipX (the 2D engine).
- **a copy to a buffer with a row stride of 288 keeps the bytes between rows** (exact):
  `nvnCommandBufferSetCopyRowStride`, rows 288 bytes apart and the 32 between them untouched.

  These are ScarletNX's NVN copy tests (`nvn_copy_formats.c`, `vulkan_nvn_copy_flags.inc`,
  `vulkan_nvn_compat.inc`) as far as the layer reaches: the same textures, flags and strides through the program's
  NVN, so ScarletNX answers with `nvn_copy.c`, the path a game's copies take, and the console and Eden with what
  deko3d sends the copy and 2D engines for them.
- **a window acquires and presents both its textures** (none tier): two 1280x720 textures on the default window,
  each acquired with `nvnQueueAcquireTexture`, cleared and presented once. Nothing is read back. Skipped as
  `unavailable` while the program's screen holds the window, which a run begun on the start screen keeps up to its
  end, `noprogress` or not: a quick run from the mode file runs it, as the game family presents only then, and the
  start screen says so when the file names nvn.

## Where nvn/nvn.h comes from

`source/nvn/nvn.h` is h1k421's reconstruction of NVN's types from one game's debug information (MIT, 2020), as
ScarletNX carries it (`src/nx/gpu/nvn/nvn.h`, from nxnative's `dm/gfx/nvn/include/nvn.h`), with the format numbers
written out from ScarletNX's `nvn_tic_format.inc`. The prototypes of `NVN_LAYER_PROCS` follow ScarletNX's
`gpu/nvn/nvn.hook`. Neither holds driver code: names, numbers, sizes and argument types.
