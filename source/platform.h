// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// What a frontend gives the engine that its C library does not: the one place the engine and the
/// shared cases meet the machine they run on (docs/Frontends.md). Files, time and threads come
/// from the C library itself, which every frontend has: stdio, clock_gettime and pthreads, newlib
/// on the consoles, bionic on Android, glibc on a PC. Each frontend links one source that defines
/// these: source/platform_switch.c, source/platform_host.c, android's platform_android.c.

/// Writes one line to the machine's log: the debug string service on the console, which an
/// emulator logs; logcat on Android; standard output on a PC. @p line is @p length bytes, with no
/// newline. Every report line goes here as well as to the file, and so does the program's last
/// word, `EDENPROBE DONE ...`.
void PlatformLog(const char *line, size_t length);

/// Whether PlatformLog writes to standard output, where a case can read its own line back: the
/// host build's alone.
bool PlatformLogIsStandardOutput(void);

/// Why the last call below that answered false did: a sentence for the case's detail.
const char *PlatformError(void);

/// Brings up the sockets the shared socket cases use, and takes them down again. False when the
/// machine has none, and those cases skip with PlatformError's sentence.
bool PlatformSocketsOpen(void);
void PlatformSocketsClose(void);

/// A surface the shared drawing cases draw into and read back: RGBA, 8 bits a channel, rows from
/// the top. Only a frontend whose steps run the draw suite defines these. One open at a time, at
/// most PLATFORM_SURFACE_MAX pixels a side.
#define PLATFORM_SURFACE_MAX 256u
bool PlatformSurfaceOpen(uint32_t width, uint32_t height);

/// Fills the surface with one colour.
bool PlatformSurfaceClear(const uint8_t rgba[4]);

/// One triangle of one colour, its corners in pixels from the surface's top-left corner, x right
/// and y down, filled by the machine's own rasteriser with its usual rules.
bool PlatformSurfaceTriangle(const float xy[6], const uint8_t rgba[4]);

/// Every pixel into @p out: width times height times 4 bytes, rows from the top.
bool PlatformSurfaceRead(uint8_t *out);

void PlatformSurfaceClose(void);

#ifdef __cplusplus
}
#endif
