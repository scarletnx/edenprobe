# edenprobe

A test program that runs on the console and inside an emulator of it, decides for itself whether
the graphics came out right and whether the online services answered as their protocol says, and
writes a report you can diff between the two.

It is not a benchmark and not a demo. Every case computes the answer it expects and checks the
answer it got. Nothing here needs a person to look at the screen and judge.

It was gfxprobe until 23 September 2026. The name changed because the program is growing suite
families beyond the graphics unit, and one binary with one judge is the reason to keep them
together rather than start a second program next to it.

## Why not compare screenshots

Because two correct implementations are allowed to disagree, and the specification says so. The
graphics API requires division to land within 2.5 units in the last place, the logarithm within
3 outside the range from a half to two, the power of two within 3 plus twice the magnitude of
its argument, and the sine and cosine only within an absolute error of 2^-11 and only inside
minus pi to pi. Sub-pixel rasterisation precision is a limit each implementation reports, not a
constant. A hash of a
shaded frame therefore breaks between two machines that are both right, and the projects that
took that route maintain one reference image per backend, per driver, per operating system, and
still chase false positives.

So the decision is made inside the program, on both machines, by the same code.

## The three tiers

Which tier a case uses is a claim about the hardware, and it belongs in the case.

- **Exact.** Bit for bit. Legal only where the result is defined exactly: integer render targets,
  integer arithmetic, bit patterns moved around. A float shader never qualifies, however tidy the
  numbers look on one machine.
- **Units in the last place.** For float work, with the budget stated per case.
- **Position.** The right answer, possibly a pixel or two away, for rasterisation coverage.

## What it checks today

Nineteen suites, in the order they run, because each leans on the ones before it: the judge,
then the processor, then the graphics family, then the net family. A run is 487 cases, 437 of
them the judge, the processor and the graphics family and 50 the net family.

- **selfcheck**, sixteen cases, no hardware involved. That two neighbouring floats measure one
  unit apart, that a zero budget rejects them, that an absolute budget is the same on both sides
  of the expected value, that a NaN is never equal to anything including itself, not even against
  a budget of every value there is, and that a budget of three units in the last place reaches
  six representable values below a power of two and three above it. If one of these fails,
  nothing else the suite says can be believed.
- **support**, seventy two cases, no hardware and nothing of the hardware's looked at. The two
  things this program does around a verdict rather than to reach one: the picture it leaves
  beside a failing case, and the screen the detailed mode shows that picture on. It checks that
  a case name becomes the file name its own line says it does, that a written picture parses
  back as the image its header claims to be, that the bytes kept beside a picture of an integer
  target are every byte that target held, that two case names which spell one file name still
  get a file each, that the suite takes its own fixtures away again, that the navigation and
  the layout are what the screen is made of, and that the mode file reads as the section on the
  console says: each word alone and together, a missing file, words it does not know, a family
  this build lacks, and the ten seconds an unattended screen waits; that a case which could
  not ask its question is a result of its own, `skip`, counted apart; and that the time a case
  reports is the one its samples give (see "Time per case"). One of them carries the
  whole idea: the diff picture is built from what the judge decided pixel by pixel and not from
  comparing the two images, so a pixel that differs and was allowed to is not painted as a
  difference. Every line is the none tier, because none of it is a claim about a machine.
- **reference**, one hundred and eighteen cases, no hardware involved either. The other half of
  the judge: where clip space lands on the target, which pixels a triangle covers, and which of
  two triangles owns the centres that sit exactly on the edge they share. It runs in both builds,
  because a reference that answered differently on the two processors would be blamed on the
  graphics unit.
  It also projects the very clip space numbers the rasterisation cases hand the hardware, so what
  it proves is the geometry those cases draw and not a restatement of it, and it pins fifteen
  transcendental answers as bit patterns worked out to fifty digits away from any C library, so
  that a runtime which drifts moves an expected value on one machine and says so there rather
  than inside a float case. Three of its cases are about the blend table rather than about
  arithmetic: that no case asks the graphics library for something it does not send, and that
  what the table gave up in exchange is still covered. The blend suite below says why.
  Seven of its cases pin the rules the format suite judges by: the clear and drawn pixel of all
  sixty formats, worked out from each format's name by a separate script
  (`tools/format-pixels.py`, which prints the table the test holds) that shares no code with
  `formatref.c`, every format's fields against its pixel, the half, small float and sRGB
  conversions inverted over every code they have, and the quad covering exactly its rectangle.
  One of those caught the emulator's processor rather than its graphics unit: under Eden, `rint`,
  which is a single `frintx`, rounds 1024.5 to 1025 with the rounding mode at nearest, where the
  architecture gives 1024, so the format reference rounds its halves by hand.
  Eleven pin the grown intmath suite: 58 answers at two pixels worked out with exact rationals
  away from this program's code, and each rule against a second algorithm over the whole sweep,
  the minimum, maximum and clamps against 64 bit comparisons and the half patterns against the
  rule that builds them among them.
  Five pin the grown fmath suite: 54 answers worked out with exact rationals and sixty digit
  decimals, the float judge's decisions one at a time, the answer the console never gives
  refused at every pixel where it differs, where the edge operands sit, and a budget written
  down for every mode.
  Twenty one are the net family's judge, in `source/netref.c`: the file that says what a
  backend publishes, what each service is expected to answer from it, and every format a service
  answers in, each fixture written out by hand from the format rather than captured from a
  machine. The resolver's two serialisations, a certificate in PEM and in DER and whether it names
  itself as its issuer (against bytes openssl printed), the two forms a handshake hands a
  certificate back in, an HTTP response head, a service status document, a friend profile at the
  offsets the guest reads, a bearer token's alphabet, a result code as the platform prints one;
  and the expectations: what nsd and sfdnsres should answer for a name, when the sign-in wait is
  over, a picture url's `{size}`, and what makes a profile slot the right one.
- **cpu**, three cases, on the processor and in every mode, since it needs no device: count
  leading zeros over the sixteen 8-bit, eight 16-bit and four 32-bit lanes of one vector
  register, loaded from memory so a translator cannot fold the answer away, compared exactly with
  the reference, which pins its own answers against numbers written by hand in four cases. The
  16-bit case is there because a translator that widened the lanes to count them lost the upper
  half on hosts with AVX-512: lanes four to seven came back as 0xfff0. Console build only, since
  its cases are AArch64 instructions.
- **clear**, five cases, on the graphics unit: one that reports whether the graphics device came
  up at all, at no tier because it compares nothing, and four that draw. Each of the four clears a
  64 by 64 RGBA8 target and copies all 4096 pixels back to compare them. Three cover the whole
  target, in three colours in a row, so a readback handing over a stale buffer cannot pass by
  accident. The fourth covers the target in one colour and then clears a rectangle inside it in
  another, which is what makes the answer depend on where a pixel is: a target that is all one
  colour reads back the same whether or not the copy walked its blocks in the right order, so the
  three before it cannot catch a transposed, flipped or shuffled readback and this one can. All
  four are Exact tier: the format is integer and every clear value is a constant, so each channel
  has exactly one right byte.
- **format**, sixty four cases, on the graphics unit. Every colour format the graphics library can
  render to, sixty of them: one to four channels, eight to thirty two bits, normalised, signed
  normalised, integer, signed integer, float, the two small floats of RG11B10, sRGB, the packed
  RGB10A2 and the blue first sixteen bit ones, and the X formats whose fourth channel is stored
  and not defined. Each gets its own 32 by 32 target, cleared to one value, with a quad drawn
  over a rectangle of it (x 4 to 20, y 12 to 28, aligned to nothing and not centred) with a
  second value out of a uniform buffer, and every pixel read back at the format's own size and
  compared with what `source/formatref.c` says the format stores there: the drawn value inside,
  the clear outside. The four other cases are the bring-up and a check that the reference's
  table and the library's formats are paired by name.

  The layout rule is one line: a format's name lists its fields from the least significant bit
  up, little endian, so RGBA8 is red in the first byte and BGR565 is blue in bits 0 to 4 and red
  in 11 to 15. The conversion rules are the specification's: clamp and round to the nearest code
  for normalised channels, minus one as minus the largest code (never the one below it), the low
  bits of an integer, binary16 and the small floats rounded to nearest even, and the sRGB curve
  before the normalised rule. The values are chosen so that each rule has one answer. The
  specification's conversion must return the integer its product lands on exactly and may
  otherwise pick either neighbour, so every normalised value is the nearest float to a code over
  the largest code, and its product with the largest code in single precision is exactly that
  code, for every code used at every width from 1 to 16 bits; the reference suite checks it. The
  sRGB values decode from a code and encode back within three millionths of it. The drawn value
  also carries one channel above the range and one below it, so both clamps are judged; the
  integer ones are the golden ratio's word and its complement, so every stored bit changes; the
  float ones are exactly representable, including the largest half, the smallest normal and an
  infinity, and no denormal, which a target may flush. Integer and normalised formats are Exact;
  the float ones report the units in the last place tier at a budget of nought. sRGB clears use
  nought and one, which encode to themselves, so whether a clear goes through the curve decides
  nothing.

  The formats the library refuses as a target are named in a note with the reason, not left out
  quietly: the twelve byte RGB32 ones, the sampling only packed ones, the shared exponent one,
  the compressed ones and the depth ones. A failing case says how many pixels were wrong, the
  first one's bytes against the expected ones under the mask of defined bits, and how many
  inside the quad still hold the clear value and outside hold the drawn one, which separates a
  conversion from a coverage fault. In an emulator a failure here points at the format tables:
  the render target format mapping and the host format it becomes (`video_core/surface.cpp`,
  `video_core/renderer_vulkan/maxwell_to_vk.cpp`), the clear of that format, or the fragment
  output's type for it. Run against the emulator build from before the integer target fix
  (`eden-cli-dacd0e59f2`), every one of the 25 integer formats fails, and only outside the quad:
  the clear wrote the wrong bits and the draw was right, which is the defect that fix addressed,
  now seen for every integer format rather than for R32_Uint alone.
- **raster**, ten cases, on the graphics unit. A triangle or a quad drawn in one flat colour
  through a vertex and a fragment shader compiled into the binary, with every pixel compared to
  what the coverage rule says. Exact for pixels at least half a pixel from every edge, position
  for the band an edge runs through, and exact everywhere for the two cases whose geometry puts
  32 pixel centres exactly on a shared edge: those are decided by the fill rule, not by sub pixel
  precision, and an implementation that draws a shared edge twice or not at all says so there.
- **intmath**, forty six cases, on the graphics unit. Fragment shaders doing integer arithmetic on
  their own coordinate into an R32_Uint target: add, multiply, all three shifts, and, or, exclusive
  or, bit counting, bit scanning, division and remainder; conversions between integers and floats
  both ways, above two to the 31 included; packUnorm4x8, packSnorm2x16 and packHalf2x16 and
  their round trips, 0x8000 coming back as 0x8001; shifts by 31, 32 and 33 and by every count
  up to 63; signed division and remainder with every combination of signs; findMSB and findLSB
  of nought and of minus one; bitfieldExtract and bitfieldInsert at every offset and width,
  nought and 32 included; bitfieldReverse; and the signed and unsigned minimum, maximum and
  clamp, with the most negative and most positive values, nought and minus one against each
  other on the last row. Exact tier, and it is the strongest claim in the suite: an integer target normalises
  nothing, so a wrong answer is a wrong bit pattern rather than a difference of opinion about the
  last place.

  Two of those needed a rule the language does not give, and the rule is the console's own,
  read out of the compiled shader. The language leaves a shift by 32 or more undefined; `uam`
  emits every register shift here without the wrap flag (bit 39 of the instruction clear, read
  with a decoder of the opcode table in the emulator's `frontend/maxwell/maxwell.inc`), and
  such a shift clamps its count to 32, which is how PTX defines the same instructions: 32 and up
  shift everything out, to nought or to the sign. And the console has no integer divide: `uam`
  says so as it builds (`program uses non-constant integer division/modulo ... floating point
  emulation with resulting loss of precision has been applied`), and the compiled division is
  two conversions, a reciprocal, a multiply and a truncation. A quotient near a whole number is
  then the reciprocal's to decide, so the operands are built to keep it honest: under two to the
  18, never a multiple of the divisor, so the true quotient is at least a part in 2334 from a
  whole number and a float error of a few units cannot move the truncation. The reference suite
  measures both properties.

  The modes are spread over six shaders by family (the original fourteen, conversions and
  packing, shifts, division, remainder, bit operations), so an instruction an emulator cannot
  translate, which fails the whole shader it is in, takes down its own family and no other; a
  case whose every pixel still holds the sentinel says so. The emulator's log names the
  instruction it could not translate, not the shader; the family of the failing cases says which
  shader it was in. The first three cases are the fragment coordinate and the uniform buffer,
  which every case after them reads its operands and its instructions out of.
- **blend**, thirteen cases, on the graphics unit. The fixed function blend unit: adding,
  subtracting both ways round, the minimum and the maximum, the source alpha, the blend constant,
  and the clamp at each end of the range. Exact tier on a normalised target, which takes an
  argument rather than an assertion: the specification requires the conversion back to eight bits
  to round to nearest but leaves a halfway value to the implementation, so every case here is
  built to land at least 0.49 of a step away from one and the reference suite measures that
  rather than taking it on trust. Two of the cases set both factors to zero under the minimum and
  the maximum, because the specification says those two equations ignore the factors entirely.

  The alpha channel has its own equation and its own pair of factors, and wiring it to the colour
  one is the classic way to get this unit wrong, so the table keeps them apart. It keeps them
  apart by the equation and by the source factor and never by the destination factor, and that is
  a limit of the graphics library rather than a gap in the suite. `dkCmdBufBindBlendStates` writes
  six registers per target and puts the colour destination factor in the last of them, which is
  the alpha destination one, so a blend state whose two destination factors differ reaches the
  hardware as one whose two are the same (deko3d 0.5.0, `source/maxwell/gpu_3d_state.cpp`; the
  other five words each carry the field they are named after). A case built on that difference
  fails on a machine that has done nothing wrong, which is the one kind of failure this suite
  must not be able to report, so the reference suite refuses one and checks that some case still
  separates each of the two parts that do reach the registers.

  Whether the hardware then acts on those two is a second question the library leaves open, and
  the blend suite measures it rather than assuming. Separate alpha blending is switched on by the
  register immediately before the six that call writes, and no call in the library writes that
  one or its global twin, so what it holds is whatever the machine left there and a console and
  an emulator of one can disagree. Two notes on every run carry the answers: whether the alpha
  channel runs an equation of its own, and whether an alpha destination factor of its own has
  started arriving. They are notes and not cases because each is a property of the way to the
  hardware rather than of the blend unit's arithmetic, and the first is what makes the second
  readable: with no separate alpha state, a destination factor that made no difference and one
  that never arrived look the same.
- **depth**, thirty one cases, on the graphics unit. One quad laid down at a known depth and a
  second one challenging it: every one of the eight compare operations the graphics library
  offers, at every one of three depths, which is twenty four cases. Three depths each is the
  smallest set that works and not padding: three depths give eight patterns of pass and fail,
  there are eight operations, and each owns exactly one pattern, so leaving any depth out of any
  operation lets that operation share its answers with another and an emulator that had wired the
  two together would go unnoticed. One further case works all twenty four answers out on the
  processor and checks that the eight really are different, which is that argument rather than a
  claim of it. Plus the depth buffer read back and compared as a float bit for bit. Exact tier:
  the depths are 0.25, 0.5 and 0.75, which survive the transform into depth space untouched, so
  "the buffer holds exactly one half" is a statement about a bit pattern, and a fragment at the
  buffer's own depth really is an equality rather than two numbers that came out close. Three of
  the cases are about the write rather than the test, which is the half a colour cannot show.
- **texture**, twelve cases, on the graphics unit. A 4 by 4 texture drawn over the whole target, so
  sixteen by sixteen pixels land on each texel and every pixel names a texel this suite can work
  out. Nearest filtering with repeating, clamping to the edge and mirroring, each with a quad
  three copies wide so two thirds of it is outside the texture, and linear filtering laid out so
  the only weight the filter can use is one half and every pair of texels averages to a whole
  byte. Exact tier throughout, and what earns it is the layout: no pixel centre comes within a
  thirty second of a texel boundary, which the reference suite measures, and no two texels the
  linear case blends differ by more than 48, so the filter's weight would have to be wrong by one
  part in 96 before any byte moved.
- **coherence**, twenty one cases, on the graphics unit. Five are the none tier: the device, the
  shaders, the images and their starting texels coming up, and the block linear texture having
  exactly the size the gob rule below assumes. Sixteen are exact. Every suite above draws once
  into memory nobody has read, so the caches an emulator keeps between draws, submits and copies
  never have an old answer to give. Here each sampling and uniform case, except the first alias
  case, first makes the reader hold the old contents, then writes new ones by one path and reads
  them by another: a texture rendered and sampled in the same list, whole and in part; a texture
  the processor rewrites between two submits, pitch linear, block linear from the middle of a gob,
  and one row through a cached mapping flushed with `dkMemBlockFlushCpuCache`; a uniform buffer
  the processor rewrites between submits, once whole and then one word at a time over four
  submits, and one the list itself pushes between two draws; a render target sampled through an
  R32_Uint view of the same memory, judged as the little endian word rotated right by four bits,
  so every output byte mixes two input bytes; a texture filled by the copy engine from a buffer
  and from part of another image; and a render target copied, whole and in part, to another image
  and read back, which tests that rendered pixels are flushed before the copy engine reads them.
  Everything drawn or written is a pattern whose four bytes are unique to each texel and to each
  step, so a shift, a transposition or a stale texel is always a wrong byte, and the reference
  suite checks that over every seed. Region one of the target shows the old contents, region two
  the new, and a failing case counts how many of its wrong pixels hold exactly the old contents,
  which is what a stale cache looks like. The synchronisation is what `deko3d.h` documents for
  each hand over and no more, named in the suite's header: the fragments barrier between a draw
  that samples an image and one that renders it, the full barrier around the copy engine, the
  image or the shader invalidation before reading what was rewritten, and a wait on the queue only
  where the processor touches the memory, since a wait anywhere else would hide the fault the
  suite is there to find. Every expectation comes from one image model in `source/coherref.c`,
  pinned by the reference suite.
- **fmath**, twenty six cases, on the graphics unit. Addition, multiplication, a fused multiply and
  add, division, the square root, powers and logarithms of two, the sine and the cosine, into an
  R32_Float target, compared against the same arithmetic in double precision; and the edges: a
  fused multiply and add that nearly cancels its own product, the minimum and the maximum against
  infinities and NaNs, mix carrying them through, denormal products, sums and operands, the power
  of two from -128 to nearly 128, the logarithm from the smallest normal to nearly the largest
  float, square roots and inverse square roots of denormals and tiny normals, and floor, fract,
  round and roundEven on negatives and halves. Units in the last
  place tier, with the budget per case, because the tables give each operation a different figure
  and two of them are not even the same shape. They all come from one place, named in
  `source/fmathref.h`: the Vulkan specification's precision tables, core for the operations that
  are core instructions and GLSL.std.450 for the rest. That choice is not arbitrary. The shading
  language's own specification leaves the trigonometric functions undefined, so a suite leaning
  on it would have no figure at all for two of these nine; the Vulkan tables cover all nine, and
  they are the right ones to hold an emulator to. What they give, and what each case therefore
  allows:

  | operation | the table | what the case allows |
  |---|---|---|
  | addition, multiplication | correctly rounded | nought |
  | a multiply and an add | inherited from a multiply and an add; the console's FFMA fuses | nought |
  | division | 2.5 | 3 |
  | the square root | inherited from one over the inverse square root, which is 2 and 2.5 | 5 |
  | two to the power of x | 3 plus twice the magnitude of x | 3.5 plus 2 per unit of it |
  | the logarithm to base two | 3 outside the range from a half to two | 3.5 |
  | the sine and the cosine | an absolute 2^-11 inside minus pi to pi | the same |
  | the edges of the power of two and the logarithm | as above, over the wider ranges | as above |
  | the inverse square root of tiny operands | 2 | 2.5 |
  | the square root of tiny operands | inherited from one over the inverse square root, 2 and 2.5 | 5 |
  | fma nearly cancelling its product, floor, fract, round, roundEven, min and max against infinities, denormals | exact once the console's rules are applied | nought |
  | min and max against a NaN | the console hands back the other operand, as NMin and NMax do | nought |
  | mix through infinities and NaNs | the formula in IEEE arithmetic | nought |

  Where the tables allow more than one answer and the console's compiled shader gives exactly
  one, the case holds a machine to the console's, as the integer suite does for its shifts and its
  division. Read out of `fmath.frag.dksh`: every float instruction flushes denormals (the FMUL,
  FFMA, FADD and FMNMX flush bits are set, and MUFU has none to clear), every fma is one fused
  FFMA, and round compiles to the same round to nearest even as roundEven. So denormal answers
  and denormal operands are nought, a multiply and an add round once, and a half goes to even.
  The fma case subtracts the rounded product with its low twelve bits cleared, so a fused machine
  keeps the product's rounding error and a machine that multiplies and adds apart loses it at
  nearly every pixel. A failing case says how many of its wrong pixels hold the answer the tables
  would allow and the console does not give, and names it: denormals kept, the multiply and the
  add done apart, halves rounded away from nought, plus nought where the console's rounding
  keeps minus nought. The judge's rule is one function,
  `FMathJudge`, pinned case by case by the reference suite: a NaN expected is met by any NaN, a
  nought by the same nought with its sign, and anything else by the budget. The NaN and infinity
  cases hold an emulator to the console rather than to the Vulkan tables, which let a host
  assume neither arrives; a failure there is a difference from the console, and the budget line
  says so.

  Most of those carry half a unit on top of the table's figure, for the reference itself being a
  double precision answer rounded to single. Three do not, and each says why on its own line:
  addition and multiplication are exact in double precision before the rounding, the multiply and
  add is measured against the processor's own fused one which is the exact answer, and the sine
  and cosine's budget is two thousand times larger than anything the reference could contribute.
  The power of two is the only budget that is not a constant, because the table's figure for it
  is not one: over the range this case sweeps the allowance runs from three and a half to eleven
  and a half, and a flat number would either fail a correct machine at the ends or wave the
  middle through.

  A unit in the last place is a distance and not a count of representable values, because the
  values just below a power of two are half as far apart as the ones just above, and the suite's
  own answers land on powers of two often. Each case reports the worst error it saw next to the
  allowance it had, so a run that passes still says how much room was left. Every case that
  computes something reports the units in the last place tier, including the two whose budget is
  nought, because a tolerance of nought is still a tolerance and a float shader does not qualify
  for Exact; the one case that does is the guard, which copies a float through a uniform buffer
  and computes nothing. The suite runs last because it is the only one where two correct
  machines are allowed to disagree.

The net family asks the services a program reaches the network through, the way a game asks them,
and judges each answer against its protocol or against what `sdmc:/edenprobe/net` says the backend
publishes (the section on the net family below). Every line says what was expected and what came
back, pass or fail. A case that needs a backend the file does not name, or a service this program
cannot open, is `skip` with the reason, and counted apart from passes. The tier is exact wherever
a value is compared and none where a line only says a service opened. Nothing waits without a
budget of its own: every connect is non-blocking and polled for three seconds, every read is
polled first, and every handshake, write and read over ssl runs in non-blocking mode against five
or three seconds, so a server that drops the connection attempt or accepts and never speaks costs
a failed case with the reason, never the run.

  - **nifm**, seven cases. The service opens; a request nobody submitted says so; and, when the
  file names a backend, the connection is reported connected, the machine has an address of its
  own, a submitted request is accepted without being asked for its result and again when it is,
  and while one stands the service says an internet request is accepted.
  - **dns**, eleven cases, sfdnsres and nsd. A dotted address resolves to itself through both of
  sfdnsres's serialisations, read by this program's own parsers; a name under `.invalid` does not
  resolve (RFC 6761); nsd names its environment, replaces the `%` placeholder with it, and leaves
  a plain name as it is. With a session, and only once the session answers (the fork signs in in
  the background when the program loads, so the suite asks nsd until it gives the session's
  answer, for at most the fork's fifteen second sign-in budget, and a note says how long it took):
  nsd hands back the destination the backend publishes for a name, sfdnsres resolves that name to
  the destination's address as a hostent, as addrinfo and through the C library, and a name
  carrying the placeholder, and the name nsd expanded, both go to loopback (the contract's host
  rewriting rules, reden's `docs/OnlineService.md`).
  - **bsd**, eight cases. Two datagram sockets bind to loopback; a datagram goes from one to the
  other and is sent back, arriving whole both ways and each time from the socket that sent it, so
  the second socket is the suite's own echo through the same service; an empty socket has nothing
  to read; a stream to a listener the suite opens carries bytes both ways; a port nothing listens
  on refuses; a receive timeout set with `SO_RCVTIMEO` gives up with `EAGAIN` in its time (bounded
  by a datagram another thread sends two seconds in, so a machine that ignores the option fails
  the case and does not hang the run). With a server: a stream connects to it, and a connect that
  does not block completes, polls writable and reports no pending error, which is what a game
  does.
  - **ssl**, thirteen cases. A context; a new connection's default verification (the peer and the
  name, as libnx documents); the name set on a connection reading back; a connection taking a
  socket and blocking mode, and saying so; a descriptor handed back by default and none with
  `DoNotCloseSocket` (libnx's `ssl.h` on `sslConnectionSetSocketDescriptor`). With a server whose
  certificate the file names: a verifying handshake refuses a certificate that names itself as
  issuer, succeeds once that certificate is imported, and hands back exactly its bytes; a
  handshake that does not verify succeeds, carries a request out whole and reads back a 200 with a
  status document that says nothing is wrong; and the connection keeps its socket after the
  program closes its own descriptor, since the service works on a duplicate.
  - **acc**, six cases. acc:u0 and a user, the network account manager, and with a session: the
  account id is the one the backend publishes, the id token refresh completes, succeeds and leaves
  a token in the cache, the token is a bearer token (RFC 6750's alphabet) terminated inside its
  buffer, and the cache hands back the same token again.
  - **friend**, five cases. The service opens, and with a session: the blocked user list is the
  backend's, a profile the backend knows comes back valid with its name and picture, one it does
  not comes back invalid in its own slot with the id that was asked, and a picture url comes back
  with every `{size}` replaced by the size asked for.

When a session is named and never answers the sign-in wait, every session case in dns, acc and
friend says so at the end of its detail, so a failure there reads as a sign-in that did not happen
rather than as a fault in the service.

Some expectations rest on documentation and reasoning, not on a console run yet, and a run on one
is what would settle them: that an unknown profile's slot carries the id that was asked (the
contract says only that it comes back invalid; the id is the fork's `profile_query.cpp`); that the
ssl connection keeps its socket when the program closes its own descriptor (POSIX `dup`
semantics); that a submitted nifm request is accepted by itself within three seconds (the three
seconds are this program's); the descriptor `sslConnectionSetSocketDescriptor` hands back (libnx's
reading of it, which switchbrew shares: the one passed in); and nsd expanding the placeholder
with no session (the contract's statement about the real service). Each case's detail says the
same.

Nothing is ever put on a screen. The target is offscreen and the program reads it back itself,
which keeps the compositor, the swapchain and vertical sync out of a test that is about none of
them, and keeps the first problem on a new machine from being "did the screen work".

## The roadmap

The suites are ordered by how much one case tells you when two machines disagree, and each
one leans on the ones above it. That order is the whole point: when a run comes back with
forty failures, the first suite that failed is the only one worth reading, because everything
below it was measured with a broken instrument.

This is the order they run in, and the order they were built in. Anything added later goes where
its dependencies put it, not at the end.

| suite | tier | what a failure points at |
|---|---|---|
| selfcheck | none, no hardware | the comparators themselves. Nothing below can be believed. |
| support | none, no hardware | what is written and drawn around a verdict, never a verdict. |
| reference | none, no hardware | the expected answers: the coverage rule, the blend arithmetic, the texel address. The judge, not the machine. |
| clear | exact | the render target's memory layout and the path that copies it back. Everything below draws into that same target. |
| format | exact, ulp for floats | the render target format mapping for one format: its host format, its clear and its fragment output type. In an emulator: `video_core/surface.cpp` and `maxwell_to_vk.cpp`, and the clear of an integer target. Inside or outside the quad tells a conversion from a coverage fault. |
| raster | position for edges, exact for interiors | vertex fetch, the clip space to screen space transform, and the coverage rule. In an emulator: the viewport registers, the y direction, the half pixel offset, and the fill rule the host rasteriser is asked for. |
| intmath | exact | the shader translator, bit for bit, and the delivery of the fragment's own coordinate. In an emulator: one wrongly decoded or untranslated instruction (`shader_recompiler/frontend/maxwell/translate/impl/`; the log names the instruction, the failing family names the shader), or an integer render target mapped to a normalised host format. |
| blend | exact | the fixed function blend unit: the equation and factor registers, and the rounding on the way back to eight bits. |
| depth | exact | the depth test: the depth target's format and layout, the depth range set up by the viewport, and the compare operation. |
| texture | exact | the texture and sampler descriptors: the wrap mode and filter fields, and the block linear layout a sampled image is stored in. |
| coherence | exact | a cache between two uses of the same memory. In an emulator: the texture cache not marking an image the graphics unit wrote as modified, or missing a processor write to one (`texture_cache/texture_cache.h`, `MarkModification`, `WriteMemory`); the buffer cache serving a uniform buffer the processor rewrote, or dropping an update pushed inside the list (`buffer_cache/buffer_cache.h`, `Maxwell3D::ProcessCBData`); the copy engine's writes not reaching the images it overlaps (`engines/maxwell_dma.cpp`), or a render target not flushed before the copy engine reads it; or two formats over one memory joined as a conversion rather than a view (`texture_cache/texture_cache.h`, `JoinImages`). The region two pixels that still hold region one's contents say which. |
| fmath | units in the last place | the float side of the shader translator, and the transcendental unit behind it; for the edge cases, the minimum and maximum's NaN rule, denormal handling and the rounding modes. This is the only suite where two correct machines are allowed to differ, which is why it is last among the graphics suites. |
| nifm | exact | the network state a program is told: the connection status, the address, and a request's life from submitted to accepted (`hle/service/nifm/nifm.cpp`, `IRequest`, `HasConnection`). |
| dns | exact | name resolution: the two serialisations sfdnsres writes, nsd's placeholder, and, signed in, the rewrite table and the loopback redirect (`hle/service/sockets/{sfdnsres,nsd}.cpp`, `core/online_initiator.cpp`, `ResolveUrl`). |
| bsd | exact | sockets on the host: bind, datagrams, streams, a refused connect, a receive timeout (`SetSockOptImpl`, and `internal_network/network.cpp`), a connect that does not block, and the duplicate an ssl connection is handed, which shares the program's socket and dies with it (`hle/service/sockets/bsd.cpp`, `DuplicateSocketImpl`, `CloseImpl`). |
| ssl | exact | the secure connection: its defaults and getters, the descriptor it hands back, verification, an imported certificate, the handshake, and a request and its answer (`hle/service/ssl/ssl.cpp`, `ssl_backend_openssl.cpp`). |
| acc | exact | the account a signed in session reports and the id token it caches (`hle/service/acc/acc.cpp`, `IManagerForApplication`, `EnsureIdTokenCacheAsyncInterface`). |
| friend | exact | the blocked list and the profiles a session fetches (`hle/service/friend/{friend,profile_query}.cpp`). |

What comes next, in this order, each one its own change with its own critic:

- **the net family, sharper**: a rewrite whose destination has an address of its own, so that
  the two sfdnsres rewrite cases can tell a published rewrite from the session's loopback
  redirect (today the fixture's destination is `localhost`, which the redirect also gives); and a
  verifying handshake with a name the certificate does not carry, which must be refused.
- **another emulator**: the same binary under Ryujinx, with its own keys, and the two reports
  diffed against Eden's and against a console's. Nothing in this program depends on the emulator
  it runs under, and that run is what proves it.

Two rules hold across all of them. Every case computes what it expects on the processor and
checks every pixel, so there is no stored image anywhere and no machine's answer is the
reference for another's. And every rule a case judges by, wherever that rule is more than one
line of arithmetic, is implemented once in `source/` and pinned by the `reference` suite, which
runs with no graphics device at all: `make -f host.mk host-run` proves the judge before a
console or an emulator is involved.

Two things in the table are not suites and are not in it: the picture a failing case leaves on
the memory card, and the detailed mode that shows it. Both are support. Neither decides
anything, neither is compared to anything, and no run's answer changes if either is turned off.
They are in the `support` suite's row only because that suite checks them, the way the
`reference` row is about the judge rather than about a machine. The two sections below say what
they are for and where the line between them and a verdict is drawn.

## What it reports

One JSON object per line, to two channels at once, because the two machines read different ones:

- the debug string service, which an emulator logs with no setup and hardware ignores unless a
  debugger is attached;
- a file on the memory card, which is what a console run leaves behind.

A case line carries its tier next to the result:

```json
{"type":"case","suite":"clear","name":"clear to opaque red","tier":"exact","result":"pass","detail":""}
```

The tier is on the line and not only in the source because a pass means nothing on its own: two
machines agreeing is worth something only if both were held to the same tolerance, and a reader
diffing two reports has no way to know which one a case used otherwise. The values are `exact`,
`ulp` and `position`, and one more that is not a tier at all: `none`.

`none` is for any line, in any suite, that reports no comparison of something the hardware
produced. That is the whole rule, and it is worth stating as a rule rather than as a list,
because the list is longer than it looks: the two suites that judge this program's own
arithmetic, every case that reports whether some precondition held rather than what came out of
it, from the device coming up to a shader loading to a texture reaching the graphics unit, and
any drawing case that was stopped before it could compare anything, which still reports the
reason in its detail. The tier describes the comparison the line carries, not the ambition of the
case that wrote it. Calling any of those `exact` would read, in a diff, as a passing claim about
a machine that was never asked anything.

A case that could not ask its question at all, because the backend it needs is not named or the
service it needs cannot be opened, says so with the result `skip`, the none tier and the reason
in its detail. It is counted apart from passes and failures, because a machine that was never
asked has agreed with nothing:

```json
{"type":"case","suite":"acc","name":"the id token refresh completes and succeeds","tier":"none","result":"skip","detail":"needs an online session: ..."}
{"type":"summary","passed":424,"failed":9,"skipped":27}
```

A run that dies halfway still leaves everything up to its last finished case, and the summary
line carries the counts so a runner does not have to parse the rest. The summary is the last
line of every run but one kind: a detailed run nobody attended adds one note after it, which the
section on the console below describes.

## Time per case

Every line a drawing suite reports ends with how long that case kept the graphics unit busy, in
microseconds, and every drawing suite ends with a line that sums its cases up:

```json
{"type":"case","suite":"clear","name":"clear to transparent cyan","tier":"exact","result":"pass","detail":"","us":231}
{"type":"perf","suite":"clear","timed":4,"total_us":7161,"min_us":192,"max_us":6530,"median_us":219,"submits":9,"samples":9}
```

**What is measured.** Every piece of work a case puts on the graphics unit goes through one
function in `source/gpu.c`, which reads the system tick when it closes a command list and again
when the queue stands idle after it. A case's time is the sum of those intervals since the line
before it: its clears, its uploads, its draw and the copy that reads the target back. A line with
no work since the previous one has no `us`: the device opening, a shader loading, a case judged
from a readback another case already made.

**What is not.** Recording the commands, the judge comparing pixels, building the line, writing
the report and writing a picture all happen outside those intervals. A number here is the
machine's work, not this program's, and a slower judge cannot move it.

**How it is repeated.** A drawing suite runs ten times. The first is a warm-up whose numbers are
dropped; the next eight are rehearsals with the report muted, so they write no line, count no
case and leave no picture; the tenth is the verdict pass, the only one a reader sees, and each of
its lines carries the smallest of that case's nine samples. The smallest because everything else
on the machine only ever adds time to a sample. Five runs of a build that kept every sample, on
this machine while other builds shared it, settled both choices: over the ten pairs, the minimum
of nine put 92 percent of cases within a tenth of the other run and the median of nine 79, and
going past nine bought almost nothing (93 at thirteen). The perf line's median is over the
suite's cases, the middle one or between the two middle ones rounded down, and `samples` says how
many runs each case's number is the smallest of. `submits` is how many submits the timed cases
made in the verdict pass, for the floor below.

**Why no verdict moves.** Each suite opens its own graphics device and closes it before it
returns, so a rehearsal leaves the verdict pass nothing to inherit but the emulator's shader
cache, which a second `make run` inherits anyway. The verdict pass is the same code it always was
with the report open. Checked against the build before this was added, run the same way: all
259 case lines were byte for byte the same once `"us"` was taken off, in the quick mode, the
detailed mode and with `images`, and the 315 pictures the `images` run wrote were identical.
Checked again on the build with the `format` suite, the grown `intmath` and `fmath` and the
`net` family: all 460 case lines the same, failures and skips included, in six quick runs and in
a `notime` run, and the pictures of the failures identical. The one allowance is the `detail` of
the net family's lines, which carries the ports and buffers the run was given and differs between
two runs of that build on its own; their suite, name, tier and result are the same.

**When a rehearsal hangs.** A hang now happens in a rehearsal first, before the suite has written
a case line, so the run says where in two places. The report carries one note before each drawing
suite's rehearsals:

```json
{"type":"note","text":"timing depth: 1 warm-up and 8 rehearsals before the verdict pass"}
```

A report that ends on that note stopped while the suite it names was being timed. The debug
channel, and only it, gets a line before every rehearsed case, so the emulator's log then ends on
the case that was about to run:

```
EDENPROBE TIMING depth pass 4 case a fragment nearer than the buffer passes
```

The pass counts from one, the warm-up first. The warm-up has no pass before it to have learnt
the names from, so its lines number the case instead, `pass 1 case number 3 after a depth clear
writes the value it was given`, which is the case after the one named; a number one past the
suite's last case is the work after it, closing the device. The log line means something only
when the report ends on the timing note: a hang in the verdict pass leaves the report ending on
the case line before the hang, as it always did, and the log's last timing line is then a stale
one from the last rehearsal, pointing at a device that closed cleanly. On a console with no
debugger the note is all there is. Either way, `notime` in the mode file (`make run MODE="quick
notime"`) skips the warm-up and the rehearsals entirely: every drawing suite runs once, no case
line carries `us`, no perf line and no timing note is written, and the mode note ends in `no
timing passes`. That is the old drawing run case for case, plus the support cases this feature
adds (nineteen, all checks of the timing itself), and the way to get the case line a hang stops
at, or the old behaviour for any other reason.

**What a number can see.** Every submit costs a round trip, whatever it carries. Under eden-cli,
over the 374 case lines under two milliseconds in two runs at a load near eight, a case's time
is about 145 microseconds plus 31 per submit, or 80 per submit taken alone (the middle half
between 73 and 102). The targets are 64 by 64, so for most cases that round trip is the whole
number: a case of three submits takes about 235 microseconds, which is its floor and not its
draw. The critic proved it the direct way, on three runs of a build with one case changed each,
40 comparisons of three runs a side:

| one case changed to | its time | listed |
|---|---|---|
| read its target back twice | 190 to 270 microseconds to 320 to 370 | 40 of 40 |
| clear twice | about 40 more, inside the noise | 0 of 40 |
| draw its triangle twice, or sixteen times | no change | 0 of 40 |
| upload its 4 by 4 texture once more | 45 to 70 more, one round trip | 0 of 40, unsteady in 20 |

So under an emulator what this can watch is the readbacks and the first case of each suite,
whose time is the device's first work (below). A clear, a draw or an upload of the size these
suites use costs less than one round trip, about a hundred microseconds, and sits under the
floor. The perf line's `submits` times the per-submit cost is the floor under a suite's total;
what is above it is the work. A console's floor is unknown until one has run it.

Two kinds of case are dominated by something other than their draw, and the warm-up fixes
neither, because every pass opens a fresh device and the emulator pays again for each one:

- the first case of every suite, which is the first work on a new device: 6 to 10
  milliseconds where that work is a clear, a draw and a readback. It is not a shader being
  compiled: the `clear` suite has no shader and pays it too. In `texture` and `coherence` the
  first work is an upload, and that case's 2 to 2.9 milliseconds is the device's first work
  too, not the upload, which the table above shows costs one round trip;
- the first draw that samples a texture, 14 to 18.5 milliseconds in `texture` and `coherence`.

And those two first cases are not one number at all: "the texels reach the textures" in
`texture` and "the starting texels reach the textures" in `coherence` take 2 to 2.9 milliseconds
in most emulator processes and anything from 60 microseconds to 1.6 milliseconds in about one in
five, the whole process in that mode for that case. A side of three that holds one such process
has a spread past the unsteady limit below, and with one process in five that is about half the
sides: there perf.py cannot see a move in either case and names it as unsteady instead (the
`texture` one was set aside in 7140 of the 9240 splits below, the `coherence` one in 4620).

**How stable it is, and the method that follows.** Inside one process the smallest of nine
samples is steady. Between processes it is not: one emulator process runs nearly every case
about 13 percent slower than the next, now and then, and no number of samples inside a process
can see that. So one report is one sample of the process, and a comparison takes three runs a
side, both sides run in the same sitting:

```sh
tools/perf.py a1.jsonl a2.jsonl a3.jsonl -- b1.jsonl b2.jsonl b3.jsonl
```

A case's time on a side is its median over the three, which needs two processes to agree; the
smallest over them was tried and lost, because the one fast process then decides the side: with
the same limits it listed a move in 9 of 840 splits of nine runs and 16206 of 185640 of eighteen.
A move is listed when it is more than 20 percent **and** more than 100 microseconds, and a case
whose three runs on either side are more than 75 percent apart is named as unsteady and not
compared.

Which of the three does the work, measured on twelve runs of this build in one sitting (six at a
load of 4 to 5.5, six at 1 to 14), every way to split them into two sides of three, 9240
comparisons of two runs of one build:

| limits | comparisons listing a move |
|---|---|
| 20 percent alone | 840 |
| 20 percent and 100 microseconds | 840 |
| 20 percent and unsteady at 100 percent, with or without the floor | 91 |
| 20 percent, 100 microseconds, unsteady at 75: the defaults | 0 |
| 30 percent, 100 microseconds, unsteady at 100 | 0 |

The percent is the brief's 20 and is not what keeps noise out. The unsteady rule is: everything
the lesser limits listed was one of the two first-work uploads above. At 75 it sets aside those
two and nothing else on these twelve runs (on nine runs of the build before `format`, one
intmath case that one process ran fast was set aside too, in 260 of 840 splits); at 50 steady
`blend` cases start being set aside. The floor is what keeps the quarter-millisecond cases quiet
on a noisier day: over the critic's nine runs of the build before `format`, twenty percent with
no floor listed a move in 76 of 1680 ordered splits, and none with it. And 30 would have hidden
a 20 to 30 percent move in exactly the cases this can watch.

Agreement between the two sides, same twelve runs: cases within a tenth, 85.4 percent at worst
and 98.0 typical; within a fifth, 99.0 at worst; suite medians within 10.1 percent. The six runs
at a load of 4 to 5.5 alone: every case within a tenth in all ten splits, suite medians within
4.7 percent. Six more runs of the build as it lands, with the `net` family, at a load near nine:
no split listed a move, cases within a tenth 99.0 percent at worst, suite medians within 7.5
percent, the `texture` upload set aside in all ten. One report a side is still accepted, for a
quick look, but 32 of the 66 pairs of the twelve, and 8 of the 15 of the six, list a case that
did not change.

Both sides in the same sitting is part of the method, not advice. Pooling those eighteen runs,
which came from two sittings about an hour apart at different loads, the defaults listed a move
in 24135 of 185640 splits, nearly all of them the two first sampling draws, about 18.3
milliseconds typical in the first sitting and 14.8 in the second: the device's first work moves
with the machine more than anything else does. Run the two builds alternately if the machine is
shared. The perf line and the case's `us` stay one process's figure; the method lives in the
comparison.

**Comparing two runs.**

```sh
tools/perf.py a1.jsonl a2.jsonl a3.jsonl -- b1.jsonl b2.jsonl b3.jsonl
tools/perf.py before.jsonl after.jsonl      # one run a side
tools/perf.py report.jsonl                  # the per-suite table of one run
tools/perf.py r1.jsonl r2.jsonl r3.jsonl -- # the table of one side of three
```

It lists the cases that moved, largest first, then the unsteady ones, then the cases only one
side timed, then a table per suite: cases timed, the two totals, the two medians and the change
in each. `--threshold`, `--floor` and `--unsteady` change the three limits; each must be a
finite number of nought or more. It reads the case lines, not the perf lines, and skips a line
it cannot parse with a count on standard error, so a report cut off by a killed run still
compares; a line that is not UTF-8 is skipped and counted the same way. A report with nothing
timed, an empty file, a `notime` run or a report from before cases were timed, is refused on
standard error with exit status 1 wherever it stands, alone or as one run of a side, rather than
compared as a hundred percent faster; and a suite only one side timed says so in the table
instead of a percent. Its tests run with the host suite.

## What a failing case leaves behind

A verdict says how many pixels were wrong and what the first one held. It cannot say that the
wrong ones were the right shape in the wrong place, or one block out of sixteen, or every other
column. So a drawing case that fails writes what it saw next to the report, and names the files
on its own line, which is one line and is only wrapped here to fit:

```json
{"type":"case","suite":"intmath","name":"bitwise and","tier":"exact","result":"fail",
 "detail":"4095 of 4096 pixels wrong, first at (1,0) is 0x00000000, expected 0x00106000",
 "image":"images/intmath/bitwise-and.pam","expected":"images/intmath/bitwise-and-expected.pam",
 "diff":"images/intmath/bitwise-and-diff.pam","raw":"images/intmath/bitwise-and.bin"}
```

Each field is a path relative to the directory the report is in, which is the one holding
`report.jsonl`, and each appears only when that file was written, so every line a run produced
before this existed still reads the same. The files land under `images/<suite>/`, named after
the case with the letters lower cased and every run of characters that is not a letter or a
digit turned into a single dash: "the fragment coordinate's x is the pixel centre, bit for bit"
becomes `the-fragment-coordinate-s-x-is-the-pixel-centre-bit-for-bit.pam`, one dash for the
apostrophe and one for the comma and the space together. Two case names that spell one file
name get a file each: the second is numbered, `-2` at the end of the name and before the
extension, giving up characters off the end of the name if that is what it takes to make room
for the number. A name is held together with the two the pictures beside it are spelled from,
so a case called "bitwise and expected" cannot land on the expectation of one called "bitwise
and". If the numbers run out, after ninety eight of them, the case is left with no picture at
all and its line simply carries none of the four fields, because one failure quietly
overwriting another's evidence is worse than a missing picture and far worse than a name
nobody likes. The same goes for the directory the whole lot lives in: one too long to hold
whole is refused outright rather than cut short into a place nobody named.

This is not a stored reference image and it must never become one. The README's first section
says why a hash of a frame breaks between two machines that are both right, and that is still
true of these files. Nothing compares them to anything, no case reads one, and none of them can
change an answer: the readback is judged, and then the very bytes the judge looked at are
written. If every image were deleted the run would report exactly what it reported before.

**The formats.** All three pictures are binary PAM, which is the one netpbm format that carries
four channels, so the alpha of a colour target is in the picture rather than in a second file
beside it that can go missing. `magick`, `identify` and the netpbm tools read them as they are;
anything that wants a PNG is one `magick a.pam a.png` away.

| file | what it is |
|---|---|
| `<case>.pam` | what the machine drew, four channels, MAXVAL 255, labelled `drawn` on screen |
| `<case>-expected.pam` | what the processor worked out for that same pixel |
| `<case>-diff.pam` | three channels: the judge's verdict per pixel, labelled `judged` on screen |
| `<case>.bin` | the target's bytes as they were read back, for the formats an image cannot hold |

The four bytes of a pixel go into the image in the order the readback handed them over, and
that one rule covers every format the suite uses. For a colour target that is red, green, blue
and alpha and the picture is the picture. For a target holding 32 bit integers or floats it is
the little end first, which makes a picture of bit patterns rather than of magnitudes: equal
values look equal and one changed bit changes a colour, which is what a reader of an integer
target wants, while a float range has no honest mapping onto a byte and inventing one would
make the picture a judgement. Those formats also get their bytes verbatim in a `.bin`, so
`cmp` between two machines has something exact to work on.

**The diff is the judge's verdict and not a comparison of the two images**, and that distinction
is the whole reason this is safe. Dark grey means the judge accepted the pixel, red that it
refused it, blue that it declined to judge it at all, which is the band an edge runs through in
the rasterisation cases. A fourth colour, magenta, means no compare loop ever looked at that
pixel. It is there because the alternative was to start every pixel at one of the other three,
where a loop that skipped one would be indistinguishable from one that meant to leave it alone;
a run that shows magenta has a bug in this program and not in the machine. A pixel inside a
budget of units in the last place differs in its bytes and is not a difference here. An image
comparison would paint the entire float suite red on a machine that had done nothing wrong,
which is precisely the failure the suite exists not to report.

By default only a failing case writes anything, because a green run would otherwise leave three
files for every drawing case and nobody asked for them. A run that wants them all says so with
the word `images` in the `mode` file described in the next section. The `support` suite is the
one exception and only while it runs: it writes a handful of eight by four fixtures into
`support-check/` beside the report, parses them back, and then takes them away again, which is a
case of its own. A run that leaves that directory behind has failed that case and says so.

## Looking at it on the console

One binary, two ways to run it, and a file on the memory card that chooses.

**Detailed** is the default, because the person who starts this on a console is standing in
front of the screen: every suite runs, both report channels get every line, the summary is
counted and the report is closed, and then the cases go on the screen one at a time: the three
pictures above, side by side and labelled `drawn`, `expected` and `judged`, each 64 by 64 target
scaled up with nearest neighbour so it is legible, with the suite, the case, the tier, the
verdict and the detail as text around them, and a key naming each of the four colours `judged`
is made of. It is for the question a verdict cannot answer, which is whether a failure looks
like a rendering fault or like the judge being wrong.

**Quick** is what a script wants: every suite, both channels, and the program exits, about six
to seven seconds under eden-cli (6.4 to 7.3 in most runs at a load of 4 to 9, emulator start
included) and up to several times that when the machine is busy. It is the
same run up to the report closing, and then it stops.

What the screen holds is every case that compared pixels, pass or fail, which on a full run is
198 of the 479 and is the number the heading counts in. The other 281 compared nothing the
graphics unit produced and have no picture to show. A case that failed before it read a target
back has no picture either, so it is not in the list; when that happens the screen says how
many of the run's failures are missing from it rather than letting the count quietly disagree.
It holds them in memory rather than reading back what it wrote, so a green run and a run that
wrote nothing are both worth looking at, and the `images` word changes what is on the memory
card and not what is on the screen.

The order is the safeguard and it is not a detail of the implementation. Every suite runs, every
line is emitted, the summary is counted and the report is closed, and only then does anything
reach a screen; the viewer refuses to start while a report is open. So the two modes leave the
same verdicts, line for line, and the same images beside them. The report differs in the one
note near the top that says which mode ran and why, and in the one case below. The screen is a
second opinion for a person, never an input to one.

**Nobody there.** A detailed run whose screen sees no button pressed for ten seconds after the
report closed leaves by itself, and appends one line after the summary saying so:

```json
{"type":"note","text":"detailed mode: nothing pressed for 10 seconds after the report closed, left by itself"}
```

That is for a script that forgot the mode file, which must not leave an emulator running until
something kills it. Ten seconds rather than fewer because on a console the person is reading the
first case when the count starts, and must not lose it. It is a press and not a connected
controller that counts, because an emulator presents a controller as connected whether or not
anyone is holding it. The screen counts the seconds down on its bottom line, and any button at all
stops the count for the rest of the run. The first eight frames of the screen are not a press, for
the same reason they are not navigation: a controller arriving with the window reports its whole
mask as newly pressed.

**The mode file** is `sdmc:/edenprobe/mode`, a few words in any order and any case, anything that
is not a letter between them, one per line or all on one:

| word | what it does |
|---|---|
| no file, or no `quick` in it | detailed |
| `quick` | the automatic run: every suite, the report, exit |
| `detailed` | detailed even beside `quick`, the same as holding X |
| `images` | a picture for every drawing case, passing ones too, on either mode |
| `notime` | no timing passes: each drawing suite runs once, and no line carries a time; see "Time per case" |
| `graphics` | the graphics family: the nine drawing suites, clear to fmath; selfcheck, support and reference always run, whatever family is named |
| `net` | the net family: nifm, dns, bsd, ssl, acc and friend, against what `sdmc:/edenprobe/net` names |

Families choose what runs beyond the judge. None named means every family this build has;
naming some runs those, and a named family this build does not have is listed in the mode line
and skipped, so a file written for a later build is reported rather than refused. `net` alone
runs the judge and the six net suites, which is what the fixture below writes. The judge itself,
selfcheck, support and reference, always runs, because nothing a family reports can be believed
without it. Any other word is reported in the mode line, quoted and counted, and ignored: a typo
costs a line in the report, never the run.

The first 4096 bytes of the file are read, which is every word there is many times over. A
longer file is read that far, the mode line says "mode file longer than 4096 bytes, the rest
ignored", and a word the limit cuts in two is dropped rather than reported as an unknown word
nobody wrote. A directory called `mode` is read as no file, and the mode line says that too.

```json
{"type":"note","text":"mode: quick, from the mode file; pictures of failures only; families graphics; 2 unknown words ignored: grpahics fast"}
```

**Holding X** as the program starts is the other way into the detailed mode, for a person with a
card that says `quick` and no way to change it. The pad is asked only then, since with no
`quick` the run is detailed already; it is asked once it turns up, which on a machine with a
controller already attached is a frame or two, and the wait is twelve frames at the very most.
The report is opened before any of this, so a machine that cannot read the file or reach the pad
still leaves a report saying how far it got.

On the screen: **A** or right for the next case, **B** or left for the previous one, **down**
and **up** for the next and previous failure, **X** to cycle between the three side by side and
each one on its own, and **plus** to quit. The frame is composed on the processor and only when
something moved or the countdown ticked, so a still screen costs an emulated machine nothing.

## Versions

The version is the one line of `VERSION`, semantic (`0.1.0` is the first one), and a tag
`v<version>` on `main` marks each. Every report opens with a note that names it and the build
it came from, as git describes the tree: `edenprobe 0.1.0 (9632b63) starting`, or `-dirty` when
uncommitted changes were built. Two reports are worth diffing only when that line agrees, or
when the difference between the two builds is the thing being measured. The version moves when
what a case asks or how it judges changes; a new suite or a new case is a minor step, a changed
verdict for an existing case is at least that, and the README's counts move with it.

The cover a loader shows beside the file is `icon.jpg`, drawn from `static/icon.svg` (the reden
mark, the Eden sunset without the palm, on the site's dark ground) by `make icon`; the JPEG is
committed so a build needs neither drawing tool.

## Building

For the console, with the toolchain installed:

```sh
export DEVKITPRO=$HOME/devkitpro
make
```

That produces `edenprobe.nro`. The same file runs on hardware and in an emulator, unchanged: that
is the point, since the two reports are only comparable if the binary is identical.

There is also a host build, which compiles the parts that touch no console and runs them on the
machine you are sitting at:

```sh
make -f host.mk host-run
```

It covers the suite's judgement of itself: that two neighbouring floats measure one unit apart,
that a zero budget rejects them, that a NaN is never equal to anything including itself. If those
fail, nothing else the suite says can be believed, and you find out in a second instead of after
a boot. It also covers what is written and drawn around a verdict, which has no console in it
either: the picture writer against a fixture it then parses back, the detailed mode's layout
and navigation against a frame composed in ordinary memory, the words of the mode file, and the
time a case reports. Last it runs the tests of `tools/perf.py`, which needs Python 3 and nothing
else.

Commit messages have a convention too: `edenprobe: what changed`, one to three lines of intent,
one Signed-off-by that is also the author and the committer, ASCII, no merges. `git config
core.hooksPath hooks` once per clone turns the check on; `sh hooks/test-commit-msg.sh` shows
what it accepts and `--history <range>` runs it over existing commits.

## Running it under an emulator

```sh
make run
```

`EMULATOR` points at the binary to test, and is a variable on the command line:
`make run EMULATOR=/path/to/emulator`. With nothing given it is the eden-cli build kept in
`~/WorkDir/accord/bin`.

The run leaves `report.jsonl`, and `images/` beside it, in `sdmc:/edenprobe`. On the console
that is the memory card. On a desktop the emulator keeps its memory card in an ordinary
directory under `$XDG_DATA_HOME`, and its settings and caches under `$XDG_CONFIG_HOME` and
`$XDG_CACHE_HOME`. `make run` points all three at `build/xdg/` inside this tree before it starts
the emulator, so the report is at `build/xdg/data/eden/sdmc/edenprobe/report.jsonl`, the last
line of the target prints that path, and `make clean` takes the lot away. That is not tidiness.
An emulator's frontend saves its settings when it exits, so a run pointed at your own
directories reads your settings and may rewrite them; one pointed here starts from the
emulator's defaults every time, which also makes two runs comparable. The target deletes the
previous report, its pictures and the support suite's scratch directory first, so a run that
never started cannot leave an old report looking like its own, and a green run cannot sit beside
the pictures of an earlier failing one. An emulator path that is missing or not executable, or a
run that leaves no report or a report with no summary line (the emulator is stopped at 60
seconds, and killed ten after that), stops the target with a line saying which, and a non-zero
exit.
Any emulator with a different layout puts the same names under whatever it calls its memory
card.

Before the emulator starts, the target writes the word `quick` into the mode file and says so,
because without it the run would stop on the detailed screen, which is for a person. `MODE` is a
variable too: `make run MODE="quick images"` keeps every picture, and `make run MODE=` writes no
file at all, so the screen comes up, counts down ten seconds and leaves by itself, which is the
way to see it from here.

The run needs a real rendering backend. An emulator set to a null or headless backend will still
report the selfcheck cases and still open a graphics device, and will then fail every drawing case
with every pixel reading zero, because a backend that draws nothing is behaving exactly as asked.
That is a property of the run, not a defect found, and a report is only worth diffing against
another machine when both were rendering.

A run like that is still worth doing, because it proves the things a null backend does not stand
in for. The graphics library runs on the emulated processor, not on the host, so everything it
checks about the commands it is given is checked for real: a shader module it will not load, a
copy it will not make, a rectangle it will not clear all end the program rather than fail a case.
A headless run that reports every case and reaches its summary has established that none of that
happened, which is most of what can go wrong before a pixel is involved.

## The net family

The net family needs to know what the machine's backend promises, and a file on the memory card
says it: `sdmc:/edenprobe/net`, one `key value` per line, `#` to the end of a line a comment, a
line it does not understand counted and named in the report and otherwise ignored.

| line | what it promises |
|---|---|
| `session <origin>` | an online session in the sense of reden's `docs/OnlineService.md` is configured against this backend, so the cases that ask one run |
| `account <16 hex digits>` | the account id its `/api/v1/config` publishes |
| `rewrite <source> <destination> <a.b.c.d>` | a rewrite it publishes, and the address the destination has |
| `blocked <16 hex digits>` | one entry of its `/api/v1/blocked` |
| `profile <16 hex digits> <url or -> <name>` | one entry of its `/api/v1/profiles`, the name running to the end of the line |
| `server <a.b.c.d> <port>` | a game server reachable over TCP and TLS; an address, not a name, so a server known by name is resolved by hand first |
| `server-name <name>` | the name to present to it, which its certificate has to carry as a subject alternative name for the verifying cases to pass |
| `server-cert <path>` | the certificate it presents, PEM, relative to `sdmc:/edenprobe` |

With no file every case that needs a backend is skipped and the rest still run: loopback sockets,
a dotted address resolving to itself, a connection's defaults. Without a `session` line nothing
asks acc or friend for anything that reaches a server, which on a console would be the platform's
own. A console run against a deployed game server names `server`, `server-name` and, when the
certificate is one no authority signed, `server-cert`, and nothing else changes: the same file
runs there and here. By hand, for a server at 203.0.113.7 whose certificate names
`game.example.org`, copied to the card as `sdmc:/edenprobe/game.pem`:

```
# sdmc:/edenprobe/net
server 203.0.113.7 443
server-name game.example.org
server-cert game.pem
```

And every key, as the fixture below writes it for a backend here (the session lines only make
sense against an emulator configured for that backend):

```
session http://127.0.0.1:41234
account bb00000100000002
rewrite game.edenprobe.invalid localhost 127.0.0.1
blocked bb00000100000009
blocked bb0000010000000a
profile bb00000100000002 http://127.0.0.1:41234/avatar/{size}.png edenprobe
server 127.0.0.1 8443
server-name redenex
server-cert net-server.pem
```

**Here**, `tools/net-fixture.sh` does all of it and takes it down again:

```sh
EMULATOR=/path/to/eden-cli tools/net-fixture.sh
```

It builds redenex from `REDENEX` (default `~/WorkDir/accord/redenex`) and starts it on a free port
with a certificate it makes for the name `redenex` (as a subject alternative name), starts reden's
reference online backend from `REDEN` through `tools/net-backend.py` on another free port with one
rewrite, a profile and two blocked ids to publish, and then asks that backend over its own
protocol (discovery, a token per scope, the three reads) what it publishes and writes the answers
into the net file, so an expectation is only ever what the backend said. It puts the three
`reden_online_*` settings in the scratch `XDG_CONFIG_HOME` that `make run` uses (eden-cli's
`eden/sdl2-config.ini`, group `[Services]`), runs `make run MODE="net quick"`, prints a table of
passes, failures and skips per suite, and stops both servers. The settings file is copied aside
before anything else happens and put back as it was whatever way the run ends, and the net file
and the certificate come off the scratch memory card, so a later plain `make run` skips rather
than fails against servers that are gone. redenex keeps its database on port 45432 (under
`~/.local/share/redenex`) and adopts one already running there; one that loses the race with
another redenex bringing that database up is started again, three tries in all, each with its own
log (`build/net/redenex-1.log` and on). Building it writes Go's own caches under the home
directory, as any `go build` does.

It fails loudly rather than print a table: a server that does not come up, `make run` exiting
non-zero (an emulator that is not there, a build error, a run with no summary), or a run whose
output has no `EDENPROBE DONE` line ends it with one line saying which and a non-zero exit, and
the previous report is deleted before the run, so nothing from an earlier run can be read as this
one's. `NET_CONFIG_DELAY=6` makes the backend hold `/api/v1/config` for six seconds, which is how
the sign-in wait is tested.

## Licence

GPL-3.0-or-later; the full text is in `LICENSE`. That lets code from other GPL-3 projects come
in, and everything the program links or follows from elsewhere is under a licence compatible
with it.
