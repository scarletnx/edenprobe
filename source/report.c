// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// fileno and ftruncate, which the host build's C11 does not declare without asking.
#define _POSIX_C_SOURCE 200809L

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef __SWITCH__
#include <switch.h>
#endif

#include "report.h"

// The console reports through the debug string service, which an emulator logs for free and
// real hardware ignores unless a debugger listens. A host build has neither, and prints, so the
// same suite runs on a PC and its verdicts can be checked without a device.
static void SendToDebugChannel(const char *line, size_t length) {
#ifdef __SWITCH__
    svcOutputDebugString(line, length);
#else
    (void)length;
    puts(line);
#endif
}

static FILE *report_file;
static bool report_open;
static size_t passed;
static size_t failed;
static size_t skipped;
static size_t crashed;
static bool rehearsing;
static char rehearsal_suite[64];

// The journal, and whether every line goes onto the card before the program moves on. Both
// follow the journal: without it a line is flushed as it always was, and a crash costs the rest
// of the run as it always did.
static FILE *journal_file;
static bool journal_broken;

// What decides whether a case runs. The crash list is the file's, kept here and written back
// whole when a case joins or leaves it; the crashed set is this run's, which retry never runs.
static CrashList crash_list;
static char crash_list_path[64];
static bool retry_listed;
#define CRASHED_THIS_RUN_MAX JOURNAL_CRASHES_MAX
static JournalRecord crashed_this_run[CRASHED_THIS_RUN_MAX];
static uint32_t crashed_this_run_count;

// The case that said it started and has not ended, for the journal and for the check that
// every case of a family that asks the machine something says it started.
static bool starts_required;
static bool pending;
static char pending_suite[JOURNAL_SUITE_MAX];
static char pending_name[JOURNAL_NAME_MAX];
static uint32_t bookkeeping_notes;

static bool retaining;
static ReportRecord *records;
static size_t record_count;
static size_t record_capacity;

/// Sends one finished line to both channels.
///
/// The debug string service is the only one an emulator sees for free, and the only one that
/// costs nothing on hardware when nobody is listening. The file is what a console run leaves
/// behind for the memory card to carry back.
static void Emit(const char *line) {
    // Both channels stop together. The file already did, through its own handle; without this
    // the debug one stayed open, and one line written after the summary would reach the log a
    // runner greps and make the two modes' runs read differently.
    if (!report_open) {
        return;
    }
    SendToDebugChannel(line, strlen(line));
    if (report_file != NULL) {
        if (journal_file != NULL) {
            // On the card before anything else happens, as the journal's records are: a run
            // that goes on after a crash cuts the report back to a length the journal wrote
            // down, and every byte before it has to be there.
            char whole[REPORT_LINE_MAX + 2];
            snprintf(whole, sizeof(whole), "%s\n", line);
            JournalDurableAppend(report_file, whole);
            return;
        }
        fputs(line, report_file);
        fputc('\n', report_file);
        // Flushed per line on purpose: a run that hangs or is killed still leaves everything up
        // to the last completed case, which is exactly what a diff needs.
        fflush(report_file);
    }
}

/// Copies @p text into @p out with the escapes JSON requires, and nothing else: the strings
/// here are test names and short details written by this program, not arbitrary input. Hands
/// back how many characters of @p text made it in.
static size_t EscapeInto(char *out, size_t out_size, const char *text) {
    size_t w = 0;
    size_t r = 0;
    for (; text[r] != '\0' && w + 2 < out_size; ++r) {
        const char c = text[r];
        if (c == '"' || c == '\\') {
            out[w++] = '\\';
            out[w++] = c;
        } else if (c == '\n') {
            out[w++] = '\\';
            out[w++] = 'n';
        } else if ((unsigned char)c < 0x20) {
            out[w++] = ' ';
        } else {
            out[w++] = c;
        }
    }
    out[w] = '\0';
    return r;
}

void ReportOpen(const char *path) {
    passed = 0;
    failed = 0;
    skipped = 0;
    crashed = 0;
    // Set whether or not the file below opens: a run with no memory card still reports through
    // the debug channel, and the guard this flag serves is about a run being under way rather
    // than about a file existing.
    report_open = true;
    report_file = fopen(path, "w");
    if (report_file == NULL) {
        // Not fatal. On hardware with no memory card, or an emulator pointed somewhere odd, the
        // debug channel still carries the whole run.
        static const char warning[] =
            "{\"type\":\"note\",\"text\":\"report file unavailable\"}";
        SendToDebugChannel(warning, sizeof(warning) - 1);
    }
}

/// Opens @p path for update at its end, which is what an append is here: a file opened to
/// append would put the byte a durable line writes again after the line rather than on it.
static FILE *OpenAtEnd(const char *path) {
    FILE *file = fopen(path, "r+");
    if (file != NULL && fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        file = NULL;
    }
    return file;
}

void ReportAppend(const char *path) {
    if (report_open) {
        return;
    }
    report_open = true;
    report_file = OpenAtEnd(path);
}

bool ReportResume(const char *path, uint64_t offset, bool cut, const JournalCounts *counts) {
    passed = counts != NULL ? counts->counts[JOURNAL_PASSED] : 0u;
    failed = counts != NULL ? counts->counts[JOURNAL_FAILED] : 0u;
    skipped = counts != NULL ? counts->counts[JOURNAL_SKIPPED] : 0u;
    crashed = counts != NULL ? counts->counts[JOURNAL_CRASHED] : 0u;
    report_open = true;
    report_file = fopen(path, "r+");
    if (report_file != NULL && cut &&
        (fflush(report_file) != 0 || ftruncate(fileno(report_file), (off_t)offset) != 0)) {
        fclose(report_file);
        report_file = NULL;
    }
    if (report_file != NULL && fseek(report_file, 0, SEEK_END) != 0) {
        fclose(report_file);
        report_file = NULL;
    }
    if (report_file == NULL) {
        static const char warning[] =
            "{\"type\":\"note\",\"text\":\"report file unavailable to go on with\"}";
        SendToDebugChannel(warning, sizeof(warning) - 1);
        return false;
    }
    return true;
}

bool ReportIsOpen(void) {
    return report_open;
}

void ReportClose(void) {
    report_open = false;
    if (report_file != NULL) {
        fclose(report_file);
        report_file = NULL;
    }
}

/// The word a tier goes onto the line as. An unrecognised value reads as "unknown" rather than
/// falling through to one of the real tiers: a diff between two machines is only worth something
/// if the tolerance each side allowed itself is on the line beside the result.
static const char *TierName(CheckTier tier) {
    switch (tier) {
    case CHECK_NONE:
        return "none";
    case CHECK_EXACT:
        return "exact";
    case CHECK_ULP:
        return "ulp";
    case CHECK_POSITION:
        return "position";
    }
    return "unknown";
}

/// Adds one optional field, and nothing at all when there is no such file. A path is built out
/// of lower case letters, digits, dashes and the separators this program put there, so it needs
/// no escaping and the one that did would be a bug rather than a string to quote.
static void AppendPath(char *out, size_t out_size, const char *key, const char *value) {
    if (value == NULL || value[0] == '\0') {
        return;
    }
    const size_t used = strlen(out);
    // Whole or not at all. A path cut in half leaves the string it sits in unterminated, and a
    // reader of the file gets a parse error rather than a line that is merely missing a field.
    const size_t needed = strlen(key) + strlen(value) + 6u;
    if (used + needed >= out_size) {
        return;
    }
    snprintf(out + used, out_size - used, ",\"%s\":\"%s\"", key, value);
}

size_t ReportBuildCaseLine(char *out, size_t out_size, const char *suite, const char *name,
                           const char *tier, bool passed_, const char *detail,
                           const PicturePaths *paths) {
    return ReportBuildResultLine(out, out_size, suite, name, tier, passed_ ? "pass" : "fail",
                                 NULL, detail, paths);
}

size_t ReportBuildTimedCaseLine(char *out, size_t out_size, const char *suite, const char *name,
                                const char *tier, bool passed_, const char *detail,
                                const PicturePaths *paths, const uint32_t *us) {
    return ReportBuildTimedResultLine(out, out_size, suite, name, tier,
                                      passed_ ? "pass" : "fail", NULL, detail, paths, us);
}

size_t ReportBuildResultLine(char *out, size_t out_size, const char *suite, const char *name,
                             const char *tier, const char *result, const char *reason,
                             const char *detail, const PicturePaths *paths) {
    return ReportBuildTimedResultLine(out, out_size, suite, name, tier, result, reason, detail,
                                      paths, NULL);
}

size_t ReportBuildTimedResultLine(char *out, size_t out_size, const char *suite,
                                  const char *name, const char *tier, const char *result,
                                  const char *reason, const char *detail,
                                  const PicturePaths *paths, const uint32_t *us) {
    char safe_suite[REPORT_KEPT_SUITE];
    char safe_name[REPORT_KEPT_NAME];
    char safe_detail[REPORT_KEPT_DETAIL];
    EscapeInto(safe_suite, sizeof(safe_suite), suite);
    EscapeInto(safe_name, sizeof(safe_name), name);
    EscapeInto(safe_detail, sizeof(safe_detail), detail != NULL ? detail : "");

    if (out == NULL || out_size < 3u) {
        if (out != NULL && out_size > 0u) {
            out[0] = '\0';
        }
        return 0;
    }

    // One byte held back for the closing brace, so it can be written unconditionally. What the
    // guard used to do instead was drop it whenever the content filled the buffer, which turned
    // a long line into something no reader could parse.
    const size_t room = out_size - 1u;
    // The reason, one of this file's own words, sits between the result it explains and the
    // sentence that explains it further.
    char reason_field[40] = "";
    if (reason != NULL) {
        snprintf(reason_field, sizeof(reason_field), ",\"reason\":\"%s\"", reason);
    }
    const int wrote =
        snprintf(out, room,
                 "{\"type\":\"case\",\"suite\":\"%s\",\"name\":\"%s\",\"tier\":\"%s\","
                 "\"result\":\"%s\"%s,\"detail\":\"%s\"",
                 safe_suite, safe_name, tier, result, reason_field, safe_detail);
    if (wrote < 0 || (size_t)wrote >= room) {
        // The caller's buffer cannot hold this case's line at all. An empty object still parses
        // and says nothing; half a line says something and cannot be read.
        snprintf(out, out_size, "{}");
        return strlen(out);
    }
    if (paths != NULL) {
        // The picture the machine produced first, then what was expected there, then the
        // judge's own verdict per pixel, then the bytes an image cannot hold exactly.
        AppendPath(out, room, "image", paths->image);
        AppendPath(out, room, "expected", paths->expected);
        AppendPath(out, room, "diff", paths->diff);
        AppendPath(out, room, "raw", paths->raw);
    }
    if (us != NULL) {
        // Last, after everything a verdict wrote, so a line with its time taken off is the line
        // this program wrote before it kept time.
        char field[24];
        const int length = snprintf(field, sizeof(field), ",\"us\":%u", (unsigned)*us);
        const size_t before = strlen(out);
        if (length > 0 && before + (size_t)length < room) {
            memcpy(out + before, field, (size_t)length + 1u);
        }
    }
    const size_t used = strlen(out);
    out[used] = '}';
    out[used + 1u] = '\0';
    return used + 1u;
}

size_t ReportBuildBreadcrumb(char *out, size_t out_size, const char *suite, uint32_t pass,
                             uint32_t index, const char *name, const char *previous) {
    if (out == NULL || out_size == 0u) {
        return 0;
    }
    if (name != NULL) {
        snprintf(out, out_size, "EDENPROBE TIMING %s pass %u case %s", suite, (unsigned)pass,
                 name);
    } else if (previous != NULL) {
        snprintf(out, out_size, "EDENPROBE TIMING %s pass %u case number %u after %s", suite,
                 (unsigned)pass, (unsigned)(index + 1u), previous);
    } else {
        snprintf(out, out_size, "EDENPROBE TIMING %s pass %u case number %u", suite,
                 (unsigned)pass, (unsigned)(index + 1u));
    }
    return strlen(out);
}

/// Says on the debug channel which case of the rehearsal comes next. Never to the file: the
/// report stays exactly the lines the verdict passes wrote, and the note before the rehearsals.
static void Breadcrumb(void) {
    // Only inside a timed pass: the support suite's own check of the mute has none, and a line
    // naming a pass nought would send whoever greps the log looking for a hang that never was.
    if (PerfPassNumber() == 0u) {
        return;
    }
    const uint32_t index = PerfCaseIndex();
    char line[256];
    ReportBuildBreadcrumb(line, sizeof(line), rehearsal_suite, PerfPassNumber(), index,
                          PerfNameAt(index), index > 0u ? PerfNameAt(index - 1u) : NULL);
    SendToDebugChannel(line, strlen(line));
}

void ReportRehearse(const char *suite) {
    rehearsing = suite != NULL;
    snprintf(rehearsal_suite, sizeof(rehearsal_suite), "%s", suite != NULL ? suite : "");
    if (rehearsing) {
        Breadcrumb();
    }
}

/// Keeps the case whose line was just emitted, for the screens. Never when the line was not
/// emitted, so nothing is on a screen that is not in the report.
static void Keep(const char *suite, const char *name, const char *tier, ReportResult result,
                 const char *detail, int32_t picture) {
    if (!retaining || !report_open || record_count >= REPORT_MAX_RECORDS) {
        return;
    }
    if (record_count == record_capacity) {
        size_t grown = record_capacity == 0u ? 256u : record_capacity * 2u;
        if (grown > REPORT_MAX_RECORDS) {
            grown = REPORT_MAX_RECORDS;
        }
        ReportRecord *const more = (ReportRecord *)realloc(records, grown * sizeof(*records));
        if (more == NULL) {
            retaining = false;
            return;
        }
        records = more;
        record_capacity = grown;
    }
    ReportRecordFill(&records[record_count++], suite, name, tier, result, detail, picture);
}

/// Copies what a reader of the line gets back for @p text, which the line escaped into a buffer
/// of @p size: the same characters, stopping where escaping stopped, and control characters as
/// the spaces the line turned them into.
static void KeepAsLine(char *out, size_t size, const char *text) {
    char escaped[REPORT_KEPT_DETAIL];
    const size_t kept = EscapeInto(escaped, size < sizeof(escaped) ? size : sizeof(escaped),
                                   text != NULL ? text : "");
    for (size_t i = 0; i < kept; ++i) {
        out[i] = (unsigned char)text[i] < 0x20 && text[i] != '\n' ? ' ' : text[i];
    }
    out[kept] = '\0';
}

void ReportRecordFill(ReportRecord *record, const char *suite, const char *name,
                      const char *tier, ReportResult result, const char *detail,
                      int32_t picture) {
    if (record == NULL) {
        return;
    }
    // Each buffer is the size of the one the line escapes that field into.
    KeepAsLine(record->suite, sizeof(record->suite), suite);
    KeepAsLine(record->name, sizeof(record->name), name);
    snprintf(record->tier, sizeof(record->tier), "%s", tier != NULL ? tier : "");
    KeepAsLine(record->detail, sizeof(record->detail), detail);
    record->result = result;
    record->picture = picture;
}

void ReportRetain(bool on) {
    retaining = on;
}

bool ReportIsRetaining(void) {
    return retaining;
}

const ReportRecord *ReportRecords(size_t *count) {
    if (count != NULL) {
        *count = record_count;
    }
    return record_count > 0u ? records : NULL;
}

/// The index PictureAt will answer for the picture PictureFinish just kept, or -1 when it kept
/// none; @p before is PictureCount from just before the finish.
static int32_t PictureKept(size_t before) {
    return PictureCount() > before ? (int32_t)before : -1;
}

/// A name as the journal and the crash list keep it: what a line can carry of it, and no more,
/// so a name longer than they hold still finds itself.
static void KeyOf(char *out, size_t out_size, const char *text) {
    size_t n = 0;
    for (; text != NULL && text[n] != '\0' && n + 1u < out_size; ++n) {
        const unsigned char c = (unsigned char)text[n];
        out[n] = c < 0x20u || c == 0x7Fu ? ' ' : (char)c;
    }
    out[n] = '\0';
}

/// A note about this program's own bookkeeping going wrong: a case that reported without saying
/// it started, or started twice. A few, and then one saying there were more.
static void Bookkeeping(const char *what, const char *suite, const char *name, const char *other) {
    ++bookkeeping_notes;
    if (rehearsing) {
        return;
    }
    if (bookkeeping_notes <= 20u) {
        ReportNote("bookkeeping: %s case \"%s\" %s%s%s%s", suite, name, what,
                   other != NULL ? " \"" : "", other != NULL ? other : "",
                   other != NULL ? "\"" : "");
    } else if (bookkeeping_notes == 21u) {
        ReportNote("bookkeeping: more cases than these twenty went wrong the same way");
    }
}

static void JournalCase(JournalKind kind, const char *suite, const char *key,
                        JournalResult result) {
    if (journal_file == NULL) {
        return;
    }
    JournalRecord record;
    memset(&record, 0, sizeof(record));
    record.kind = kind;
    KeyOf(record.suite, sizeof(record.suite), suite);
    KeyOf(record.name, sizeof(record.name), key);
    record.number = PerfPassNumber();
    record.result = result;
    ReportJournalWrite(&record);
}

/// Where a crash in this run happened, as a skip line says it.
static const char *PassWords(uint32_t pass) {
    return pass == 1u ? ", in the warm-up" : pass > 1u ? ", in a timed pass" : "";
}

static bool CrashedThisRun(const char *suite, const char *key, uint32_t *pass) {
    for (uint32_t i = 0; i < crashed_this_run_count; ++i) {
        if (strcmp(crashed_this_run[i].suite, suite) == 0 &&
            strcmp(crashed_this_run[i].name, key) == 0) {
            *pass = crashed_this_run[i].number;
            return true;
        }
    }
    return false;
}

static void WriteCrashList(void) {
    if (crash_list_path[0] == '\0') {
        return;
    }
    static char text[CRASH_LIST_MAX * (JOURNAL_SUITE_MAX + JOURNAL_NAME_MAX + 2u) + 512u];
    if (CrashListFormat(text, sizeof(text), &crash_list) == 0u ||
        !JournalWriteFile(crash_list_path, text)) {
        ReportNote("crash list: %s could not be written", crash_list_path);
    }
}

bool ReportStart(const char *suite, const char *name) {
    char key[JOURNAL_NAME_MAX];
    KeyOf(key, sizeof(key), name);
    if (pending) {
        Bookkeeping("started while", suite, key, pending_name);
        pending = false;
    }
    uint32_t pass = 0;
    if (CrashedThisRun(suite, key, &pass)) {
        char detail[REPORT_KEPT_DETAIL];
        snprintf(detail, sizeof(detail),
                 "the program ended inside it earlier in this run%s; it is on the crash list "
                 "(sdmc:/edenprobe/crashes), and the mode word retry runs it in a later run",
                 PassWords(pass));
        ReportSkip(suite, name, REPORT_REASON_CRASHED, detail);
        return false;
    }
    if (!retry_listed && CrashListHas(&crash_list, suite, key)) {
        ReportSkip(suite, name, REPORT_REASON_CRASHED,
                   "on the crash list (sdmc:/edenprobe/crashes): the program ended inside it in "
                   "an earlier run on this machine; the mode word retry runs it again");
        return false;
    }
    pending = true;
    KeyOf(pending_suite, sizeof(pending_suite), suite);
    memcpy(pending_name, key, sizeof(pending_name));
    JournalCase(JOURNAL_START, suite, key, JOURNAL_MUTED);
    return true;
}

void ReportRequireStarts(bool on) {
    starts_required = on;
}

size_t ReportBookkeepingProblems(void) {
    return bookkeeping_notes;
}

/// Closes the bookkeeping of a case that ended: the one that started, or one that never said.
static void Ended(const char *suite, const char *key, bool verdict) {
    if (pending) {
        if (strcmp(pending_suite, suite) != 0 || strcmp(pending_name, key) != 0) {
            Bookkeeping("ended while", suite, key, pending_name);
        }
        pending = false;
    } else if (verdict && starts_required) {
        Bookkeeping("ended without starting:", suite, key, NULL);
    }
}

/// A case retry asked for again that ran to its end, in the pass that counts, leaves the list.
static void Survived(const char *suite, const char *key) {
    if (!retry_listed || !CrashListRemove(&crash_list, suite, key)) {
        return;
    }
    WriteCrashList();
    ReportNote("retry: %s case \"%s\" ran to its end, so it leaves the crash list", suite, key);
}

void ReportCase(const char *suite, const char *name, CheckTier tier, bool passed_,
                const char *detail) {
    uint32_t us = 0;
    const bool timed = PerfCaseEnd(name, &us);
    char key[JOURNAL_NAME_MAX];
    KeyOf(key, sizeof(key), name);
    Ended(suite, key, true);
    if (rehearsing) {
        // The picture is closed unwritten and unkept, and nothing else of the case remains: a
        // rehearsal leaves the report, the counts and the names of the files as it found them.
        PictureDiscard();
        JournalCase(JOURNAL_END, suite, key, JOURNAL_MUTED);
        Breadcrumb();
        return;
    }
    if (passed_) {
        ++passed;
    } else {
        ++failed;
    }

    // Before the line and after the verdict, which is the order the whole idea rests on: the
    // bytes written are the ones the judge looked at, and nothing written here can move a
    // result that has already been decided.
    PicturePaths paths;
    const size_t pictures = PictureCount();
    PictureFinish(suite, name, TierName(tier), passed_, detail, &paths);

    char line[REPORT_LINE_MAX];
    ReportBuildTimedCaseLine(line, sizeof(line), suite, name, TierName(tier), passed_, detail,
                             &paths, timed ? &us : NULL);
    Emit(line);
    Keep(suite, name, TierName(tier), passed_ ? REPORT_PASS : REPORT_FAIL, detail,
         PictureKept(pictures));
    // After the line, so a launch that finds this case ended finds its line in the report.
    JournalCase(JOURNAL_END, suite, key, passed_ ? JOURNAL_PASSED : JOURNAL_FAILED);
    Survived(suite, key);
}

static const char *const REASON_WORDS[REPORT_REASON_COUNT] = {
    "crashed", "may crash", "not configured", "unavailable", "depends",
};

const char *ReportReasonWord(ReportReason reason) {
    return reason < REPORT_REASON_COUNT ? REASON_WORDS[reason] : "unavailable";
}

void ReportSkip(const char *suite, const char *name, ReportReason reason, const char *detail) {
    // A skip closes a case like any other, so a timed suite's positions stay in step; it asked
    // nothing, so its line never carries a time, and a rehearsal counts it no more than a case.
    (void)PerfCaseEnd(name, NULL);
    char key[JOURNAL_NAME_MAX];
    KeyOf(key, sizeof(key), name);
    Ended(suite, key, false);
    const bool as_crash = reason == REPORT_REASON_CRASHED;
    if (rehearsing) {
        PictureDiscard();
        JournalCase(JOURNAL_END, suite, key, JOURNAL_MUTED);
        Breadcrumb();
        return;
    }
    if (as_crash) {
        ++crashed;
    } else {
        ++skipped;
    }
    // Closes whatever picture a case may have opened, so it cannot land on the next one; a skip
    // compared nothing and is never a failure, so nothing is written for it.
    PicturePaths paths;
    PictureFinish(suite, name, TierName(CHECK_NONE), true, detail, &paths);

    char line[REPORT_LINE_MAX];
    ReportBuildResultLine(line, sizeof(line), suite, name, TierName(CHECK_NONE), "skip",
                          ReportReasonWord(reason), detail, NULL);
    Emit(line);
    // No picture on the screens either, as on the line: nothing in one was judged.
    Keep(suite, name, TierName(CHECK_NONE), as_crash ? REPORT_CRASHED : REPORT_SKIP, detail, -1);
    JournalCase(JOURNAL_END, suite, key, as_crash ? JOURNAL_CRASHED : JOURNAL_SKIPPED);
}

bool ReportJournalOpen(const char *path, bool fresh) {
    ReportJournalClose();
    journal_broken = false;
    FILE *file = fresh ? fopen(path, "w") : OpenAtEnd(path);
    if (file == NULL) {
        return false;
    }
    journal_file = file;
    return true;
}

void ReportJournalClose(void) {
    if (journal_file != NULL) {
        fclose(journal_file);
        journal_file = NULL;
    }
}

bool ReportJournalOn(void) {
    return journal_file != NULL;
}

void ReportJournalWrite(const JournalRecord *record) {
    if (journal_file == NULL || record == NULL) {
        return;
    }
    char line[JOURNAL_LINE_MAX];
    if (JournalFormat(line, sizeof(line), record) == 0u) {
        return;
    }
    if (!JournalDurableAppend(journal_file, line)) {
        // A card that stops taking writes: the run goes on unjournaled, and says so once.
        ReportJournalClose();
        if (!journal_broken) {
            journal_broken = true;
            ReportNote("journal: a write to the card failed, so the rest of this run is not "
                       "journaled and a crash in it costs the rest of the run");
        }
    }
}

uint64_t ReportOffset(void) {
    if (report_file == NULL || fflush(report_file) != 0) {
        return 0;
    }
    const long at = ftell(report_file);
    return at > 0 ? (uint64_t)at : 0u;
}

JournalCounts ReportCounts(void) {
    JournalCounts counts;
    counts.counts[JOURNAL_PASSED] = (uint32_t)passed;
    counts.counts[JOURNAL_FAILED] = (uint32_t)failed;
    counts.counts[JOURNAL_SKIPPED] = (uint32_t)skipped;
    counts.counts[JOURNAL_CRASHED] = (uint32_t)crashed;
    return counts;
}

void ReportSetCrashList(const CrashList *list, const char *path, bool retry) {
    if (list != NULL) {
        crash_list = *list;
    } else {
        CrashListInit(&crash_list);
    }
    snprintf(crash_list_path, sizeof(crash_list_path), "%s", path != NULL ? path : "");
    retry_listed = retry;
}

void ReportAddCrashed(const JournalRecord *crash) {
    if (crash == NULL || crashed_this_run_count >= CRASHED_THIS_RUN_MAX) {
        return;
    }
    uint32_t pass = 0;
    if (CrashedThisRun(crash->suite, crash->name, &pass)) {
        return;
    }
    crashed_this_run[crashed_this_run_count++] = *crash;
}

bool ReportCrashListAdd(const char *suite, const char *name) {
    char key[JOURNAL_NAME_MAX];
    KeyOf(key, sizeof(key), name);
    if (!CrashListAdd(&crash_list, suite, key)) {
        return false;
    }
    WriteCrashList();
    return true;
}

const CrashList *ReportCrashList(void) {
    return &crash_list;
}

const char *ReportCrashListPath(void) {
    return crash_list_path;
}

bool ReportRetrying(void) {
    return retry_listed;
}

bool ReportStartsRequired(void) {
    return starts_required;
}

void ReportNote(const char *fmt, ...) {
    if (rehearsing) {
        return;
    }
    // One byte more than the longest note the header promises to carry whole, for the
    // terminator. The two buffers below are sized off this one: the escaping can grow a note,
    // and the line adds its own two dozen characters of shape around it.
    char text[REPORT_NOTE_MAX + 1];
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);

    // Twice the longest note, because escaping a character can take two, so even a note made
    // entirely of quotation marks arrives whole. Sized off the same constant as the buffer above
    // rather than guessed, so raising that one carries these two with it.
    char safe[2 * REPORT_NOTE_MAX + 1];
    EscapeInto(safe, sizeof(safe), text);

    char line[2 * REPORT_NOTE_MAX + 64];
    snprintf(line, sizeof(line), "{\"type\":\"note\",\"text\":\"%s\"}", safe);
    Emit(line);
}

size_t ReportBuildSummaryLine(char *out, size_t out_size, size_t passed_, size_t failed_,
                              size_t skipped_, size_t crashed_) {
    if (out == NULL || out_size == 0u) {
        return 0;
    }
    const int wrote = snprintf(out, out_size,
                               "{\"type\":\"summary\",\"passed\":%zu,\"failed\":%zu,"
                               "\"skipped\":%zu,\"crashed\":%zu}",
                               passed_, failed_, skipped_, crashed_);
    if (wrote < 0 || (size_t)wrote >= out_size) {
        snprintf(out, out_size, "{}");
    }
    return strlen(out);
}

void ReportSummary(void) {
    char line[160];
    ReportBuildSummaryLine(line, sizeof(line), passed, failed, skipped, crashed);
    Emit(line);
}

size_t ReportBuildPerfLine(char *out, size_t out_size, const char *suite,
                           const PerfSummary *summary, unsigned samples) {
    if (out == NULL || out_size == 0u) {
        return 0;
    }
    char safe_suite[64];
    EscapeInto(safe_suite, sizeof(safe_suite), suite);
    const int wrote = snprintf(
        out, out_size,
        "{\"type\":\"perf\",\"suite\":\"%s\",\"timed\":%u,\"total_us\":%llu,\"min_us\":%u,"
        "\"max_us\":%u,\"median_us\":%u,\"submits\":%u,\"samples\":%u}",
        safe_suite, (unsigned)summary->timed, (unsigned long long)summary->total,
        (unsigned)summary->min, (unsigned)summary->max, (unsigned)summary->median,
        (unsigned)summary->submits, samples);
    if (wrote < 0 || (size_t)wrote >= out_size) {
        // An empty object parses; the front of a real one does not.
        out[0] = '\0';
        if (out_size >= 3u) {
            snprintf(out, out_size, "{}");
        }
    }
    return strlen(out);
}

void ReportPerf(const char *suite) {
    PerfSummary summary;
    PerfSuiteSummary(&summary);
    char line[256];
    ReportBuildPerfLine(line, sizeof(line), suite, &summary, PERF_SAMPLES);
    Emit(line);
}

size_t ReportPassed(void) {
    return passed;
}

size_t ReportFailed(void) {
    return failed;
}

size_t ReportSkipped(void) {
    return skipped;
}

size_t ReportCrashed(void) {
    return crashed;
}

_Static_assert(2 * REPORT_NOTE_MAX + 64 <= REPORT_LINE_MAX,
               "a note's line fits the buffer a durable line is written from");
