// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "journal.h"
#include "mode.h"

#ifdef __cplusplus
extern "C" {
#endif

/// A run, the same on every frontend (docs/Frontends.md): the mode file read, the run's folder
/// made, the journal of the run before read and the plan made from it, the report opened, the
/// steps run in their order with the journal written between them, and the summary. A frontend
/// calls these in order, with its own screens, when it has any, between them; main.c is the
/// console's and android's frontend.c Android's.

/// What main runs as one, and the journal records as one: a suite, or a family run by one call.
typedef struct {
    const char *name;
    void (*run)(void);
    /// The family it belongs to, or nought for the steps every mode runs.
    uint32_t family;
    /// A drawing suite, timed with a warm-up and rehearsals before its verdict pass.
    bool timed;
    /// Its cases ask the machine something, so each says it started before its work.
    bool asks;
} RunnerStep;

/// A frontend: where its files are, which build it is, and the steps it has.
typedef struct {
    /// The probe's folder: the mode file, the crash list, runs/ and baseline/ go under it.
    /// sdmc:/edenprobe on the console. At most RUNNER_ROOT_MAX characters.
    const char *root;
    const char *version;
    const char *build;
    /// What the run's folder is named after (ModeRunFolder).
    const char *hash;
    const RunnerStep *steps;
    size_t step_count;
    /// The families this frontend's steps have, and of those, the ones that run only when the
    /// mode file names them.
    uint32_t present;
    uint32_t asked;
} RunnerFrontend;

#define RUNNER_ROOT_MAX 160u
#define RUNNER_PATH_MAX (RUNNER_ROOT_MAX + 16u + MODE_FOLDER_MAX + 16u)

/// A run, as the runner works it out. Large: a frontend keeps one, static.
typedef struct {
    const RunnerFrontend *frontend;
    char mode_path[RUNNER_ROOT_MAX + 8u];
    char crash_list_path[RUNNER_ROOT_MAX + 16u];
    char last_run_path[RUNNER_ROOT_MAX + 16u];
    char baseline_directory[RUNNER_ROOT_MAX + 16u];
    char runs_directory[RUNNER_ROOT_MAX + 8u];
    char run_directory[RUNNER_PATH_MAX];
    char report_path[RUNNER_PATH_MAX + 16u];
    char journal_path[RUNNER_PATH_MAX + 16u];
    /// What the card held about the run before this launch, the crash list, and the plan.
    JournalState journal;
    CrashList crashes;
    JournalPlan plan;
} Runner;

/// Names every path under the frontend's root and makes the root. False when the root is longer
/// than RUNNER_ROOT_MAX, and nothing is named.
bool RunnerInit(Runner *runner, const RunnerFrontend *frontend);

/// The mode file at @p path as it is: a directory there reads as no file, and a file longer than
/// MODE_FILE_MAX is read to there and said so.
RunMode RunnerModeFromFile(const char *path);

/// The run's folder for this build and @p mode's label, made if it is not there, every path under
/// it, and last-run naming it; then the pictures' root. Then what the card says before anything is
/// opened: the journal of the run before in that folder, the crash list, and the plan for this
/// launch from them, this build and @p from_file's words.
void RunnerPlan(Runner *runner, const RunMode *from_file);

/// The mode a launch runs in before any screen: the file's, with what the start screen chose
/// when the run it goes on with began.
RunMode RunnerModeAtStart(const Runner *runner, const RunMode *from_file);

/// The families of @p mode this frontend runs.
uint32_t RunnerFamilies(const Runner *runner, const RunMode *mode);

/// Opens the report the way the plan says, and the journal of a run that goes on; then the crash
/// list, the case the last launch ended inside put on it, and a note when the launch is not the
/// usual one.
void RunnerOpen(Runner *runner, const RunMode *mode);

/// A new run's journal: the run record, with the words a launch that goes on with it will find
/// in the mode file, the screen's when it wrote them there; what the screen chose, when the file
/// does not say it; and the first launch. @p from_file is the mode before any screen.
void RunnerBeginJournal(Runner *runner, const RunMode *from_file, const RunMode *mode);

/// The report's settings from @p mode, and for a new run the mode line.
void RunnerApplyMode(Runner *runner, const RunMode *mode);

/// Called as each step starts: its number among the steps this run has, done ones included.
typedef void (*RunnerProgress)(uint32_t number, const char *name, bool timed);

/// Runs every step of @p families this launch has left, in the frontend's order, each recorded in
/// the journal before and after; a timed step with its passes when @p timing is on.
void RunnerSteps(Runner *runner, uint32_t families, bool timing, RunnerProgress progress);

/// After the steps: the notes a run ends on, the baseline's lines, the summary; the report and the
/// journal closed, and `EDENPROBE DONE` with the counts on the log.
void RunnerFinish(Runner *runner, const RunMode *mode, bool timing);

#ifdef __cplusplus
}
#endif
