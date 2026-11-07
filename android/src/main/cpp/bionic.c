// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
/* bionic's own behaviour, each case one function that returns what it measured as text (BionicCases.java compares);
   what the C standard defines is the libc family's (source/tests/t_libc.c). */
#include "probe.h"

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/system_properties.h>
#include <unistd.h>
#include <wchar.h>


/* what C leaves to the library: a negative NaN's sign, and %a's first digit for a normal number */
JNIEXPORT jstring JNICALL PROBE_FN(printfWords)(JNIEnv* env, jclass cls) {
    (void)cls;
    return probe_string(env, "%f|%a", copysign(NAN, -1.0), 1.0);
}

JNIEXPORT jstring JNICALL PROBE_FN(strerror)(JNIEnv* env, jclass cls) {
    (void)cls;
    errno = 0;
    int fd = open("/nonexistent-edenprobe", O_RDONLY);
    int e = errno;
    if (fd >= 0) close(fd);
    char t[4][96];
    const int codes[4] = {ENOENT, EAGAIN, ENOSYS, 9999};
    for (int i = 0; i < 4; ++i) snprintf(t[i], sizeof t[i], "%s", strerror(codes[i]));
    return probe_string(env, "open %ld errno %d|%s|%s|%s|%s", (long)fd, e, t[0], t[1], t[2], t[3]);
}

JNIEXPORT jstring JNICALL PROBE_FN(dl)(JNIEnv* env, jclass cls) {
    (void)cls;
    void* h = dlopen("libc.so", RTLD_NOW);
    size_t (*len)(const char*) = h ? (size_t(*)(const char*))dlsym(h, "strlen") : NULL;
    dlerror();
    void* missing = h ? dlsym(h, "edenprobe_no_such_symbol") : NULL;
    const char* e = dlerror();
    char e1[256];
    snprintf(e1, sizeof e1, "%s", e ? "yes" : "no");
    void* lib = dlopen("libedenprobe_missing.so", RTLD_NOW);
    e = dlerror();
    char e2[512];
    snprintf(e2, sizeof e2, "%s", e && strstr(e, "libedenprobe_missing.so") ? "names it" : e ? e : "none");
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
        while (!found && getline(&line, &cap, f) > 0) found = strstr(line, "libedenprobe.so") != NULL;
        free(line);
        fclose(f);
    }
    char exe[256] = "";
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    exe[n > 0 ? n : 0] = 0;
    return probe_string(env, "cmdline %s|maps %s|exe %s", cmd, f ? (found ? "names libedenprobe.so" : "lacks libedenprobe.so") : "unreadable",
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
