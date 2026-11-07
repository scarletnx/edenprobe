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

- **the program's NVN functions resolve** (none tier): the lookup gives every function the suite uses.
- **the device initializes** (none tier): `nvnDeviceInitialize` with no flags. If it fails, the rest are skipped as
  `depends`.
- **the device answers the layer's API version** (exact): items 0 and 1 of `nvnDeviceGetInteger` are 55 and 8.
- **a queue, its pools and a 64x64 target come up** (none tier): a queue, a command pool, a readback pool, an RGBA8
  target in a pool of its own, and a command buffer with its command and control memory.
- **clear to opaque red**, **clear to a mid channel value** (exact): the target cleared through
  `nvnCommandBufferClearColor`, ordered before `nvnCommandBufferCopyTextureToBuffer` by a barrier, copied to the
  readback pool and compared pixel by pixel, after the pool is filled with a byte no clear here makes. The clear
  suite's colours.
- **a window acquires and presents both its textures** (none tier): two 1280x720 textures on the default window,
  each acquired with `nvnQueueAcquireTexture`, cleared and presented once. Nothing is read back. Skipped as
  `unavailable` while the program's screen holds the window: run it with the word `noprogress`, as the game family
  presents only then.

## Where nvn/nvn.h comes from

`source/nvn/nvn.h` is h1k421's reconstruction of NVN's types from one game's debug information (MIT, 2020), as
ScarletNX carries it (`src/nx/gpu/nvn/nvn.h`, from nxnative's `dm/gfx/nvn/include/nvn.h`), with the format numbers
written out from ScarletNX's `nvn_tic_format.inc`. The prototypes of `NVN_LAYER_PROCS` follow ScarletNX's
`gpu/nvn/nvn.hook`. Neither holds driver code: names, numbers, sizes and argument types.
