// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

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
///   noprogress no running screen while the suites run: the window keeps the screen before it,
///             as before there was one. For a machine whose window misbehaves while it draws.
///   listen    before the audio family's cases, a sequence of sounds for a person to hear, and
///             the audio family with it.
///   cpu       a suite family: the processor's instructions, which run in every mode anyway, so
///             naming it alone is a run with no drawing and no network.
///   graphics  another; see below.
///   fs        another: reads from the memory card through the file system service, timed.
///   audio     another: the audio renderer, recorded through its own effects and sinks. It
///             makes sound, so it runs only when named or with `listen`.
///   display   another: the display service's buffer producer, on a layer of its own. It runs only
///             when named until a console has shown what it answers.
///   kernel    another: the kernel's scheduler, threads of chosen priorities and cores with a
///             watchdog. It runs only when named until a console has shown its cases never hang.
///   video     another: the video image compositor, given input the processor wrote, and the
///             decoder, given VP9 jobs built from libvpx's test vectors. It runs only when
///             named until a console has shown its jobs never hang.
///   pad       another: a person presses each button the screen names and the run records what
///             arrived. It waits for a person, so it runs only when named.
///   service   another: commands a game sends to the system's services, each answer held to
///             reden's. It runs only when named until a console has shown what it answers.
///   net       another: the online services, nifm to friend, against the backend the file
///             sdmc:/edenprobe/net names (see netref.h).
///
/// And three that are not words, a name after each: `label=<name>`, the name of the folder the
/// run's report goes in beside this build's hash (ModeRunFolder), so runs of one build against two
/// emulators keep apart; `baseline=<name>`, the baseline file this run's times go into as a new
/// sample, and `compare=<name>`, the one they are held to (baseline.h). A name runs to the next
/// space or line end; letters, digits, dot, dash and underscore, at most MODE_LABEL_MAX, not
/// starting with a dot. Anything else is refused and said in the mode line: a refused label puts
/// the run in the folder with no label, a refused baseline or compare name leaves that out.
///
/// Families choose which suites beyond the judge run. None named means every family this build
/// has but audio, display, kernel, pad, service and video. The judge itself (selfcheck, support,
/// reference) always runs, because no verdict below it can be believed without it, and so does
/// cpu, which needs no device. Any other word is reported and ignored, never fatal: a file with
/// a typo in it still gets a run, and a line in the report saying what was not understood.

/// The suite families, one bit each.
enum {
    MODE_FAMILY_GRAPHICS = 1u << 0,
    MODE_FAMILY_NET = 1u << 1,
    MODE_FAMILY_CPU = 1u << 2,
    MODE_FAMILY_FS = 1u << 3,
    MODE_FAMILY_AUDIO = 1u << 4,
    MODE_FAMILY_VIDEO = 1u << 5,
    MODE_FAMILY_PAD = 1u << 6,
    MODE_FAMILY_KERNEL = 1u << 7,
    MODE_FAMILY_DISPLAY = 1u << 8,
    MODE_FAMILY_SERVICE = 1u << 9,
};

/// The families this build carries. A family named in the file and missing here is reported,
/// not run.
#define MODE_FAMILIES_PRESENT                                                                     \
    ((uint32_t)(MODE_FAMILY_CPU | MODE_FAMILY_PAD | MODE_FAMILY_GRAPHICS | MODE_FAMILY_FS |      \
                MODE_FAMILY_KERNEL | MODE_FAMILY_DISPLAY | MODE_FAMILY_VIDEO |                     \
                MODE_FAMILY_AUDIO | MODE_FAMILY_SERVICE | MODE_FAMILY_NET))

/// The families that run only when a word asks for them.
#define MODE_FAMILIES_ASKED                                                                       \
    ((uint32_t)(MODE_FAMILY_AUDIO | MODE_FAMILY_DISPLAY | MODE_FAMILY_KERNEL | MODE_FAMILY_VIDEO | \
                MODE_FAMILY_PAD | MODE_FAMILY_SERVICE))

/// What runs when the file names no family.
#define MODE_FAMILIES_DEFAULT ((uint32_t)(MODE_FAMILIES_PRESENT & ~MODE_FAMILIES_ASKED))

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

/// The longest label, and the longest build hash a folder name keeps.
#define MODE_LABEL_MAX 32u
#define MODE_HASH_MAX 48u

/// The longest folder name ModeRunFolder makes, the separator included.
#define MODE_FOLDER_MAX (MODE_HASH_MAX + 1u + MODE_LABEL_MAX)

/// A name a word carries after its sign, `baseline=<name>` or `compare=<name>`: the last good one,
/// or empty, and the last one refused, as written and cut to fit, for the mode line. A refusal
/// leaves an earlier good name alone.
typedef struct {
    char name[MODE_LABEL_MAX + 1u];
    bool was_refused;
    char refused[MODE_LABEL_MAX + 1u];
    bool refused_cut;
} ModeName;

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
    /// The start screen changed what runs beyond those two: the families, the listening
    /// sequence, quick or notime. The mode line then says the mode was chosen there.
    bool chosen_on_screen;
    /// The start screen wrote these words into the mode file, for the launches after this one.
    bool saved_by_screen;
    /// The word `nojournal`: no journal, and no run that goes on after a crash.
    bool no_journal;
    /// The word `noprogress`: no running screen.
    bool no_progress;
    /// The word `listen`: the listening sequence, and the audio family with it.
    bool listen;
    /// The family words in the file, whether or not this build has them.
    uint32_t families_named;
    /// How many words were not understood, and the first of them, space separated.
    uint32_t unknown_count;
    char unknown[MODE_UNKNOWN_TEXT_MAX];
    /// `label=<name>`: the name, or empty. With several, the last one the file has.
    char label[MODE_LABEL_MAX + 1u];
    /// The last `label=` refused, as written and cut to fit, for the mode line; empty when
    /// none was. A refused label leaves an earlier good one alone.
    bool label_was_refused;
    char label_refused[MODE_LABEL_MAX + 1u];
    bool label_refused_cut;
    /// `baseline=<name>`: this run's times go into sdmc:/edenprobe/baseline/<name>.jsonl as a new
    /// sample. Like the label, it decides nothing the suites report, so it stays out of the key.
    ModeName baseline;
    /// `compare=<name>`: this run's times are held to that file's samples once the suites end.
    ModeName compare;
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
/// since they decide what happens after the report and not what is in it, and so is the label,
/// since it decides the folder the journal is in.
size_t ModeKey(char *out, size_t out_size, const RunMode *mode);

/// The mode file's words for @p mode, as the start screen saves them: quick when the run leaves
/// once the report closes, then the words ModeKey keeps, nojournal, noprogress, and the label.
/// Words the file
/// had and nobody understood are not written. Returns the length, or 0 with an empty string when
/// @p out is too small.
size_t ModeFormat(char *out, size_t out_size, const RunMode *mode);

/// What the journal's chosen record keeps of a mode the start screen chose: the word `screen`,
/// then the words ModeKey keeps, then quick when the run leaves once the report closes. Never
/// empty, since a choice that turned every word off is still a choice.
size_t ModeChosenWords(char *out, size_t out_size, const RunMode *mode);

/// The mode a launch going on with a run runs in: @p from_file with what the start screen chose
/// when the run began, as ModeChosenWords wrote it. Everything ModeKey keeps comes from the
/// choice, and quick with it; the label and the file's other words stay the file's. NULL or an
/// empty text changes nothing.
RunMode ModeWithChosen(RunMode from_file, const char *chosen);

/// What a new run's journal keeps of its mode: in @p run_key, the words its run record holds,
/// which a launch that goes on with it must find in the mode file, the screen's when it saved
/// them there and the file's otherwise; in @p chosen, ModeChosenWords when the run's mode is not
/// what those words and the file say, and an empty string when it is. @p from_file is the mode
/// as the file and the pad left it, @p mode as the start screen did.
void ModeJournalWords(char *run_key, size_t run_key_size, char *chosen, size_t chosen_size,
                      const RunMode *from_file, const RunMode *mode);

/// Whether @p label is one a folder can be named after: 1 to MODE_LABEL_MAX letters, digits,
/// dots, dashes and underscores, the first not a dot.
bool ModeLabelValid(const char *label);

/// The name of the folder a run's report, pictures and journal go in, under
/// sdmc:/edenprobe/runs: the build's @p hash, and `_<label>` after it when the mode file has a
/// label. The hash keeps letters, digits, dots and dashes, every other character a dash, at
/// most MODE_HASH_MAX of them, and is `unknown` when it is empty or NULL; it has no underscore,
/// so the first one in a name is where the label starts. Returns the length, or 0 with an empty
/// string when @p out is too small.
size_t ModeRunFolder(char *out, size_t out_size, const char *hash, const RunMode *mode);

#ifdef __cplusplus
}
#endif
