// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <string.h>

#include "../check.h"
#include "../mode.h"
#include "../perf.h"
#include "../picture.h"
#include "../report.h"
#include "tests.h"

// The time a case reports and the perf line a timed suite ends on. Neither decides anything, so
// these are support cases and the none tier: what they pin is that a number on a line is the
// number the samples give, and that the passes before a verdict leave nothing else behind.

static const char *const SUITE = "support";

static void CheckArithmetic(void) {
    uint32_t odd[] = { 9, 1, 5 };
    uint32_t even[] = { 40, 10, 30, 20 };
    uint32_t one[] = { 7 };
    uint32_t top[] = { UINT32_MAX, UINT32_MAX - 2u };
    const uint32_t m_odd = PerfMedian(odd, 3);
    const uint32_t m_even = PerfMedian(even, 4);
    const uint32_t m_one = PerfMedian(one, 1);
    const uint32_t m_top = PerfMedian(top, 2);
    const uint32_t m_none = PerfMedian(NULL, 0);
    CHECK_TRUE(SUITE, "the median is the middle value, or between the two middle ones", CHECK_NONE,
               m_odd == 5u && m_even == 25u && m_one == 7u && m_none == 0u &&
                   m_top == UINT32_MAX - 1u,
               "odd %u even %u one %u none %u top %u", (unsigned)m_odd, (unsigned)m_even,
               (unsigned)m_one, (unsigned)m_none, (unsigned)m_top);

    const uint32_t samples[] = { 300, 250, 2700, 251 };
    CHECK_TRUE(SUITE, "a case's number is the smallest of its samples", CHECK_NONE,
               PerfMinimum(samples, 4) == 250u && PerfMinimum(samples, 0) == 0u,
               "%u", (unsigned)PerfMinimum(samples, 4));

    uint32_t times[] = { 8000, 250, 300, 275, 260 };
    PerfSummary summary;
    PerfSummarize(times, 5, &summary);
    CHECK_TRUE(SUITE, "a suite's summary counts, adds and orders its cases' times", CHECK_NONE,
               summary.timed == 5u && summary.total == 9085u && summary.min == 250u &&
                   summary.max == 8000u && summary.median == 275u,
               "timed %u total %llu min %u max %u median %u", (unsigned)summary.timed,
               (unsigned long long)summary.total, (unsigned)summary.min, (unsigned)summary.max,
               (unsigned)summary.median);

    PerfSummarize(times, 0, &summary);
    CHECK_TRUE(SUITE, "a suite that timed nothing sums up to nothing", CHECK_NONE,
               summary.timed == 0u && summary.total == 0u && summary.min == 0u &&
                   summary.max == 0u && summary.median == 0u,
               "timed %u total %llu", (unsigned)summary.timed, (unsigned long long)summary.total);
}

/// One pass over a made-up suite of three cases: one that draws for @p first nanoseconds, one
/// that draws nothing, and one that draws for @p third. Hands back what the verdict pass would
/// put on the first and third lines.
static void Pass(PerfPass pass, uint64_t first, uint64_t third, bool *timed_first,
                 uint32_t *us_first, bool *timed_second, bool *timed_third, uint32_t *us_third) {
    PerfPassBegin(pass);
    PerfAddWork(first);
    *timed_first = PerfCaseEnd("draws", us_first);
    uint32_t ignored = 0;
    *timed_second = PerfCaseEnd("judges only", &ignored);
    PerfAddWork(third / 2u);
    PerfAddWork(third - third / 2u);
    *timed_third = PerfCaseEnd("draws twice", us_third);
}

static void CheckSampling(void) {
    bool t1 = false;
    bool t2 = false;
    bool t3 = false;
    uint32_t u1 = 0;
    uint32_t u3 = 0;

    PerfSuiteBegin();
    // The warm-up is fastest of all and must still not count: it is where the first use of a
    // shader is paid for on an emulator, and dropping it is the point of having it.
    Pass(PERF_WARMUP, 100000u, 100000u, &t1, &u1, &t2, &t3, &u3);
    const bool warmup_silent = !t1 && !t2 && !t3;
    const uint64_t firsts[] = { 400000u, 300400u, 350000u };
    bool rehearsals_silent = true;
    for (size_t i = 0; i < sizeof(firsts) / sizeof(firsts[0]); ++i) {
        Pass(PERF_REHEARSAL, firsts[i], 2000000u + i * 1000u, &t1, &u1, &t2, &t3, &u3);
        rehearsals_silent = rehearsals_silent && !t1 && !t2 && !t3;
    }
    Pass(PERF_VERDICT, 500000u, 1999499u, &t1, &u1, &t2, &t3, &u3);
    CHECK_TRUE(SUITE, "only the verdict pass hands a time to a line", CHECK_NONE,
               warmup_silent && rehearsals_silent && t1 && t3, "warm-up %d rehearsals %d",
               warmup_silent, rehearsals_silent);
    CHECK_TRUE(SUITE, "the time is the smallest sample, rounded to the microsecond", CHECK_NONE,
               u1 == 300u && u3 == 1999u, "first %u third %u", (unsigned)u1, (unsigned)u3);
    CHECK_TRUE(SUITE, "a case that put nothing on the graphics unit reports no time", CHECK_NONE,
               !t2, "it was handed a time");

    PerfSummary summary;
    PerfSuiteSummary(&summary);
    CHECK_TRUE(SUITE, "the perf line sums up the times the verdict pass reported", CHECK_NONE,
               summary.timed == 2u && summary.total == 2299u && summary.min == 300u &&
                   summary.max == 1999u && summary.median == 1149u && summary.submits == 3u,
               "timed %u total %llu median %u submits %u", (unsigned)summary.timed,
               (unsigned long long)summary.total, (unsigned)summary.median,
               (unsigned)summary.submits);

    // A pass that took another path puts a different case at the same position. Its samples
    // start again rather than inheriting the time of a case that is not there any more. And work
    // left over after a pass's last case belongs to no case: with only the verdict pass's sample
    // to go on, five seconds of it landing here would show.
    PerfSuiteBegin();
    PerfPassBegin(PERF_REHEARSAL);
    PerfAddWork(10000u);
    PerfCaseEnd("the case that was there", NULL);
    PerfAddWork(5000000000u);
    PerfPassBegin(PERF_VERDICT);
    PerfAddWork(900000u);
    uint32_t other = 0;
    const bool other_timed = PerfCaseEnd("the case that is there now", &other);
    CHECK_TRUE(SUITE, "a case inherits neither another case's samples nor a pass's leftovers",
               CHECK_NONE,
               other_timed && other == 900u, "handed %u", (unsigned)other);

    // Work too short to round to a microsecond still happened, and says so.
    PerfSuiteBegin();
    PerfPassBegin(PERF_VERDICT);
    PerfAddWork(1u);
    uint32_t tiny = 0;
    const bool tiny_timed = PerfCaseEnd("a clear too quick to see", &tiny);
    // And to the nearest, not down: a microsecond and a half is two, a hair under is one.
    PerfAddWork(1500u);
    uint32_t half_up = 0;
    PerfCaseEnd("a microsecond and a half", &half_up);
    PerfAddWork(1499u);
    uint32_t half_down = 0;
    PerfCaseEnd("a hair under", &half_down);
    // Outside a timed suite nothing is timed at all, which is every suite before the drawing
    // ones.
    PerfPassBegin(PERF_UNTIMED);
    PerfAddWork(900000u);
    uint32_t untimed = 0;
    const bool untimed_timed = PerfCaseEnd("a selfcheck case", &untimed);
    CHECK_TRUE(SUITE, "times round to the nearest microsecond, never below one", CHECK_NONE,
               tiny_timed && tiny == 1u && half_up == 2u && half_down == 1u,
               "tiny %u, 1500 ns %u, 1499 ns %u", (unsigned)tiny, (unsigned)half_up,
               (unsigned)half_down);
    CHECK_TRUE(SUITE, "untimed suites report no time", CHECK_NONE, !untimed_timed,
               "a selfcheck case was handed %u", (unsigned)untimed);

    PerfSuiteBegin();
}

static void CheckLines(void) {
    char plain[REPORT_LINE_MAX];
    char timed[REPORT_LINE_MAX];
    const uint32_t us = 257u;
    ReportBuildCaseLine(plain, sizeof(plain), "clear", "clear to opaque red", "exact", true, "",
                        NULL);
    ReportBuildTimedCaseLine(timed, sizeof(timed), "clear", "clear to opaque red", "exact", true,
                             "", NULL, &us);
    const size_t plain_length = strlen(plain);
    CHECK_TRUE(SUITE, "a timed line is the untimed line with its time last", CHECK_NONE,
               strcmp(timed, "{\"type\":\"case\",\"suite\":\"clear\",\"name\":\"clear to opaque "
                             "red\",\"tier\":\"exact\",\"result\":\"pass\",\"detail\":\"\","
                             "\"us\":257}") == 0 &&
                   strncmp(plain, timed, plain_length - 1u) == 0,
               "%.480s", timed);

    PicturePaths paths;
    memset(&paths, 0, sizeof(paths));
    snprintf(paths.image, sizeof(paths.image), "images/clear/a.pam");
    ReportBuildTimedCaseLine(timed, sizeof(timed), "clear", "a", "exact", false, "wrong", &paths,
                             &us);
    CHECK_TRUE(SUITE, "the time comes after the pictures a case names", CHECK_NONE,
               strcmp(timed, "{\"type\":\"case\",\"suite\":\"clear\",\"name\":\"a\",\"tier\":"
                             "\"exact\",\"result\":\"fail\",\"detail\":\"wrong\",\"image\":"
                             "\"images/clear/a.pam\",\"us\":257}") == 0,
               "%.480s", timed);

    // Whole or not at all at every size, the way the paths already are.
    const uint32_t big = UINT32_MAX;
    uint32_t unreadable = 0;
    uint32_t first_bad = 0;
    for (size_t size = 2; size <= 200u; ++size) {
        char small[201];
        memset(small, 'x', sizeof(small));
        ReportBuildTimedCaseLine(small, size, "clear", "a", "exact", true, "", NULL, &big);
        const size_t length = strlen(small);
        const bool shaped = length == 0u || (small[0] == '{' && small[length - 1u] == '}');
        const char *const field = strstr(small, "\"us\":");
        const bool whole = field == NULL || strcmp(field, "\"us\":4294967295}") == 0;
        if (!shaped || !whole || length >= size) {
            if (unreadable == 0u) {
                first_bad = (uint32_t)size;
            }
            ++unreadable;
        }
    }
    CHECK_TRUE(SUITE, "a time with no room on its line is left off, never cut", CHECK_NONE,
               unreadable == 0u, "%u sizes unreadable, the first at %u", (unsigned)unreadable,
               (unsigned)first_bad);

    PerfSummary summary = { 3u, 9000u, 250u, 8000u, 750u, 12u };
    char line[256];
    ReportBuildPerfLine(line, sizeof(line), "clear", &summary, PERF_SAMPLES);
    char want[256];
    snprintf(want, sizeof(want),
             "{\"type\":\"perf\",\"suite\":\"clear\",\"timed\":3,\"total_us\":9000,"
             "\"min_us\":250,\"max_us\":8000,\"median_us\":750,\"submits\":12,"
             "\"samples\":%u}",
             (unsigned)PERF_SAMPLES);
    char cramped[16];
    ReportBuildPerfLine(cramped, sizeof(cramped), "clear", &summary, PERF_SAMPLES);
    CHECK_TRUE(SUITE, "a perf line says what the summary says, or nothing at all", CHECK_NONE,
               strcmp(line, want) == 0 && strcmp(cramped, "{}") == 0, "%.200s / %s", line,
               cramped);
}

static void CheckRehearsal(void) {
    const size_t passed = ReportPassed();
    const size_t failed = ReportFailed();
    const size_t skipped = ReportSkipped();
    size_t records = 0;
    (void)ReportRecords(&records);
    static const uint8_t pixels[4 * 4 * 4];
    ReportRehearse(SUITE);
    (void)PictureBegin(4, 4, PICTURE_COLOUR, pixels);
    ReportCase(SUITE, "a rehearsed case that failed", CHECK_EXACT, false, "rehearsal");
    ReportCase(SUITE, "a rehearsed case that passed", CHECK_EXACT, true, NULL);
    ReportSkip(SUITE, "a rehearsed case that could not ask", "rehearsal");
    ReportNote("a rehearsed note");
    ReportRehearse(NULL);
    size_t records_after = 0;
    (void)ReportRecords(&records_after);
    // Nor anything for the detailed mode's screens to show.
    const bool counts_kept = ReportPassed() == passed && ReportFailed() == failed &&
                             ReportSkipped() == skipped && records_after == records;

    // The picture the rehearsed case opened is closed, so nothing is left for the next line to
    // write under its own name.
    PicturePaths paths;
    PictureFinish(SUITE, "a rehearsed case that failed", "exact", false, "", &paths);
    CHECK_TRUE(SUITE, "a rehearsal leaves no count and no picture behind", CHECK_NONE,
               counts_kept && paths.image[0] == '\0',
               "passed %zu to %zu, failed %zu to %zu, kept %zu to %zu, picture \"%s\"", passed,
               ReportPassed(), failed, ReportFailed(), records, records_after, paths.image);
}

static void CheckBreadcrumbs(void) {
    // The warm-up knows no names yet; every later pass has the one the pass before it saw.
    PerfSuiteBegin();
    PerfPassBegin(PERF_WARMUP);
    const uint32_t first_pass = PerfPassNumber();
    const bool unknown_at_first = PerfNameAt(0) == NULL;
    PerfCaseEnd("draws", NULL);
    PerfCaseEnd("judges only", NULL);
    PerfPassBegin(PERF_REHEARSAL);
    const uint32_t second_pass = PerfPassNumber();
    const char *const known = PerfNameAt(1);
    CHECK_TRUE(SUITE, "a rehearsal knows each case's name from the pass before it", CHECK_NONE,
               first_pass == 1u && second_pass == 2u && unknown_at_first && known != NULL &&
                   strcmp(known, "judges only") == 0 && PerfCaseIndex() == 0u,
               "passes %u %u, name \"%s\"", (unsigned)first_pass, (unsigned)second_pass,
               known != NULL ? known : "(none)");
    PerfSuiteBegin();

    char named[256];
    char numbered[256];
    char first[256];
    ReportBuildBreadcrumb(named, sizeof(named), "depth", 4, 2, "a fragment nearer passes",
                          "a depth clear");
    ReportBuildBreadcrumb(numbered, sizeof(numbered), "depth", 1, 2, NULL, "a depth clear");
    ReportBuildBreadcrumb(first, sizeof(first), "depth", 1, 0, NULL, NULL);
    CHECK_TRUE(SUITE, "the log line before a rehearsed case names the suite, pass and case",
               CHECK_NONE,
               strcmp(named, "EDENPROBE TIMING depth pass 4 case a fragment nearer passes") == 0 &&
                   strcmp(numbered,
                          "EDENPROBE TIMING depth pass 1 case number 3 after a depth clear") ==
                       0 &&
                   strcmp(first, "EDENPROBE TIMING depth pass 1 case number 1") == 0,
               "%.160s / %.160s / %.160s", named, numbered, first);

    const RunMode timed = ModeFromText("quick");
    const RunMode untimed = ModeFromText("quick notime");
    char described[256];
    ModeDescribe(described, sizeof(described), &untimed, MODE_FAMILY_GRAPHICS);
    char plain[256];
    ModeDescribe(plain, sizeof(plain), &timed, MODE_FAMILY_GRAPHICS);
    CHECK_TRUE(SUITE, "the word notime turns the timing passes off and says so", CHECK_NONE,
               untimed.no_timing && !timed.no_timing && untimed.unknown_count == 0u &&
                   strcmp(described, "mode: quick, from the mode file; pictures of failures "
                                     "only; families graphics; no timing passes") == 0 &&
                   strcmp(plain, "mode: quick, from the mode file; pictures of failures only; "
                                 "families graphics") == 0,
               "%.240s / %.240s", described, plain);
}

void RunSupportPerf(void) {
    CheckArithmetic();
    CheckSampling();
    CheckLines();
    CheckRehearsal();
    CheckBreadcrumbs();
}
