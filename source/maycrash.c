// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT

#include <string.h>

#include "cpuref.h"
#include "maycrash.h"
#include "mode.h"

// Eden, and the fork before ECFE-2026-1163, throw Shader::NotImplementedException out of the
// pipeline cache on a shader with PRET, which ends the program; square.geom compiles to one.
#define GEOMETRY_PRET                                                                              \
    "its geometry shader has a PRET, and a shader recompiler that cannot translate PRET ends the " \
    "program (Eden, and the fork before ECFE-2026-1163)"

// The same for fetch_array.frag, the bcn suite's fragment shader.
#define BCN_PRET                                                                                   \
    "its fragment shader has a PRET, and a shader recompiler that cannot translate PRET ends the " \
    "program (Eden, and the fork before ECFE-2026-1163)"

static const MayCrash TABLE[] = {
    { "cpu", CPU_SM4_CASE, MODE_FAMILY_CPU,
      "the console's processor has no SM4, and a translator that cannot compile it ends the "
      "program (dynarmic before ECFE-2026-1158 throws Xbyak's 64-bit displacement error)" },
    { "geometry", "a geometry shader turns each of four points into its 8 by 8 square",
      MODE_FAMILY_GRAPHICS, GEOMETRY_PRET },
    { "geometry", "a point the geometry shader emits nothing for draws nothing",
      MODE_FAMILY_GRAPHICS, GEOMETRY_PRET },
    { "bcn", "every layer of a six-layer BC4 array keeps its own 2x2 and 1x1 levels",
      MODE_FAMILY_GRAPHICS, BCN_PRET },
    { "bcn", "both layers of a BC4 array six texels tall keep their own blocks",
      MODE_FAMILY_GRAPHICS, BCN_PRET },
    { "bcn", "the 2x8 second level of a 4x16 BC4 texture reads both of its blocks",
      MODE_FAMILY_GRAPHICS, BCN_PRET },
};

size_t MayCrashCount(void) {
    return sizeof(TABLE) / sizeof(TABLE[0]);
}

const MayCrash *MayCrashAt(size_t index) {
    return index < MayCrashCount() ? &TABLE[index] : NULL;
}

const MayCrash *MayCrashFind(const char *suite, const char *name) {
    for (size_t i = 0; suite != NULL && name != NULL && i < MayCrashCount(); ++i) {
        if (strcmp(TABLE[i].suite, suite) == 0 && strcmp(TABLE[i].name, name) == 0) {
            return &TABLE[i];
        }
    }
    return NULL;
}
