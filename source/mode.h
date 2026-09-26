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
///   notime    no timing passes: each drawing suite runs once, and no line carries a time.
///   stress    the heavy variants too: cases that ask a machine for far more than a game in
///             one list, off by default because an emulator can die of one and take the rest
///             of the run with it.
///   fatal     the cases in the may-crash table (maycrash.h), known to end the program on a
///             machine that gets them wrong: off by default, and never on the console, whose
///             processor lacks SM4.
///   retry     the cases on the crash list, which a machine's program ended inside before, run
///             again; one that runs to its end leaves the list. A case in the hang table stays
///             skipped until `hang` asks too.
///   hang      the cases in the hang table (hang.h), known to never return on a machine that
///             gets them wrong. Off by default. The start screen cannot turn this on.
///   nojournal nothing written between cases: no journal, so a crash costs the rest of the run.
///             For measuring what the journal costs, and for a card that must not be written.
///   cpu       a suite family: the processor's instructions, which run in every mode anyway, so
///             naming it alone is a run with no drawing and no network.
///   graphics  another; see below.
///   fs        another: reads from the memory card through the file system service, timed.
///   net       another: the online services, nifm to friend, against the backend the file
///             sdmc:/edenprobe/net names (see netref.h).
///
/// Families choose which suites beyond the judge run. None named means every family this build
/// has. The judge itself (selfcheck, support, reference) always runs, because no verdict below
/// it can be believed without it, and so does cpu, which needs no device. Any other word is reported and ignored, never fatal: a file
/// with a typo in it still gets a run, and a line in the report saying what was not understood.

/// The suite families, one bit each.
enum {
    MODE_FAMILY_GRAPHICS = 1u << 0,
    MODE_FAMILY_NET = 1u << 1,
    MODE_FAMILY_CPU = 1u << 2,
    MODE_FAMILY_FS = 1u << 3,
};

/// The families this build carries. A family named in the file and missing here is reported,
/// not run.
#define MODE_FAMILIES_PRESENT                                                                     \
    ((uint32_t)(MODE_FAMILY_CPU | MODE_FAMILY_GRAPHICS | MODE_FAMILY_FS | MODE_FAMILY_NET))

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
    /// The word `notime`: each drawing suite runs once, with no warm-up and no rehearsals, so
    /// the run is the single pass it was before cases were timed.
    bool no_timing;
    /// The word `stress`: the heavy variants of the cases that have one.
    bool stress;
    /// The word `fatal`: the may-crash table's cases run.
    bool fatal;
    /// The word `retry`: the crash list's cases run again.
    bool retry;
    /// The word `hang`: the hang table's cases run.
    bool hang;
    /// Asked for on the start screen rather than in the file, which the mode line says.
    bool fatal_by_screen;
    bool retry_by_screen;
    /// The word `nojournal`: no journal, and no run that goes on after a crash.
    bool no_journal;
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

/// The words that decide what a run reports, in one order, for the journal: a launch goes on
/// with a run only when the mode file still says the same. Quick and detailed are left out,
/// since they decide what happens after the report and not what is in it.
size_t ModeKey(char *out, size_t out_size, const RunMode *mode);

#ifdef __cplusplus
}
#endif
