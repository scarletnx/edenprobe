# The judge

Three suites that ask the machine nothing, run first and run in the host build too: nothing a
family reports can be believed without them.

- **selfcheck**, sixteen cases, no hardware involved. That two neighbouring floats measure one unit
  apart and a zero budget rejects them; that an absolute budget is the same on both sides of the
  expected value; that a NaN equals nothing, itself included, whatever the budget; and that three
  units in the last place reach six representable values below a power of two and three above it.
- **support**, two hundred and eleven cases, no hardware involved: what the program does
  around a verdict rather than to reach one. First the way the run asks the services a machine may
  answer in a game's own code, by IPC or through the SDK's functions, and what answered them (see
  [The hooks mode](../Hooks.md)); then the pictures a failing case leaves and the names they
  get, the detailed mode's store, screens and navigation, the start screen, the screen that asks
  whether to go on with a run, the running screen, the listening screen, the pad screen and the
  touch suite's, a tap on each of them and the touch bar's boxes, the start screen's line on nvn's
  window case, the mode file and its words, `all` and `hooks` among them, the
  label and the folder it names, the game family's word and the frames it runs, the machine's
  graphics memory and what `vram=` says of it, the widest mode kept
  whole in the journal, the families each frontend runs and the runner's files under its root, the `skip` result, the time a case reports (twenty cases, see [Time per
  case](../TimePerCase.md)), the journal, the crash list, the hang table, the ban list, the suites
  `skip=` leaves out and what a launch decides from them, a launch a runner stopped at its time
  limit among them (thirty four cases, see [When the program ends in the middle](../Crashes.md)),
  the last copy a device waits for before it closes, and the note a late one leaves (eight cases),
  and a baseline: the limit a case's samples allow, its file and lines,
  the words that name one and the mode line they can make too long, what a run with them does and a
  made-up timed suite whose verdict pass alone it keeps (thirty eight cases, see
  [Baselines](../Baseline.md)). One case carries the whole idea: the diff picture is built from what
  the judge decided pixel by pixel, not from comparing two images, so a pixel that differs and was
  allowed to is not painted as a difference. Every line is the none tier, because none of it is a
  claim about a machine. On a computer (`make -f host.mk host-run`) the suite has all but the first,
  which asks the console's services, and ten cases more: a backward seek that drops a buffered line, which needs the C library's own stream buffer; a
  baseline file that is there and cannot be read, which needs the computer's own file errors; a
  case that times itself, with the one that reads its line back off standard output, the debug
  channel only there, to see the time it was given last, the case counted, and a rehearsal of it
  write nothing; a run of two steps launched in processes of its own, whose first launch ends
  inside a case: the next launch skips that case as crashed and runs the rest, a new run skips it
  from the crash list, and the word `fatal` in its mode file is said once a run and changes none of
  it; the same run with that case banned on the machine its mode file names: one launch, the
  case skipped as banned with its line, then run under `unban`, and nothing changed on a machine no
  line names; the same run with `skip=` leaving its first step's suite out: one launch, each of
  its cases skipped as asked, the next step run, and a name no case came up for said; and the same
  run with a runner's `timeout` after the first launch: the case skipped as timeout with the
  runner's two numbers, off the crash list, and started again by the next run.
- **reference**, four hundred and forty cases, no hardware involved either: the other half
  of the judge, where the rules the cases judge by are pinned. Where clip space lands on the target,
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
    rectangle. The format reference rounds its halves by hand: under Eden, `rint` (one `frintx`)
    rounds 1024.5 to 1025 at nearest, where the architecture gives 1024; the cpu suite checks that
    instruction;
  - eleven on intmath: 58 answers at two pixels worked out with exact rationals away from this
    program's code, and each rule against a second algorithm over the whole sweep;
  - five on fmath: 54 answers worked out with exact rationals and sixty digit decimals, the float
    judge's decisions one at a time, the answer the console never gives refused at every pixel where
    it differs, where the edge operands sit, and a budget written down for every mode;
  - twenty four on the net family's judge, `source/netref.c`: the net file, what each service is
    expected to answer from it, and every format a service answers in, each fixture written out by
    hand from the format rather than captured (the resolver's two serialisations, a certificate in
    PEM and DER and whether it is self-signed, against bytes openssl printed, a handshake's two
    certificate forms, an HTTP response head, a status document, a friend profile at the offsets the
    guest reads, a bearer token's alphabet, a result code as the platform prints one), and what nsd,
    sfdnsres, the sign-in wait, a picture url's `{size}` and a profile slot should be, what a wrong
    nsd expansion is said to be, and when a machine with its network off skips the stream cases;
  - thirteen on the cpu suite's answers, against numbers written by hand: the leading zero counts,
    the two roundings, the saturating shifts, the conversions with 64 fraction bits, the SM4
    inputs from the standard's key and constants, the rounding shifts, doubling multiplies and
    rounding narrows of the NEON gap cases, and the register shifts SSHL to UQSHL, from the Arm
    ARM's pseudocode; and CRC32 and CRC32C against the standard check values;
  - eleven on the raw 3D method words the index, instance, drawtex, query, fixed and coverage
    suites send, against words worked out by hand from deko3d's command header layout, a draw
    texture's signed source among them;
  - eight on the fixed suite's rules, `source/fixedref.c`, on a 64 by 64 target: the squares its
    four points cover and that no pixel centre sits on their edges, where a rectangle lands
    through each viewport it sets, the face a counter clockwise triangle shows under each of them
    and on a lower left device, the window depth of each z from minus one, the depth bounds with
    both ends included, and the eight alpha tests told apart by an alpha of 0.5 at three
    references, against answers worked out by hand;
  - seven on the topology suite's rules, `source/topologyref.c`: the triangles, segments and
    provoking vertices each primitive type makes of a few vertices under either convention, from
    OpenGL's table; where an index list restarts and what the vertex offset adds; the pixels the
    suite's quads, quad strip, polygon and fans cover, and that a polygon taken as a strip misses
    some; that every edge where the suite's colour changes keeps a twentieth of a pixel from
    every pixel centre; which line pixels are judged drawn, clear or neither, a quad's diagonal
    among the clear; and that a missed restart draws pixels the cases judge clear;
  - three on the copy suite's rules, `source/copyref.c`: every texel of its pictures a value of
    its own and every combination of nought and full in its conversion picture; the bytes a
    channel of nought or full becomes in BGRA8, RGB565, RGBA16F and RGBA8; and where a flipped,
    layered, scaled or padded copy puts a texel, against answers worked out by hand;
  - one on the vertex suite's rules, `source/vertexref.c`: the floats each layout gives from bytes
    worked out by hand, the components it lacks, BGRA's swap, and where a vertex, an attribute past
    the stride and a per instance element read;
  - two on the svc suite's result codes, `source/svcref.c`: six kernel results against the codes
    switchbrew's error page gives, and a line's name for a known code, success and an unknown one;
  - five on the cpu suite's floating point rules, `source/cpufpref.c`, against values worked out
    by hand from the Arm ARM's pseudocode: FRINT's roundings of a single in every mode; the estimate tables at their ends and the unsigned
    estimates; FRECPE, FRSQRTE and FRECPX of one, two, four, the edges, noughts, infinities,
    negative values and NaNs, with their flags; half precision under AHP and DN; and the order
    the minimum and maximum put NaNs and noughts in;
  - twenty three on the astc suite's answers: the ASTC reference decoder, `source/astcref.c`, held
    block by block to hashes of what ARM's decoder (astcenc) makes of the same sixteen blocks, its
    weight table to values worked out by hand from the bit replication rule, one value whose
    FP16 decode lands on a byte neither other answer gives, worked out by hand, and where each astc
    and astcimage case says its expected texels come from;
  - ten on the astcimage suite's answers: the reference decoder's sRGB mode and its FP16 decode's
    byte, `AstcUnorm8Fp16`, held over the suite's whole picture to hashes of what astcenc decodes
    it to, the linear footprints both ways (to FP16 and to eight bits) and the sRGB ones once;
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
    [The blit2d suite](Blit2d.md));
  - nine on the game scene's arithmetic, `source/gameref.c`: the world's steps and bounces worked
    out by hand, the fill whose nearest quad is not the last drawn, where the texture, the fill and
    the cells land in a frame's image, the quarters' hash, the reads that cover the big file once in
    1024 frames, the frame times' mean, 99th percentile, longest and hitches on runs made to order,
    the audio thread's limit, the timed cases the warm-up is left out of, and the notes they end on
    (see [The game family](Game.md));
  - eleven on the tex8k suite's judge, `source/tex8kref.c`: the graphics memory its textures want
    of a machine and what it says of one with less, its BC7 mode 6 and BC1 blocks against the
    bytes and texels Pillow 12.3.0 decodes them to, both ways a BC1 code may become eight bits, its
    RGBA8 texels, their fast fill and a salt past 63 folded, its ASTC texels against the picture's
    from the block their tile shifts in, BC7's bound in each region against the texel it was
    measured at, where its regions, levels, samples, copy-out and the processor's square sit, the
    blocks its raw copies cover across levels 3 and 4 and in the mip tail, and its percentile and
    distance (see [the graphics family](Graphics.md#tex8k));
  - twelve on the coverage suite's answers, `source/coverageref.c`: deko3d's standard sample
    positions read back to sixteenths, the mask a rectangle to a pixel's centre or a quarter into
    it covers at two, four and eight samples and with every sample at the centre, the suite's own
    two patterns and its 2 by 2 grid, each split at least a sixteenth from its edge, the modulation
    table entries a quarter to all read, and every modulated channel a whole byte under a factor
    kept to eight bits (see [the graphics family](Graphics.md#coverage));
  - five on the stencil suite's rules, `source/stencilref.c`: the eight compares against the three
    bands in eight different ways, every op at the two values the op cases hold, the ends of the
    range, and the two masks (see [the graphics family](Graphics.md#stencil));
  - six on the lod suite's rules, `source/lodref.c`: whole lambdas from its gradients, one along y
    scaled by the width landing a level up, nearest mip filtering's edges, its coordinate's margin
    on every level, an offset counted in a level's own texels against one in level nought's, and
    its blended value exact in a float (see [the graphics family](Graphics.md#lod)).
