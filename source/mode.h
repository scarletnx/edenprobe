// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// What a run was asked to do, which is a few words in a file on the memory card and so belongs
/// somewhere the host build can check it with no card and no console.
///
/// The words, whole words only, in any order and any case, anything that is not a letter
/// separating them:
///
///   quick     run every suite and exit, which is what a script wants. Without it the run is
///             detailed: every suite, the report closed, then the cases on the screen.
///   detailed  the screen even when `quick` is there too, the same as holding X at launch.
///   images    a picture for every drawing case, passing ones included.
///   graphics  a suite family; see below.
///   net       another one, named now so a file written for a later build is not an error.
///
/// Families choose which suites beyond the judge run. None named means every family this build
/// has. The judge itself (selfcheck, support, reference) always runs, because no verdict below
/// it can be believed without it. Any other word is reported and ignored, never fatal: a file
/// with a typo in it still gets a run, and a line in the report saying what was not understood.

/// The suite families, one bit each.
enum {
    MODE_FAMILY_GRAPHICS = 1u << 0,
    MODE_FAMILY_NET = 1u << 1,
};

/// The families this build carries. A family named in the file and missing here is reported,
/// not run.
#define MODE_FAMILIES_PRESENT ((uint32_t)MODE_FAMILY_GRAPHICS)

/// How much of the mode file is read: every word there is fits many times over. A longer file is
/// read to here and the report says so; a word the limit cuts in two is dropped, never quoted as
/// an unknown one, since it is not a word anybody wrote.
#define MODE_FILE_MAX 4096u

/// What was at the mode file's path.
typedef enum {
    MODE_SOURCE_NONE,
    MODE_SOURCE_FILE,
    /// A directory by that name, which is read as no file and said so.
    MODE_SOURCE_DIRECTORY,
} ModeSource;

/// Room for the words nobody understood, as the report line quotes them. More than this and
/// the list ends in "..." while the count stays exact.
#define MODE_UNKNOWN_TEXT_MAX 96u

typedef struct {
    /// What was at the path. Nothing changes with it but the line that says so.
    ModeSource source;
    /// The file went on past MODE_FILE_MAX and the rest was not read.
    bool longer;
    /// The word `quick`.
    bool quick;
    /// Present the cases on screen once every suite has finished.
    bool detailed;
    /// Held X at launch, which can only ever add the detailed mode.
    bool detailed_by_button;
    /// Write a picture for a passing case too, which is off by default because a green run
    /// would otherwise leave a hundred files nobody asked for.
    bool every_case;
    /// The family words in the file, whether or not this build has them.
    uint32_t families_named;
    /// How many words were not understood, and the first of them, space separated.
    uint32_t unknown_count;
    char unknown[MODE_UNKNOWN_TEXT_MAX];
} RunMode;

/// Reads the words in @p text. NULL means there was no file, which reads like an empty one.
RunMode ModeFromText(const char *text);

/// Reads what was found at the path: @p length bytes of @p text for a file, nothing otherwise.
/// @p longer says the file went on past those bytes, and then text[length] must be readable: it
/// is the first byte left out, and when a word runs on into it, the part of that word that was
/// read is dropped rather than read as whatever its first letters spell.
RunMode ModeFromFile(ModeSource source, const char *text, size_t length, bool longer);

/// Adds what the button held at launch asks for, which can only be the detailed mode.
RunMode ModeWithButton(RunMode mode, bool detailed_button_held);

/// ModeWithButton over ModeFromText, for the host cases.
RunMode ModeAtLaunch(const char *text, bool detailed_button_held);

/// Whether the launch has to ask the pad about the button at all: only when the file asked for
/// a quick run, since the button can do nothing but add the detailed mode.
bool ModeAsksPad(const RunMode *mode);

/// Which of the @p present families to run.
uint32_t ModeFamiliesToRun(const RunMode *mode, uint32_t present);

/// The one line the report carries about the mode, without the braces around it. Returns the
/// length written; the text is always terminated.
size_t ModeDescribe(char *out, size_t out_size, const RunMode *mode, uint32_t present);

#ifdef __cplusplus
}
#endif
