# The judge

Three suites that ask the machine nothing, run first and run in the host build too: nothing a
family reports can be believed without them.

- **selfcheck**, sixteen cases, no hardware involved. That two neighbouring floats measure one unit
  apart and a zero budget rejects them; that an absolute budget is the same on both sides of the
  expected value; that a NaN equals nothing, itself included, whatever the budget; and that three
  units in the last place reach six representable values below a power of two and three above it.
- **support**, one hundred and eighty one cases, no hardware involved: what the program does around a
  verdict rather than to reach one. The pictures a failing case leaves and the names they get, the
  detailed mode's store, screens and navigation, the start screen, the screen that asks whether to
  go on with a run, the running screen, the listening screen, the pad screen and the touch suite's,
  a tap on each of them and the touch bar's boxes, the mode file and its words, the label and the
  folder it names, the `skip` result, the time a case reports (nineteen cases, see [Time per
  case](../TimePerCase.md)), the journal, the crash list, the may-crash and hang tables and what a
  launch decides from them (twenty seven cases, see [When the program ends in the
  middle](../Crashes.md)), the last copy a device waits for before it closes, and the note a late
  one leaves (eight cases), and a baseline: the limit a case's samples allow, its file and lines,
  the words that name one and what a run with them does (twenty eight cases, see
  [Baselines](../Baseline.md)). One case carries the whole idea: the diff picture is built from what
  the judge decided pixel by pixel, not from comparing two images, so a pixel that differs and was
  allowed to is not painted as a difference. Every line is the none tier, because none of it is a
  claim about a machine. On a computer (`make -f host.mk host-run`) the suite has one case more: a
  backward seek that drops a buffered line, which needs the C library's own stream buffer.
- **reference**, three hundred and forty nine cases, no hardware involved either: the other half of
  the judge, where the rules the cases judge by are pinned. Where clip space lands on the target,
  which pixels a triangle covers and which of two triangles owns the centres on their shared edge,
  projected from the very numbers the rasterisation cases hand the hardware, so it proves the
  geometry they draw rather than restating it. Fifteen transcendental answers as bit patterns worked
  out to fifty digits away from any C library, so a runtime that drifts moves an expected value on
  its own machine and says so there, not inside a float case. It runs in both builds, because a
  reference that answered differently on the two processors would be blamed on the graphics unit.
  Among the rest:
  - three cases on the blend table (the blend suite says why);
  - seven on the format rules: the clear and drawn pixel of all sixty formats, worked out from each
    format's name by a separate script, `tools/format-pixels.py` (it prints the table the test
    holds, and shares no code with `formatref.c`); every format's fields against its pixel; the
    half, small float and sRGB conversions inverted over every code; the quad covering exactly its
    rectangle. One of these caught the emulator's processor rather than its graphics unit: under
    Eden, `rint`, a single `frintx`, rounds 1024.5 to 1025 with the rounding mode at nearest, where
    the architecture gives 1024, so the format reference rounds its halves by hand (the cpu suite
    now checks that instruction itself);
  - eleven on intmath: 58 answers at two pixels worked out with exact rationals away from this
    program's code, and each rule against a second algorithm over the whole sweep;
  - five on fmath: 54 answers worked out with exact rationals and sixty digit decimals, the float
    judge's decisions one at a time, the answer the console never gives refused at every pixel where
    it differs, where the edge operands sit, and a budget written down for every mode;
  - twenty two on the net family's judge, `source/netref.c`: the net file, what each service is
    expected to answer from it, and every format a service answers in, each fixture written out by
    hand from the format rather than captured (the resolver's two serialisations, a certificate in
    PEM and DER and whether it is self-signed, against bytes openssl printed, a handshake's two
    certificate forms, an HTTP response head, a status document, a friend profile at the offsets the
    guest reads, a bearer token's alphabet, a result code as the platform prints one), and what nsd,
    sfdnsres, the sign-in wait, a picture url's `{size}` and a profile slot should be;
  - eleven on the cpu suite's answers, against numbers written by hand: the leading zero counts,
    the two roundings, the saturating shifts, the conversions with 64 fraction bits, the SM4
    inputs from the standard's key and constants, and the rounding shifts, doubling multiplies and
    rounding narrows of the NEON gap cases, from the Arm ARM's pseudocode;
  - eight on the raw 3D method words the index, instance, drawtex and query suites send, against
    words worked out by hand from deko3d's command header layout;
  - twenty one on the astc suite's answers: the ASTC reference decoder, `source/astcref.c`, held
    block by block to hashes of what ARM's decoder (astcenc) makes of the same sixteen blocks, and
    its weight table to values worked out by hand from the bit replication rule;
  - four on the eac suite's answers, against values worked out by hand from the specification's
    formula;
  - eighteen on the etc2 suite's answers: the block builders held to the bytes the suite samples,
    the ETC2 reference decoder, `source/etc2ref.c`, held block by block to hashes of what Mesa's
    decoder (26.1.6) makes of the same ten blocks, five texels worked out by hand, and the test
    that names a block uploaded as it is, which every block's bytes meet and no block's colours do;
  - four on the bcn suite's layout, `source/bcnref.c`: where deko3d puts each level and layer of
    a block linear array, against offsets worked out by hand from its layout rules, and the solid
    BC4 block;
  - fourteen on the audio family's answers: a tone's level read back from a recording, exact for a
    tone with whole periods in the run and blind to every other such tone; the frames the
    recordings are laid out in; the limiter's rule, held by hand to a steady input and to a sine,
    and mode 0's second answer, the two-step reciprocal, held to values worked out apart; the
    conversion tables' formula, held to values the renderer's own tables hold, and the level a tone
    keeps through each, held to a separate model; and the signals the listening sequence plays;
  - thirty on the video family's judge, `source/vicref.c`, held to `tools/vic-expect.py`: the
    BT.601 colours, the shift's two orders and the clamp by value, the configuration structure
    word by word and by hash, the command words, every case's output under each combination of
    its answers, and the judge on outputs made to order (see [The video family](Video.md));
  - eight on the MD5 the nvdec suite hashes its images with, `source/md5.c`: RFC 1321's test suite,
    each message whole and in pieces that straddle its blocks, and a million a's;
  - twelve on the nvdec suite's jobs and judge, `source/vp9ref.c` and `source/vp9jobs.c`: the jobs
    fit the buffer, the patches rebuild the tables `tools/vp9-jobs.py` wrote, the command words by
    hand, the block linear reading against nvtegra's own copy walk, and the verdicts on reads made
    to order (see [The nvdec suite](Nvdec.md));
  - fourteen on the pad family's rules, `source/padref.c`: the prompts, what a press is and when a
    prompt runs out, the stick distances, edges and their shape, the styles and ids, the slots and
    each controller's press, against answers worked out by hand (see [The pad family](Pad.md));
  - seven on the touch suite's rules, `source/touchref.c`, on states made to order: the prompts and
    targets, a prompt waiting out a finger already down, the first prompt's four outcomes, a tap
    within 48 pixels, two ids for two fingers, a drag's direction and id, the lift, and the note
    (see [The touch suite](Touch.md));
  - eight on the kernel family's judge, `source/schedref.c`, on traces made to order: what each
    case leaves with the scheduler's rules and without each of them, and the answers to Q1 to Q3
    (see [The kernel family](Kernel.md));
  - three on the display family's judge, `source/binderref.c`, on reply parcels built word by word:
    reden's two layouts, replies in neither, and the cases' frames and requests (see [The display
    family](Display.md));
  - seven on the blit2d suite's model and judge, `source/blit2dref.c`, held to
    `tools/blit2d-expect.py`: the source, the filled destination, every case's image under each
    combination of answers and the 2D engine's words, then the verdicts on reads made to order (see
    [The blit2d suite](Blit2d.md)).
