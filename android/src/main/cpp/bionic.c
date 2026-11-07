/* SPDX-License-Identifier: GPL-3.0-or-later */
/* bionic's behaviour, each case one function that returns what it measured as text (BionicCases.java compares). */
#include "probe.h"

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <setjmp.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/system_properties.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>

#include "math_expected.h"

JNIEXPORT jstring JNICALL PROBE_FN(printf)(JNIEnv* env, jclass cls) {
    (void)cls;
    char a[512], b[128];
    snprintf(a, sizeof a, "%d|%5.2f|%e|%g|%a|%x|%s|%c|%%|%lld|%zu|%.3s|%+05d|%-4d|%o", -42, 3.14159, 12345.678, 0.0001,
             1.0, 255u, "str", 'c', -9000000000LL, (size_t)7, "abcdef", 42, 7, 8);
    snprintf(b, sizeof b, "%f %f %F %f", (double)INFINITY, (double)NAN, (double)INFINITY, copysign(NAN, -1.0));
    return probe_string(env, "%s|%s", a, b);
}

JNIEXPORT jstring JNICALL PROBE_FN(scanf)(JNIEnv* env, jclass cls) {
    (void)cls;
    int i = 0, x = 0, n = -1;
    double d = 0;
    char w[16] = "";
    int r = sscanf("42 3.5 word 0x1f rest", "%d %lf %15s %x %n", &i, &d, w, &x, &n);
    return probe_string(env, "%d|%d|%.3f|%s|%d|%ld", r, i, d, w, x, (long)n);
}

JNIEXPORT jstring JNICALL PROBE_FN(strtod)(JNIEnv* env, jclass cls) {
    (void)cls;
    char* end = NULL;
    errno = 0;
    double a = strtod("0x1.8p3", &end);
    errno = 0;
    double b = strtod("1e400", NULL);
    int eb = errno;
    errno = 0;
    long c = strtol("99999999999999999999", NULL, 10);
    int ec = errno;
    unsigned long d = strtoul("-1", NULL, 10);
    long e = strtol("0x1F", NULL, 0), f = strtol("017", NULL, 0);
    return probe_string(env, "%f %s|%f errno %d|%ld errno %d|%lu|%ld %ld", a, end && !*end ? "end" : "not at the end", b, eb,
                        c, ec, d, e, f);
}

JNIEXPORT jstring JNICALL PROBE_FN(strerror)(JNIEnv* env, jclass cls) {
    (void)cls;
    errno = 0;
    int fd = open("/nonexistent-androidprobe", O_RDONLY);
    int e = errno;
    if (fd >= 0) close(fd);
    char t[4][96];
    const int codes[4] = {ENOENT, EAGAIN, ENOSYS, 9999};
    for (int i = 0; i < 4; ++i) snprintf(t[i], sizeof t[i], "%s", strerror(codes[i]));
    return probe_string(env, "open %ld errno %d|%s|%s|%s|%s", (long)fd, e, t[0], t[1], t[2], t[3]);
}

static struct {
    pthread_mutex_t mutex;
    long sum;
    pthread_key_t key;
    atomic_int destructors;
    atomic_int own_tls;
} g_threads = {.mutex = PTHREAD_MUTEX_INITIALIZER};
static __thread int t_index = -1;

static void count_destructor(void* value) {
    (void)value;
    atomic_fetch_add(&g_threads.destructors, 1);
}

static void* adder(void* arg) {
    int index = (int)(intptr_t)arg;
    t_index = index;
    pthread_setspecific(g_threads.key, (void*)(intptr_t)(index + 1));
    for (int k = 0; k < 10000; ++k) {
        pthread_mutex_lock(&g_threads.mutex);
        ++g_threads.sum;
        pthread_mutex_unlock(&g_threads.mutex);
        if (k % 1000 == 0) sched_yield();
    }
    if (t_index == index) atomic_fetch_add(&g_threads.own_tls, 1);
    return NULL;
}

JNIEXPORT jstring JNICALL PROBE_FN(threads)(JNIEnv* env, jclass cls) {
    (void)cls;
    g_threads.sum = 0;
    atomic_store(&g_threads.destructors, 0);
    atomic_store(&g_threads.own_tls, 0);
    int r = pthread_key_create(&g_threads.key, count_destructor);
    if (r) return probe_string(env, "pthread_key_create %d", r);
    pthread_t t[4];
    for (int i = 0; i < 4; ++i)
        if ((r = pthread_create(&t[i], NULL, adder, (void*)(intptr_t)i))) return probe_string(env, "pthread_create %d", r);
    for (int i = 0; i < 4; ++i) pthread_join(t[i], NULL);
    pthread_key_delete(g_threads.key);
    return probe_string(env, "sum %ld|destructors %d|own tls %d", g_threads.sum, atomic_load(&g_threads.destructors),
                        atomic_load(&g_threads.own_tls));
}

JNIEXPORT jstring JNICALL PROBE_FN(time)(JNIEnv* env, jclass cls) {
    (void)cls;
    time_t t = 1700000000;
    struct tm tm;
    gmtime_r(&t, &tm);
    char b[64];
    strftime(b, sizeof b, "%Y-%m-%dT%H:%M:%S %a %b %j %u", &tm);
    struct timespec a, z, d = {0, 20000000};
    clock_gettime(CLOCK_MONOTONIC, &a);
    nanosleep(&d, NULL);
    clock_gettime(CLOCK_MONOTONIC, &z);
    long long ns = (long long)(z.tv_sec - a.tv_sec) * 1000000000LL + (z.tv_nsec - a.tv_nsec);
    if (ns >= 20000000) return probe_string(env, "%s|monotonic advanced 20 ms or more", b);
    return probe_string(env, "%s|monotonic advanced %lld ns over a 20 ms sleep", b, ns);
}

JNIEXPORT jstring JNICALL PROBE_FN(dl)(JNIEnv* env, jclass cls) {
    (void)cls;
    void* h = dlopen("libc.so", RTLD_NOW);
    size_t (*len)(const char*) = h ? (size_t(*)(const char*))dlsym(h, "strlen") : NULL;
    dlerror();
    void* missing = h ? dlsym(h, "androidprobe_no_such_symbol") : NULL;
    const char* e = dlerror();
    char e1[256];
    snprintf(e1, sizeof e1, "%s", e ? "yes" : "no");
    void* lib = dlopen("libandroidprobe_missing.so", RTLD_NOW);
    e = dlerror();
    char e2[512];
    snprintf(e2, sizeof e2, "%s", e && strstr(e, "libandroidprobe_missing.so") ? "names it" : e ? e : "none");
    if (lib) dlclose(lib);
    jstring s = probe_string(env, "libc %s|strlen %zu|missing symbol %s, error %s|missing library %s, error %s", h ? "yes" : "no",
                             len ? len("hello") : 0, missing ? "found" : "NULL", e1, lib ? "loaded" : "NULL", e2);
    if (h) dlclose(h);
    return s;
}

JNIEXPORT jstring JNICALL PROBE_FN(memory)(JNIEnv* env, jclass cls) {
    (void)cls;
    long ps = sysconf(_SC_PAGESIZE);
    int gp = getpagesize();
    char page[96];
    if (ps == gp && ps >= 4096 && !(ps & (ps - 1)))
        snprintf(page, sizeof page, "agrees, a power of two from 4096");
    else
        snprintf(page, sizeof page, "%ld, getpagesize %d", ps, gp);
    unsigned char* p = mmap(NULL, 3 * (size_t)ps, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) return probe_string(env, "page size %s|mmap failed, errno %d", page, errno);
    memset(p, 0xab, 3 * (size_t)ps);
    int pr = mprotect(p, (size_t)ps, PROT_READ);
    int md = madvise(p + ps, (size_t)ps, MADV_DONTNEED);
    unsigned zero = p[ps], kept = p[2 * ps];
    int um = munmap(p, 3 * (size_t)ps);
    if (md) return probe_string(env, "page size %s|protect %d|madvise %d errno %d|untouched 0x%x|unmap %d", page, pr, md, errno, kept, um);
    return probe_string(env, "page size %s|protect %d|dontneed reads %u|untouched 0x%x|unmap %d", page, pr, zero, kept, um);
}

JNIEXPORT jstring JNICALL PROBE_FN(procSelf)(JNIEnv* env, jclass cls, jstring package) {
    (void)cls;
    (void)package;
    char cmd[256] = "(unreadable)";
    FILE* f = fopen("/proc/self/cmdline", "r");
    if (f) {
        size_t n = fread(cmd, 1, sizeof cmd - 1, f);
        cmd[n] = 0;
        fclose(f);
    }
    int found = 0;
    f = fopen("/proc/self/maps", "r");
    if (f) {
        char* line = NULL;
        size_t cap = 0;
        while (!found && getline(&line, &cap, f) > 0) found = strstr(line, "libandroidprobe.so") != NULL;
        free(line);
        fclose(f);
    }
    char exe[256] = "";
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    exe[n > 0 ? n : 0] = 0;
    return probe_string(env, "cmdline %s|maps %s|exe %s", cmd, f ? (found ? "names libandroidprobe.so" : "lacks libandroidprobe.so") : "unreadable",
                        n > 0 ? exe : "unreadable");
}

static struct {
    volatile sig_atomic_t ran;
    volatile int signo, code;
} g_signal;

static void on_usr1(int sig, siginfo_t* si, void* context) {
    (void)context;
    ++g_signal.ran;
    g_signal.signo = sig;
    g_signal.code = si->si_code;
}

JNIEXPORT jstring JNICALL PROBE_FN(signals)(JNIEnv* env, jclass cls) {
    (void)cls;
    struct sigaction sa, old;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_usr1;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigset_t usr1, old_mask, pending;
    sigemptyset(&usr1);
    sigaddset(&usr1, SIGUSR1);
    /* ART blocks SIGUSR1 in its threads for its signal catcher: this thread takes it for the case, then gives it back */
    pthread_sigmask(SIG_UNBLOCK, &usr1, &old_mask);
    sigaction(SIGUSR1, &sa, &old);
    g_signal.ran = 0;
    raise(SIGUSR1);
    int ran1 = g_signal.ran, signo = g_signal.signo, code = g_signal.code;
    pthread_sigmask(SIG_BLOCK, &usr1, NULL);
    raise(SIGUSR1);
    sigemptyset(&pending);
    sigpending(&pending);
    int is_pending = sigismember(&pending, SIGUSR1), ran2 = g_signal.ran;
    pthread_sigmask(SIG_UNBLOCK, &usr1, NULL);
    int ran3 = g_signal.ran;
    sigaction(SIGUSR1, &old, NULL);
    pthread_sigmask(SIG_SETMASK, &old_mask, NULL);
    return probe_string(env, "ran %d signo %d code %ld|blocked: pending %s, ran %d|unblocked: ran %d", ran1, signo, (long)code,
                        is_pending == 1 ? "yes" : "no", ran2, ran3);
}

static jmp_buf g_jump;
static void jump(int value) { longjmp(g_jump, value); }

JNIEXPORT jstring JNICALL PROBE_FN(jumps)(JNIEnv* env, jclass cls) {
    (void)cls;
    static volatile int first, stage, sfirst;
    first = -1;
    stage = 0;
    sfirst = -1;
    int r = setjmp(g_jump);
    if (first < 0) {
        first = r;
        stage = 1;
        jump(7);
    }
    sigjmp_buf sj;
    int r2 = sigsetjmp(sj, 1);
    if (sfirst < 0) {
        sfirst = r2;
        siglongjmp(sj, 9);
    }
    return probe_string(env, "setjmp %d then %d, stage %d|sigsetjmp %d then %d", first, r, stage, sfirst, r2);
}

/* the distance in units in the last place between two doubles (or floats), by their ordered bit patterns */
static long long ulps64(uint64_t a, uint64_t b) {
    int64_t x = (int64_t)a, y = (int64_t)b;
    if (x < 0) x = INT64_MIN - x;
    if (y < 0) y = INT64_MIN - y;
    return (long long)(x - y);
}

static long long ulps32(uint32_t a, uint32_t b) {
    int32_t x = (int32_t)a, y = (int32_t)b;
    if (x < 0) x = INT32_MIN - x;
    if (y < 0) y = INT32_MIN - y;
    return (long long)x - y;
}

static double vd(double x) {
    volatile double v = x;
    return v;
}

static float vf(float x) {
    volatile float v = x;
    return v;
}

JNIEXPORT jstring JNICALL PROBE_FN(math)(JNIEnv* env, jclass cls) {
    (void)cls;
    char out[1024] = "";
    size_t n = 0;
#define VD(x) vd(x)
#define VF(x) vf(x)
#define CHECK_D(fn, call, bits)                                                                            \
    {                                                                                                      \
        double v = call;                                                                                   \
        uint64_t got;                                                                                      \
        memcpy(&got, &v, 8);                                                                               \
        n += (size_t)snprintf(out + n, sizeof out - n, "%s%s %lld", n ? ", " : "", #fn, ulps64(got, bits)); \
    }
#define CHECK_F(fn, call, bits)                                                                            \
    {                                                                                                      \
        float v = call;                                                                                    \
        uint32_t got;                                                                                      \
        memcpy(&got, &v, 4);                                                                               \
        n += (size_t)snprintf(out + n, sizeof out - n, "%s%s %lld", n ? ", " : "", #fn, ulps32(got, bits)); \
    }
    MATH_DOUBLES(CHECK_D)
    MATH_FLOATS(CHECK_F)
    return (*env)->NewStringUTF(env, out);
}

JNIEXPORT jstring JNICALL PROBE_FN(property)(JNIEnv* env, jclass cls) {
    (void)cls;
    char v[PROP_VALUE_MAX] = "";
    int n = __system_property_get("ro.build.version.sdk", v);
    if (n <= 0) return probe_string(env, "no ro.build.version.sdk (%d)", n);
    return (*env)->NewStringUTF(env, v);
}

JNIEXPORT jstring JNICALL PROBE_FN(multibyte)(JNIEnv* env, jclass cls) {
    (void)cls;
    size_t n = mbstowcs(NULL, "a\xc3\xa7\xc3\xa3o", 0);
    return probe_string(env, "mbstowcs %zd|MB_CUR_MAX %zu|wcslen %zu", (ssize_t)n, (size_t)MB_CUR_MAX, wcslen(L"ação"));
}
