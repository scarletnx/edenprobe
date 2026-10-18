# The service family

Commands a game sends to the system's services, asked the way a game asks them, each answer held to
the one reden gives. Only the word `service` runs it (see [The mode file](../ModeFile.md)), since no
console has yet shown what it answers; the start screen has no row for it, and a file that names it
keeps it whatever the screen changes. One suite per service, each named after it.

## The suites

| suite | cases | what a failure points at |
|---|---|---|
| hwopus | 3 | the hardware Opus decoder's limit on live decoders and the result past it (`hle/service/audio/hardware_opus_decoder_manager.cpp`, `audio_core/opus/hardware_opus.cpp`, `RegisterDecoder`) |

Every expectation is reden's, at 75c12299a6, with the commit that set it; the line of every case
says what it expected and what came back, passed or not. A case whose question needs one before it
is skipped as `depends` when that one did not leave what it needs. The tier is exact wherever a
value is compared and none on the line that only says a service opened.

## hwopus

A game's codec library opens hardware Opus decoders until the service refuses one, and on the
refusal falls back to a software decoder (reden 00a1c392e7, #4384, read from the firmware's
`nn::codec::InitializeHardwareOpusDecoder` after a game failed to load its save with a decoder
refused the wrong way). The service keeps 24 live decoders and answers the 25th with
`ResultOutOfOpusDecoders`, module HwOpus (111), description 385: `0x3026F`.

The suite opens decoders of 48 kHz stereo with libnx's `hwopusDecoderInitialize`, which asks
`GetWorkBufferSize` (1) and then `OpenHardwareOpusDecoder` (0) with the work buffer as transfer
memory, each decoder an object of its own.

| case | what it asks | passes when | tier |
|---|---|---|---|
| hwopus opens a decoder for 48 kHz stereo | one decoder | it opens | none |
| hwopus keeps 24 decoders and refuses the 25th as out of decoders | decoders until one is refused, at most 26 | 24 open and the 25th is refused with `0x3026F` | exact |
| a decoder closed makes room for the next one | after the refusal, close the first and open again | it opens | exact |

The second case stops at the first refusal, so a machine with no limit opens all 26 and says so,
and a limit of another size or a refusal with another result says which. The third is skipped when
the second refused nothing. Every decoder is closed before the suite returns.

## The open questions

- **Q1.** The console's limit and result: reden's 24 and `0x3026F` are read from the firmware, not
  from a console run. Whether the limit is per process or for the whole machine is not known
  either; the suite opens all its decoders from one process.

## The judge, pinned

The `reference` suite holds `source/serviceref.c` on answers made to order, in both builds: every
case under a suite the screens put in the service family, and no name twice; reden's 24 and
`0x3026F` pass, and so does a reopen that succeeds, while no limit, a limit of 25 or of 3, a
refusal with another result (`0xE01`, the session limit) and a reopen still refused each fail with
the line that names it. The result code is worked out by hand: 111 + 385 x 512 = 0x3026F.
