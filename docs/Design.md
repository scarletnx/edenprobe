# Design

## No screenshots compared

Two correct implementations may disagree, and the specification allows it: division within 2.5 units in the last
place, the logarithm within 3 outside a half to two, the power of two within 3 plus twice its argument's magnitude,
the sine and cosine within an absolute 2^-11 between minus pi and pi, and sub-pixel precision a limit each
implementation reports. A hash of a shaded frame breaks between two machines that are both right. So each case
works out what it expects and decides inside the program, with the same code on every machine.

## The four tiers

A case's tier is a claim about the hardware, and the case states it.

- **Exact.** Bit for bit, where the result is defined exactly: integer targets, integer arithmetic, bit patterns
  moved around. Never a float shader.
- **Units in the last place.** For float work, with the budget stated per case.
- **Position.** The right answer up to a pixel or two away, for rasterisation coverage; and the order threads ran
  in, where the order is the verdict and the time is not.
- **Level.** A level measured over a stretch of sound, within a fraction of the expected one stated per case: the
  audio family, where two correct renderers differ in rounding and in their filters' taps.
