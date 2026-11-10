// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Before any header: gmtime_r, sockets and the clock are POSIX, which glibc and bionic hide from
// a strict C build without this. newlib on the consoles shows them anyway.
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "../check.h"
#include "../libcmath.h"
#include "../netref.h"
#include "../platform.h"
#include "../report.h"
#include "tests.h"

// The C library, the same cases on every frontend (docs/families/Libc.md): what the C standard or
// POSIX defines, never what one library chose where the standard lets it (strerror's words, the
// sign a NaN prints with, %a's leading digit), so a console's newlib, Android's bionic and a PC's
// glibc all pass, and an emulator or a layer that answers otherwise is wrong. Three suites, by
// what each needs from the machine under the library: libc nothing but the library, system
// threads and a clock, socket the network stack on loopback. A frontend runs those it has.

// ---- libc: the library alone ----

static const char *const LIBC = "libc";

static void Integers(void) {
    static const char *const NAME = "snprintf formats integers with widths, precisions and flags";
    if (!ReportStart(LIBC, NAME)) {
        return;
    }
    char got[160];
    snprintf(got, sizeof(got), "%d|%5d|%-4d|%+05d|%x|%X|%o|%lld|%zu|%.3s|%c|%%", -42, 42, 7, 42,
             255u, 255u, 8u, -9000000000LL, (size_t)7, "abcdef", 'c');
    static const char *const WANT = "-42|   42|7   |+0042|ff|FF|10|-9000000000|7|abc|c|%";
    CHECK_TRUE(LIBC, NAME, CHECK_EXACT, strcmp(got, WANT) == 0, "got \"%s\", expected \"%s\"", got,
               WANT);
}

static void Floats(void) {
    static const char *const NAME =
        "snprintf rounds floats to their precision, and words infinities and NaNs as C says";
    if (!ReportStart(LIBC, NAME)) {
        return;
    }
    // 2.5 and 0.125 are exact in binary, so their rounding is the half-to-even the library's
    // conversion of the exact value gives; NAN is a quiet NaN with its sign clear.
    char got[160];
    snprintf(got, sizeof(got), "%5.2f|%e|%g|%.0f|%.2f|%.3e|%f|%f|%F|%f", 3.14159, 12345.678, 0.0001,
             2.5, 0.125, 1.0, (double)INFINITY, -(double)INFINITY, (double)INFINITY, (double)NAN);
    static const char *const WANT =
        " 3.14|1.234568e+04|0.0001|2|0.12|1.000e+00|inf|-inf|INF|nan";
    CHECK_TRUE(LIBC, NAME, CHECK_EXACT, strcmp(got, WANT) == 0, "got \"%s\", expected \"%s\"", got,
               WANT);
}

static void Scan(void) {
    static const char *const NAME = "sscanf reads an int, a double, a word, hex and the count";
    if (!ReportStart(LIBC, NAME)) {
        return;
    }
    int i = 0, x = 0, n = -1;
    double d = 0;
    char w[16] = "";
    const int r = sscanf("42 3.5 word 0x1f rest", "%d %lf %15s %x %n", &i, &d, w, &x, &n);
    char got[96];
    snprintf(got, sizeof(got), "%d|%d|%.3f|%s|%d|%d", r, i, d, w, x, n);
    static const char *const WANT = "4|42|3.500|word|31|17";
    CHECK_TRUE(LIBC, NAME, CHECK_EXACT, strcmp(got, WANT) == 0, "got \"%s\", expected \"%s\"", got,
               WANT);
}

static void Parse(void) {
    static const char *const NAME =
        "strtod and strtol read hex floats, overflow with ERANGE and take base 0's prefixes";
    if (!ReportStart(LIBC, NAME)) {
        return;
    }
    char *end = NULL;
    errno = 0;
    const double hex = strtod("0x1.8p3", &end);
    const bool hex_whole = end != NULL && *end == '\0';
    errno = 0;
    const double huge = strtod("1e400", NULL);
    const int huge_errno = errno;
    errno = 0;
    const long big = strtol("99999999999999999999", NULL, 10);
    const int big_errno = errno;
    const unsigned long minus_one = strtoul("-1", NULL, 10);
    const long hex_int = strtol("0x1F", NULL, 0);
    const long octal = strtol("017", NULL, 0);
    const double decimal = strtod("3.5", NULL);
    const bool right = hex == 12.0 && hex_whole && huge == HUGE_VAL && huge_errno == ERANGE &&
                       big == LONG_MAX && big_errno == ERANGE && minus_one == ULONG_MAX &&
                       hex_int == 31 && octal == 15 && decimal == 3.5;
    CHECK_TRUE(LIBC, NAME, CHECK_EXACT, right,
               "0x1.8p3 gave %g%s; 1e400 gave %g with errno %d (ERANGE is %d); a long too large "
               "gave %ld with errno %d; -1 unsigned gave %lu; 0x1F and 017 in base 0 gave %ld and "
               "%ld; 3.5 gave %g",
               hex, hex_whole ? "" : " not read to its end", huge, huge_errno, ERANGE, big, big_errno,
               minus_one, hex_int, octal, decimal);
}

static void Strings(void) {
    static const char *const NAME =
        "the string functions compare, copy, search, span and move overlapping bytes";
    if (!ReportStart(LIBC, NAME)) {
        return;
    }
    char padded[8];
    memset(padded, 'x', sizeof(padded));
    strncpy(padded, "ab", 5);
    char moved[] = "abcdefgh";
    memmove(moved + 2, moved, 5);
    const char *const text = "the quick brown fox";
    const char *const found = strstr(text, "brown");
    const char *const first_o = strchr(text, 'o');
    const char *const last_o = strrchr(text, 'o');
    const bool right = strcmp("abc", "abd") < 0 && strcmp("b", "a") > 0 && strcmp("x", "x") == 0 &&
                       strncmp("abcdef", "abcxyz", 3) == 0 && memcmp(padded, "ab\0\0\0xxx", 8) == 0 &&
                       strcmp(moved, "ababcdeh") == 0 && found == text + 10 &&
                       first_o == text + 12 && last_o == text + 17 &&
                       strspn("aabbcd", "ab") == 4 && strcspn("hello, world", ", ") == 5 &&
                       strlen(text) == 19;
    CHECK_TRUE(LIBC, NAME, CHECK_EXACT, right,
               "memmove gave \"%s\", expected \"ababcdeh\"; strstr, strchr and strrchr at %ld, %ld "
               "and %ld, expected 10, 12 and 17; strspn %zu, strcspn %zu, expected 4 and 5",
               moved, found != NULL ? (long)(found - text) : -1L,
               first_o != NULL ? (long)(first_o - text) : -1L,
               last_o != NULL ? (long)(last_o - text) : -1L, strspn("aabbcd", "ab"),
               strcspn("hello, world", ", "));
}

static int CompareInts(const void *a, const void *b) {
    const int x = *(const int *)a;
    const int y = *(const int *)b;
    return (x > y) - (x < y);
}

static void Sort(void) {
    static const char *const NAME = "qsort orders sixteen integers and bsearch finds each";
    if (!ReportStart(LIBC, NAME)) {
        return;
    }
    int values[16] = { 13, -4, 7, 0, 99, 13, -100, 42, 5, 8, -1, 64, 3, 27, 1, 2 };
    qsort(values, 16, sizeof(values[0]), CompareInts);
    static const int SORTED[16] = { -100, -4, -1, 0, 1, 2, 3, 5, 7, 8, 13, 13, 27, 42, 64, 99 };
    int found = 0;
    for (int i = 0; i < 16; ++i) {
        found += bsearch(&SORTED[i], values, 16, sizeof(values[0]), CompareInts) != NULL;
    }
    const int missing = 6;
    const bool right = memcmp(values, SORTED, sizeof(SORTED)) == 0 && found == 16 &&
                       bsearch(&missing, values, 16, sizeof(values[0]), CompareInts) == NULL;
    CHECK_TRUE(LIBC, NAME, CHECK_EXACT, right,
               "sorted %d %d %d %d ... %d %d, bsearch found %d of 16", values[0], values[1],
               values[2], values[3], values[14], values[15], found);
}

static jmp_buf jump;

static void JumpBack(int value) {
    longjmp(jump, value);
}

static void Jumps(void) {
    static const char *const NAME = "setjmp returns twice, with longjmp's value, and 1 for 0";
    if (!ReportStart(LIBC, NAME)) {
        return;
    }
    // Static: a local changed between setjmp and longjmp has no value C promises after it.
    static volatile int first, second, third, stage;
    first = second = third = -1;
    stage = 0;
    const int a = setjmp(jump);
    if (stage == 0) {
        first = a;
        stage = 1;
        JumpBack(7);
    }
    if (stage == 1) {
        second = a;
        stage = 2;
        JumpBack(0);
    }
    third = a;
    CHECK_TRUE(LIBC, NAME, CHECK_EXACT, first == 0 && second == 7 && third == 1,
               "setjmp returned %d, then %d after longjmp 7, then %d after longjmp 0; expected 0, "
               "7 and 1",
               first, second, third);
}

static void Dates(void) {
    static const char *const NAME = "gmtime_r and strftime write a fixed time in the C locale";
    if (!ReportStart(LIBC, NAME)) {
        return;
    }
    // A program starts in the C locale whatever its environment says, so the names are English.
    const time_t when = 1700000000;
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    const bool converted = gmtime_r(&when, &tm) != NULL;
    char got[64] = "";
    strftime(got, sizeof(got), "%Y-%m-%dT%H:%M:%S %a %b %j %u", &tm);
    static const char *const WANT = "2023-11-14T22:13:20 Tue Nov 318 2";
    CHECK_TRUE(LIBC, NAME, CHECK_EXACT, converted && strcmp(got, WANT) == 0,
               "gmtime_r %s, strftime gave \"%s\", expected \"%s\"",
               converted ? "converted" : "returned NULL", got, WANT);
}

/// How far apart two doubles are in units in the last place, by their ordered bit patterns.
static uint64_t UlpDistance64(double produced, double expected) {
    int64_t a, b;
    memcpy(&a, &produced, sizeof(a));
    memcpy(&b, &expected, sizeof(b));
    if (a < 0) {
        a = INT64_MIN - a;
    }
    if (b < 0) {
        b = INT64_MIN - b;
    }
    return a > b ? (uint64_t)a - (uint64_t)b : (uint64_t)b - (uint64_t)a;
}

static double VD(double x) {
    volatile double v = x;
    return v;
}

static float VF(float x) {
    volatile float v = x;
    return v;
}

static void Libm(void) {
    static const char *const NAME = "libm is within 1 ulp of the correctly rounded results";
    if (!ReportStart(LIBC, NAME)) {
        return;
    }
    char off[384] = "";
    size_t used = 0;
    int wrong = 0;
#define CHECK_DOUBLE(fn, call, bits)                                                              \
    {                                                                                             \
        const uint64_t want_ = (bits);                                                            \
        double expected_;                                                                         \
        memcpy(&expected_, &want_, sizeof(expected_));                                            \
        const uint64_t ulps_ = UlpDistance64((call), expected_);                                  \
        if (ulps_ > 1u) {                                                                         \
            ++wrong;                                                                              \
            used += (size_t)snprintf(off + used, sizeof(off) - used, "%s%s %llu ulp", used ? ", " \
                                     : "", #fn, (unsigned long long)ulps_);                       \
        }                                                                                         \
    }
#define CHECK_FLOAT(fn, call, bits)                                                               \
    {                                                                                             \
        const uint32_t want_ = (bits);                                                            \
        float expected_;                                                                          \
        memcpy(&expected_, &want_, sizeof(expected_));                                            \
        const uint32_t ulps_ = CheckFloatUlpDistance((call), expected_);                          \
        if (ulps_ > 1u) {                                                                         \
            ++wrong;                                                                              \
            used += (size_t)snprintf(off + used, sizeof(off) - used, "%s%s %lu ulp", used ? ", " \
                                     : "", #fn, (unsigned long)ulps_);                            \
        }                                                                                         \
    }
    MATH_DOUBLES(CHECK_DOUBLE)
    MATH_FLOATS(CHECK_FLOAT)
#undef CHECK_DOUBLE
#undef CHECK_FLOAT
    CHECK_TRUE(LIBC, NAME, CHECK_ULP, wrong == 0, "%d functions more than 1 ulp off: %s", wrong,
               off);
}

void RunLibc(void) {
    Integers();
    Floats();
    Scan();
    Parse();
    Strings();
    Sort();
    Jumps();
    Dates();
    Libm();
}

// ---- system: threads, their keys and locals, and the clock ----

static const char *const SYSTEM = "system";

#define THREADS 4
#define ADDS 10000

static struct {
    pthread_mutex_t mutex;
    long sum;
    pthread_key_t key;
    pthread_mutex_t counted;
    int destructors;
    int own_locals;
} shared = { .mutex = PTHREAD_MUTEX_INITIALIZER, .counted = PTHREAD_MUTEX_INITIALIZER };

static _Thread_local int thread_index = -1;

static void CountDestructor(void *value) {
    (void)value;
    pthread_mutex_lock(&shared.counted);
    ++shared.destructors;
    pthread_mutex_unlock(&shared.counted);
}

static void *Adder(void *argument) {
    const int index = (int)(intptr_t)argument;
    thread_index = index;
    pthread_setspecific(shared.key, (void *)(intptr_t)(index + 1));
    for (int k = 0; k < ADDS; ++k) {
        pthread_mutex_lock(&shared.mutex);
        ++shared.sum;
        pthread_mutex_unlock(&shared.mutex);
    }
    if (thread_index == index) {
        pthread_mutex_lock(&shared.counted);
        ++shared.own_locals;
        pthread_mutex_unlock(&shared.counted);
    }
    return NULL;
}

static void Threads(void) {
    static const char *const NAME =
        "four threads add under one mutex, a key's destructor runs for each, each keeps its own "
        "thread-local";
    if (!ReportStart(SYSTEM, NAME)) {
        return;
    }
    shared.sum = 0;
    shared.destructors = 0;
    shared.own_locals = 0;
    char failed[96] = "";
    int made = 0;
    int r = pthread_key_create(&shared.key, CountDestructor);
    if (r != 0) {
        snprintf(failed, sizeof(failed), "pthread_key_create returned %d; ", r);
    } else {
        pthread_t threads[THREADS];
        for (; made < THREADS; ++made) {
            r = pthread_create(&threads[made], NULL, Adder, (void *)(intptr_t)made);
            if (r != 0) {
                snprintf(failed, sizeof(failed), "pthread_create of thread %d returned %d; ", made,
                         r);
                break;
            }
        }
        for (int i = 0; i < made; ++i) {
            pthread_join(threads[i], NULL);
        }
        pthread_key_delete(shared.key);
    }
    CHECK_TRUE(SYSTEM, NAME, CHECK_EXACT,
               failed[0] == '\0' && shared.sum == THREADS * ADDS &&
                   shared.destructors == THREADS && shared.own_locals == THREADS &&
                   thread_index == -1,
               "%sthe sum is %ld, expected %d; the destructor ran %d times and %d threads kept "
               "their own thread-local, expected %d each; this thread's is %d, expected -1",
               failed, shared.sum, THREADS * ADDS, shared.destructors, shared.own_locals, THREADS,
               thread_index);
}

static void Clock(void) {
    static const char *const NAME = "the monotonic clock moves forward over a 20 ms sleep";
    if (!ReportStart(SYSTEM, NAME)) {
        return;
    }
    struct timespec before, after;
    const struct timespec nap = { 0, 20000000 };
    const int read_before = clock_gettime(CLOCK_MONOTONIC, &before);
    const int slept = nanosleep(&nap, NULL);
    const int read_after = clock_gettime(CLOCK_MONOTONIC, &after);
    const long long ns = (long long)(after.tv_sec - before.tv_sec) * 1000000000LL +
                         (after.tv_nsec - before.tv_nsec);
    CHECK_TRUE(SYSTEM, NAME, CHECK_EXACT,
               read_before == 0 && read_after == 0 && slept == 0 && ns >= 20000000LL,
               "clock_gettime returned %d and %d, nanosleep %d; the clock moved %lld ns, expected "
               "20000000 or more",
               read_before, read_after, slept, ns);
}

void RunSystem(void) {
    Threads();
    Clock();
}

// ---- socket: the network stack on loopback ----

static const char *const SOCKET = "socket";

/// A socket bound to 127.0.0.1 on a port the stack picks, and that port; -1 when it fails, with
/// the call that did in @p why.
static int BindLoopback(int type, struct sockaddr_in *address, char *why, size_t why_size) {
    const int s = socket(AF_INET, type, 0);
    if (s < 0) {
        snprintf(why, why_size, "socket failed with errno %d", errno);
        return -1;
    }
    memset(address, 0, sizeof(*address));
    address->sin_family = AF_INET;
    // 127.0.0.1 by its number: the console's newlib names no INADDR_LOOPBACK.
    address->sin_addr.s_addr = htonl(0x7f000001u);
    address->sin_port = 0;
    socklen_t length = sizeof(*address);
    if (bind(s, (struct sockaddr *)address, sizeof(*address)) != 0 ||
        getsockname(s, (struct sockaddr *)address, &length) != 0) {
        snprintf(why, why_size, "bind or getsockname on 127.0.0.1 failed with errno %d", errno);
        close(s);
        return -1;
    }
    return s;
}

static void TcpEcho(void) {
    static const char *const NAME = "a TCP socket listens on loopback, connects and echoes";
    if (!ReportStart(SOCKET, NAME)) {
        return;
    }
    char why[128] = "";
    struct sockaddr_in address;
    const int server = BindLoopback(SOCK_STREAM, &address, why, sizeof(why));
    int client = -1, accepted = -1;
    char echoed[8] = "";
    if (server >= 0 && listen(server, 1) != 0) {
        snprintf(why, sizeof(why), "listen failed with errno %d", errno);
    } else if (server >= 0) {
        // One thread: the stack completes the handshake into the backlog before accept takes it.
        client = socket(AF_INET, SOCK_STREAM, 0);
        if (client < 0 || connect(client, (struct sockaddr *)&address, sizeof(address)) != 0) {
            snprintf(why, sizeof(why), "connect to 127.0.0.1:%u failed with errno %d",
                     (unsigned)ntohs(address.sin_port), errno);
        } else if ((accepted = accept(server, NULL, NULL)) < 0) {
            snprintf(why, sizeof(why), "accept failed with errno %d", errno);
        } else {
            char got[8] = "";
            const ssize_t sent = send(client, "probe", 5, 0);
            const ssize_t read = recv(accepted, got, sizeof(got) - 1u, 0);
            const ssize_t back = read > 0 ? send(accepted, got, (size_t)read, 0) : -1;
            const ssize_t returned = recv(client, echoed, sizeof(echoed) - 1u, 0);
            if (sent != 5 || read != 5 || back != 5 || returned != 5) {
                snprintf(why, sizeof(why),
                         "send gave %ld, the server's recv %ld, its send %ld, the client's recv %ld; "
                         "expected 5 each",
                         (long)sent, (long)read, (long)back, (long)returned);
            }
        }
    }
    const int opened[3] = { accepted, client, server };
    for (size_t i = 0; i < 3u; ++i) {
        if (opened[i] >= 0) {
            close(opened[i]);
        }
    }
    CHECK_TRUE(SOCKET, NAME, CHECK_EXACT, why[0] == '\0' && strcmp(echoed, "probe") == 0,
               "%s%sthe client read back \"%s\", expected \"probe\"", why, why[0] ? "; " : "",
               echoed);
}

static void UdpEcho(void) {
    static const char *const NAME = "a UDP datagram goes over loopback and comes back";
    if (!ReportStart(SOCKET, NAME)) {
        return;
    }
    char why[128] = "";
    struct sockaddr_in to, from;
    const int a = BindLoopback(SOCK_DGRAM, &to, why, sizeof(why));
    const int b = a >= 0 ? BindLoopback(SOCK_DGRAM, &from, why, sizeof(why)) : -1;
    char got[8] = "";
    char back[8] = "";
    if (a >= 0 && b >= 0) {
        struct sockaddr_in sender;
        socklen_t length = sizeof(sender);
        const ssize_t sent = sendto(b, "probe", 5, 0, (struct sockaddr *)&to, sizeof(to));
        const ssize_t read =
            recvfrom(a, got, sizeof(got) - 1u, 0, (struct sockaddr *)&sender, &length);
        const ssize_t answered =
            read > 0 ? sendto(a, got, (size_t)read, 0, (struct sockaddr *)&sender, length) : -1;
        const ssize_t returned = recv(b, back, sizeof(back) - 1u, 0);
        if (sent != 5 || read != 5 || answered != 5 || returned != 5 ||
            sender.sin_port != from.sin_port) {
            snprintf(why, sizeof(why),
                     "sendto gave %ld, recvfrom %ld from port %u (the sender is %u), the answer "
                     "%ld, the recv %ld; expected 5 each",
                     (long)sent, (long)read, (unsigned)ntohs(sender.sin_port),
                     (unsigned)ntohs(from.sin_port), (long)answered, (long)returned);
        }
    }
    if (a >= 0) {
        close(a);
    }
    if (b >= 0) {
        close(b);
    }
    CHECK_TRUE(SOCKET, NAME, CHECK_EXACT, why[0] == '\0' && strcmp(back, "probe") == 0,
               "%s%sthe sender read back \"%s\", expected \"probe\"", why, why[0] ? "; " : "",
               back);
}

static void Refused(void) {
    static const char *const NAME = "a connection to a closed loopback port is refused";
    if (!ReportStart(SOCKET, NAME)) {
        return;
    }
    // A port the stack just gave out and took back: nothing listens there.
    char why[128] = "";
    struct sockaddr_in address;
    const int taken = BindLoopback(SOCK_STREAM, &address, why, sizeof(why));
    int result = 0, error = 0;
    if (taken >= 0) {
        close(taken);
        const int s = socket(AF_INET, SOCK_STREAM, 0);
        errno = 0;
        result = s >= 0 ? connect(s, (struct sockaddr *)&address, sizeof(address)) : -2;
        error = errno;
        if (s >= 0) {
            close(s);
        }
    }
    CHECK_TRUE(SOCKET, NAME, CHECK_EXACT, why[0] == '\0' && result == -1 && error == ECONNREFUSED,
               "%s%sconnect returned %d with errno %d, expected -1 with ECONNREFUSED (%d)", why,
               why[0] ? "; " : "", result, error, ECONNREFUSED);
}

bool SocketStreamsRefused(char *why, size_t why_size) {
    errno = 0;
    const int s = socket(AF_INET, SOCK_STREAM, 0);
    const int error = s < 0 ? (errno != 0 ? errno : -1) : 0;
    if (s >= 0) {
        close(s);
    }
    char network_off[160] = "";
    if (error != 0) {
        PlatformNetworkOff(network_off, sizeof(network_off));
    }
    return NetStreamsSkip(why, why_size, error, error > 0 ? strerror(error) : "", network_off);
}

void RunSocket(void) {
    static const char *const TCP = "a TCP socket listens on loopback, connects and echoes";
    static const char *const UDP = "a UDP datagram goes over loopback and comes back";
    static const char *const REFUSED = "a connection to a closed loopback port is refused";
    if (!PlatformSocketsOpen()) {
        const char *const names[] = { TCP, UDP, REFUSED };
        for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
            ReportSkip(SOCKET, names[i], REPORT_REASON_UNAVAILABLE, PlatformError());
        }
        return;
    }
    // A machine whose network is off may refuse every stream socket, loopback's too, as Eden
    // does in its flight mode, its default: the stream cases say so rather than fail on it.
    char streams_why[REPORT_KEPT_DETAIL];
    const bool streams_refused = SocketStreamsRefused(streams_why, sizeof(streams_why));
    if (streams_refused) {
        ReportSkip(SOCKET, TCP, REPORT_REASON_UNAVAILABLE, streams_why);
    } else {
        TcpEcho();
    }
    UdpEcho();
    if (streams_refused) {
        ReportSkip(SOCKET, REFUSED, REPORT_REASON_UNAVAILABLE, streams_why);
    } else {
        Refused();
    }
    PlatformSocketsClose();
}
