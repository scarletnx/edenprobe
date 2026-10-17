# Design

## Why not compare screenshots

Because two correct implementations are allowed to disagree, and the specification says so. The
graphics API requires division to land within 2.5 units in the last place, the logarithm within 3
outside the range from a half to two, the power of two within 3 plus twice the magnitude of its
argument, and the sine and cosine only within an absolute 2^-11, and only inside minus pi to pi.
Sub-pixel rasterisation precision is a limit each implementation reports, not a constant. A hash of
a shaded frame therefore breaks between two machines that are both right; the projects that took
that route keep one reference image per backend, driver and operating system, and still chase false
positives.

So the decision is made inside the program, on both machines, by the same code.

## The four tiers

Which tier a case uses is a claim about the hardware, and it belongs in the case.

- **Exact.** Bit for bit. Legal only where the result is defined exactly: integer render targets,
  integer arithmetic, bit patterns moved around. A float shader never qualifies, however tidy the
  numbers look on one machine.
- **Units in the last place.** For float work, with the budget stated per case.
- **Position.** The right answer, possibly a pixel or two away, for rasterisation coverage; and
  the order threads ran in, where which came first is the verdict and the time it took is not.
- **Level.** A level measured over a stretch of sound, within a fraction of the expected one
  stated per case: for the audio family, where two correct renderers differ in rounding and in
  their filters' taps.
