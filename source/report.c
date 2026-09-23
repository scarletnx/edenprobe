// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

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
        fputs(line, report_file);
        fputc('\n', report_file);
        // Flushed per line on purpose: a run that hangs or is killed still leaves everything up
        // to the last completed case, which is exactly what a diff needs.
        fflush(report_file);
    }
}

/// Copies @p text into @p out with the escapes JSON requires, and nothing else: the strings
/// here are test names and short details written by this program, not arbitrary input.
static void EscapeInto(char *out, size_t out_size, const char *text) {
    size_t w = 0;
    for (size_t r = 0; text[r] != '\0' && w + 2 < out_size; ++r) {
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
}

void ReportOpen(const char *path) {
    passed = 0;
    failed = 0;
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
    char safe_suite[64];
    char safe_name[128];
    char safe_detail[512];
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
    const int wrote =
        snprintf(out, room,
                 "{\"type\":\"case\",\"suite\":\"%s\",\"name\":\"%s\",\"tier\":\"%s\","
                 "\"result\":\"%s\",\"detail\":\"%s\"",
                 safe_suite, safe_name, tier, passed_ ? "pass" : "fail", safe_detail);
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
    const size_t used = strlen(out);
    out[used] = '}';
    out[used + 1u] = '\0';
    return used + 1u;
}

void ReportCase(const char *suite, const char *name, CheckTier tier, bool passed_,
                const char *detail) {
    if (passed_) {
        ++passed;
    } else {
        ++failed;
    }

    // Before the line and after the verdict, which is the order the whole idea rests on: the
    // bytes written are the ones the judge looked at, and nothing written here can move a
    // result that has already been decided.
    PicturePaths paths;
    PictureFinish(suite, name, TierName(tier), passed_, detail, &paths);

    char line[REPORT_LINE_MAX];
    ReportBuildCaseLine(line, sizeof(line), suite, name, TierName(tier), passed_, detail,
                        &paths);
    Emit(line);
}

void ReportNote(const char *fmt, ...) {
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

void ReportSummary(void) {
    char line[160];
    snprintf(line, sizeof(line), "{\"type\":\"summary\",\"passed\":%zu,\"failed\":%zu}", passed,
             failed);
    Emit(line);
}

size_t ReportPassed(void) {
    return passed;
}

size_t ReportFailed(void) {
    return failed;
}
