// SPDX-FileCopyrightText: Copyright 2026 gfxprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdarg.h>
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
static size_t passed;
static size_t failed;

/// Sends one finished line to both channels.
///
/// The debug string service is the only one an emulator sees for free, and the only one that
/// costs nothing on hardware when nobody is listening. The file is what a console run leaves
/// behind for the memory card to carry back.
static void Emit(const char *line) {
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
    report_file = fopen(path, "w");
    if (report_file == NULL) {
        // Not fatal. On hardware with no memory card, or an emulator pointed somewhere odd, the
        // debug channel still carries the whole run.
        static const char warning[] =
            "{\"type\":\"note\",\"text\":\"report file unavailable\"}";
        SendToDebugChannel(warning, sizeof(warning) - 1);
    }
}

void ReportClose(void) {
    if (report_file != NULL) {
        fclose(report_file);
        report_file = NULL;
    }
}

void ReportCase(const char *suite, const char *name, bool passed_, const char *detail) {
    if (passed_) {
        ++passed;
    } else {
        ++failed;
    }

    char safe_suite[64];
    char safe_name[128];
    char safe_detail[512];
    EscapeInto(safe_suite, sizeof(safe_suite), suite);
    EscapeInto(safe_name, sizeof(safe_name), name);
    EscapeInto(safe_detail, sizeof(safe_detail), detail != NULL ? detail : "");

    char line[832];
    snprintf(line, sizeof(line),
             "{\"type\":\"case\",\"suite\":\"%s\",\"name\":\"%s\",\"result\":\"%s\",\"detail\":\"%s\"}",
             safe_suite, safe_name, passed_ ? "pass" : "fail", safe_detail);
    Emit(line);
}

void ReportNote(const char *fmt, ...) {
    char text[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);

    char safe[1024];
    EscapeInto(safe, sizeof(safe), text);

    char line[1100];
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
