#!/usr/bin/env python3
# The expected clear and drawn pixel of every format the format suite draws, worked out from the
# format's name alone (fields listed from the least significant bit up, little endian) and from
# the value rules restated here, sharing nothing with source/formatref.c. Prints the C table the
# reference suite pins FORMAT_TABLE against.
import re, struct, math
from fractions import Fraction as F

NAMES = """R8_Unorm R8_Snorm R8_Uint R8_Sint R16_Float R16_Unorm R16_Snorm R16_Uint R16_Sint
R32_Float R32_Uint R32_Sint RG8_Unorm RG8_Snorm RG8_Uint RG8_Sint RG16_Float RG16_Unorm
RG16_Snorm RG16_Uint RG16_Sint RG32_Float RG32_Uint RG32_Sint RGBA8_Unorm RGBA8_Snorm RGBA8_Uint
RGBA8_Sint RGBA16_Float RGBA16_Unorm RGBA16_Snorm RGBA16_Uint RGBA16_Sint RGBA32_Float
RGBA32_Uint RGBA32_Sint RGBX8_Unorm_sRGB RGBA8_Unorm_sRGB RGB10A2_Unorm RGB10A2_Uint
RG11B10_Float RGBX8_Unorm RGBX8_Snorm RGBX8_Uint RGBX8_Sint RGBX16_Float RGBX16_Unorm
RGBX16_Snorm RGBX16_Uint RGBX16_Sint RGBX32_Float RGBX32_Uint RGBX32_Sint BGR565_Unorm
BGR5_Unorm BGR5A1_Unorm BGRX8_Unorm BGRA8_Unorm BGRX8_Unorm_sRGB BGRA8_Unorm_sRGB""".split()

def layout(name):
    parts = name.split('_')
    body, kind, srgb = parts[0], parts[1].lower(), 'sRGB' in parts
    fields = []
    for letters, digits in re.findall(r'([A-Z]+)(\d*)', body):
        if len(digits) == len(letters) > 1 and sum(map(int, digits)) in (8, 16, 32):
            widths = [int(d) for d in digits]            # BGR565
        else:
            widths = [int(digits)] * len(letters)        # RGBA8, RG11, B10, BGR5
        fields += list(zip(letters, widths))
    shift, out = 0, []
    for letter, width in fields:
        out.append((letter, shift, width)); shift += width
    size = 1
    while size * 8 < shift: size *= 2
    if shift < size * 8 and 'X' not in body:
        out.append(('X', shift, size * 8 - shift))       # BGR5: bit 15 stored, not defined
    if kind == 'float' and body == 'RG11B10': kind = 'ufloat'
    return out, size, kind, srgb

def f32(v): return struct.unpack('<I', struct.pack('<f', v))[0]
PATTERN = [(0x9E3779B9 * (c + 1)) & 0xFFFFFFFF for c in range(4)]

def channel(letter, width, kind, srgb, drawn):
    c = 'RGBA'.index(letter)
    L = (1 << width) - 1
    if srgb and letter != 'A':
        if not drawn: return [0, 255, 0][c]
        return [188, 60, 255][c]                          # 1.5 clamps to 255
    if kind == 'unorm' or (srgb and letter == 'A'):
        if not drawn: return L if c == 3 else L * [1, 2, 3][c] // 5
        return [L - L // 5, L // 9, L, 0][c]
    if kind == 'snorm':
        m = (1 << (width - 1)) - 1
        v = [m // 3, -(m // 2), -m, m][c] if not drawn else [-(m * 4 // 5), m // 7, m, -m][c]
        return v & L
    if kind in ('uint', 'sint'):
        p = PATTERN[c] if not drawn else ~PATTERN[c]
        return p & L
    if kind == 'float':
        if width == 32:
            v = [0.5, -1.25, 1024.0, -3.75][c] if not drawn else [3.14159274, -1.0e30, 1.17549435e-38, math.inf][c]
            return f32(v)
        v = [0.5, -1.25, 1024.0, -3.75][c] if not drawn else [1.5, -2.25, 65504.0, 6.103515625e-05][c]
        return struct.unpack('<H', struct.pack('<e', v))[0]
    if kind == 'ufloat':
        v = F([0.5, 1.25, 3.0][c] if not drawn else [1.5, 96.0, 0.25][c])
        mant = width - 5
        e = math.floor(math.log2(v)); m = (v / F(2) ** e - 1) * (1 << mant)
        assert m.denominator == 1
        return ((e + 15) << mant) | int(m)
    raise ValueError(kind)

def pixel(name, drawn):
    fields, size, kind, srgb = layout(name)
    value, mask = 0, 0
    for letter, shift, width in fields:
        if letter == 'X': continue
        value |= channel(letter, width, kind, srgb, drawn) << shift
        mask |= ((1 << width) - 1) << shift
    return value.to_bytes(size, 'little'), mask.to_bytes(size, 'little')

def c_bytes(b): return '{ ' + ', '.join('0x%02X' % x for x in b) + ' }'
for name in NAMES:
    clear, mask = pixel(name, False)
    drawn, _ = pixel(name, True)
    print('        { "%s", %s,\n          %s,\n          %s },' % (name, c_bytes(clear), c_bytes(drawn), c_bytes(mask)))
