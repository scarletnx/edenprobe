// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Made by tools/libc-math.py: the correctly rounded results of the libc suite's libm case. Do not
// edit. X(name, the call, the result's bits); VD and VF read their argument through a volatile, so
// the compiler cannot fold the call into a constant of its own.

#pragma once

#define MATH_DOUBLES(X) \
    X(exp, exp(VD(1.0)), 0x4005bf0a8b145769ULL) /* 2.718281828459045 */ \
    X(log, log(VD(10.0)), 0x40026bb1bbb55516ULL) /* 2.302585092994046 */ \
    X(pow, pow(VD(2.0), VD(0.5)), 0x3ff6a09e667f3bcdULL) /* 1.4142135623730951 */ \
    X(sin, sin(VD(1.0)), 0x3feaed548f090ceeULL) /* 0.8414709848078965 */ \
    X(cos, cos(VD(10.0)), 0xbfead9ac890c6b1fULL) /* -0.8390715290764524 */ \
    X(tan, tan(VD(0.5)), 0x3fe17b4f5bf3474aULL) /* 0.5463024898437905 */ \
    X(atan, atan(VD(0.5)), 0x3fddac670561bb4fULL) /* 0.4636476090008061 */ \
    X(cbrt, cbrt(VD(2.0)), 0x3ff428a2f98d728bULL) /* 1.2599210498948732 */ \
    X(expm1, expm1(VD(1e-10)), 0x3ddb7cdfd9dda4e3ULL) /* 1.00000000005e-10 */ \
    X(log1p, log1p(VD(1e-10)), 0x3ddb7cdfd9d1d693ULL) /* 9.999999999500001e-11 */ \
    X(sinh, sinh(VD(1.0)), 0x3ff2cd9fc44eb982ULL) /* 1.1752011936438014 */ \

#define MATH_FLOATS(X) \
    X(sinf, sinf(VF(1.0f)), 0x3f576aa4U) \
    X(expf, expf(VF(1.0f)), 0x402df854U) \
    X(logf, logf(VF(10.0f)), 0x40135d8eU) \

