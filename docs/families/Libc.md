# The libc family

The C library and what it asks of the system under it, the same cases on every frontend ([Frontends](../Frontends.md)):
`source/tests/t_libc.c`. Each case holds the library to what the C standard or POSIX defines, never to what one
library chose where the standard lets it choose (strerror's words, the sign a NaN prints with, `%a`'s first digit),
so the console's newlib, Android's bionic and a PC's glibc all pass, and an emulator or a translation layer that
answers otherwise is wrong. The word `libc` runs it ([The mode file](../ModeFile.md)); on the console only when named,
and the host build runs it every time.

Three suites, by what each needs from the machine; a frontend runs the ones its machine has:

- **libc**, nine cases, the library alone: `snprintf` of integers with widths, precisions and flags, and of floats
  rounded to their precision with infinities and NaNs as C words them; `sscanf`; `strtod` and `strtol` on hex floats,
  overflow with `ERANGE` and base 0's prefixes; the string functions; `qsort` and `bsearch`; `setjmp` and `longjmp`
  (1 for 0); `gmtime_r` and `strftime` of a fixed time in the C locale, which a program starts in whatever its
  environment says; and libm, each function within 1 ulp of its correctly rounded result (tier ulp; the table is
  `source/libcmath.h`, which `tools/libc-math.py` computes in decimal at 100 digits).
- **system**, two cases, threads and a clock: four threads adding under one mutex, a key's destructor for each and
  each thread's own thread-local; the monotonic clock over a 20 ms sleep.
- **socket**, three cases, the network stack on loopback (`PlatformSocketsOpen` first; a machine without it skips
  them as `unavailable`): a TCP connection that echoes, a UDP datagram there and back, and a connection to a closed
  port refused with `ECONNREFUSED`.
