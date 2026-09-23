// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// What a render target stores, worked out on the processor: which bits of a pixel hold which
/// channel, and how the value a shader or a clear hands over becomes those bits.
///
/// Touches no graphics library, so the reference suite pins every rule here with no device. The
/// format suite pairs each entry with the graphics library's own format by position, so the
/// order of FORMAT_TABLE is a contract with source/tests/t_format.c.
///
/// The layout rule, one line for all of them: a format's name lists its fields from the least
/// significant bit of the pixel up, and a pixel is stored little endian. RGBA8 is red in the
/// first byte; RGB10A2 is red in bits 0 to 9; BGR565 is blue in bits 0 to 4 and red in 11 to 15.
/// That is how the graphics library's own tables read (each format's texture swizzle picks
/// component nought for the channel named first), and the clear suite already proves it for RGBA8.

/// How one stored field turns a value into bits.
typedef enum {
    FORMAT_UNUSED = 0,
    /// Clamped to nought to one, times the largest code, rounded to the nearest code.
    FORMAT_UNORM,
    /// Clamped to minus one to one, times the largest positive code, rounded to the nearest
    /// code. Minus one is minus the largest positive code, never the one below it.
    FORMAT_SNORM,
    FORMAT_UINT,
    FORMAT_SINT,
    /// IEEE binary32 or binary16, by the field's width.
    FORMAT_FLOAT,
    /// The unsigned small floats with a five bit exponent: 11 bits with six of mantissa, 10 bits
    /// with five.
    FORMAT_UFLOAT,
    /// Linear to sRGB encoded, then as FORMAT_UNORM on eight bits. Alpha is never this kind.
    FORMAT_SRGB,
    /// Bits the format carries and does not define, such as the X of RGBX8. Never compared.
    FORMAT_UNDEFINED,
} FormatKind;

/// What the fragment shader has to declare for the target: a float, an unsigned or a signed
/// integer output. A mismatch is undefined, so it is part of the format rather than the case.
typedef enum {
    FORMAT_OUTPUT_FLOAT,
    FORMAT_OUTPUT_UINT,
    FORMAT_OUTPUT_SINT,
} FormatOutput;

typedef struct {
    uint8_t shift;
    uint8_t bits;
    uint8_t kind; ///< FormatKind
} FormatField;

/// The four channels in the order red, green, blue, alpha, plus the undefined bits if any.
typedef struct {
    const char *name;
    uint8_t bytes;
    uint8_t output; ///< FormatOutput
    FormatField channel[4];
    FormatField undefined;
} FormatInfo;

/// Every colour format the graphics library can render to and read back, in the order the
/// suite draws them.
extern const FormatInfo FORMAT_TABLE[];
extern const uint32_t FORMAT_COUNT;

/// The ones the library refuses as a render target, with the reason, for the suite's note.
typedef struct {
    const char *name;
    const char *reason;
} FormatRefused;
extern const FormatRefused FORMAT_REFUSED[];
extern const uint32_t FORMAT_REFUSED_COUNT;

/// The four 32 bit words a clear or a shader hands over: float bits for a float output, the
/// two's complement word for a signed one.
typedef struct {
    uint32_t word[4];
} FormatValue;

/// What the target is cleared to and what the quad draws, by rule rather than by table so a new
/// format cannot arrive without both. Every channel of the two differs, so a pixel that kept
/// the clear where the quad should be is wrong in every channel it stores.
///
/// Normalised channels are codes the conversion lands on with no rounding question: the nearest
/// float to a code over the largest code, whose product with the largest code in single precision
/// is exactly the code, for every code used at every width here (the reference suite checks it).
/// The specification's conversion must return an integer it lands on exactly, and may otherwise
/// pick either neighbour, so this is what gives one answer. The sRGB ones decode from a code and
/// encode back within a thousandth of it (three millionths, measured), so any converter
/// accurate to within half a code picks that code.
/// The drawn value also carries one channel above the range and one below it where the format
/// has them, so the clamp is judged.
FormatValue FormatClearValue(const FormatInfo *format);
FormatValue FormatDrawValue(const FormatInfo *format);

/// The largest pixel any format here has.
#define FORMAT_MAX_BYTES 16u

/// The bytes one pixel holds after @p value is written, and which bits of them are defined.
void FormatPack(const FormatInfo *format, const FormatValue *value,
                uint8_t bytes[FORMAT_MAX_BYTES], uint8_t mask[FORMAT_MAX_BYTES]);

/// The code one field stores for one word. Exposed so the reference suite can check the rules
/// one at a time rather than only through whole pixels.
uint32_t FormatEncodeField(FormatKind kind, uint32_t bits, uint32_t word);

/// The rules underneath, each one on its own.
uint32_t FormatUnorm(double value, uint32_t bits);
uint32_t FormatSnorm(double value, uint32_t bits);
double FormatSrgbEncode(double linear);
double FormatSrgbDecode(double encoded);
uint32_t FormatHalfFromFloat(float value);
float FormatFloatFromHalf(uint32_t half);
uint32_t FormatUfloatFromFloat(float value, uint32_t bits);
float FormatFloatFromUfloat(uint32_t code, uint32_t bits);

/// How far the product of a normalised channel and its largest code lands from the nearest code,
/// in codes, worked out in double precision. For the sRGB channels.
double FormatCodeMargin(FormatKind kind, uint32_t bits, uint32_t word);

/// Whether an unorm or snorm channel times its largest code, multiplied in single precision, is
/// exactly a whole number, or the channel is out of range and clamps. The property that makes the
/// normalised cases exact. True for every other kind.
bool FormatCodeExact(FormatKind kind, uint32_t bits, uint32_t word);

/// The size of the target the suite draws into, and the rectangle the quad covers. Aligned to
/// nothing in particular and not centred, so a flipped or transposed readback lands the quad
/// somewhere it is not.
#define FORMAT_TARGET_SIZE 32u
#define FORMAT_QUAD_X0 4u
#define FORMAT_QUAD_X1 20u
#define FORMAT_QUAD_Y0 12u
#define FORMAT_QUAD_Y1 28u

/// The quad as two triangles in clip space, six x and y pairs, so the reference suite projects
/// the very numbers the draw carries.
void FormatQuadClip(float out[12]);

/// Whether the pixel at @p x, @p y is inside the quad.
bool FormatInsideQuad(uint32_t x, uint32_t y);

#ifdef __cplusplus
}
#endif
