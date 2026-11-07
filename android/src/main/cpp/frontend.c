// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Android's frontend (docs/Frontends.md, docs/Android.md): the engine runs here as it runs on the
// console, called from the activity's thread through Engine.nativeRun, with Android's steps: the
// judge, the libc family's cases every frontend runs, then Android's own, most of them Java cases
// that report through Engine.start and Engine.result. No screens: a run leaves when its report is
// closed, whatever the mode file says.

#include <jni.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mode.h"
#include "platform.h"
#include "report.h"
#include "runner.h"
#include "tests/tests.h"

static struct {
    JNIEnv *env;
    jclass engine;
    jmethodID suite;
} g_frontend;

/// Runs one Java suite: Engine.suite(name), which runs its cases through Engine.start and
/// Engine.result. An exception it lets out is a note, never a lost run: its cases that reported
/// are in the report, and the ones it did not reach are missing from it, which the note names.
static void RunJava(const char *name) {
    JNIEnv *const env = g_frontend.env;
    jstring suite = (*env)->NewStringUTF(env, name);
    (*env)->CallStaticVoidMethod(env, g_frontend.engine, g_frontend.suite, suite);
    (*env)->DeleteLocalRef(env, suite);
    if ((*env)->ExceptionCheck(env)) {
        jthrowable thrown = (*env)->ExceptionOccurred(env);
        (*env)->ExceptionClear(env);
        jclass of = (*env)->GetObjectClass(env, thrown);
        jmethodID text = (*env)->GetMethodID(env, of, "toString", "()Ljava/lang/String;");
        jstring said = text != NULL ? (jstring)(*env)->CallObjectMethod(env, thrown, text) : NULL;
        const char *chars = said != NULL ? (*env)->GetStringUTFChars(env, said, NULL) : NULL;
        ReportNote("suite %s: the Java suite threw %s, and its cases after that one are missing",
                   name, chars != NULL ? chars : "an exception that names nothing");
        if (chars != NULL) {
            (*env)->ReleaseStringUTFChars(env, said, chars);
        }
        (*env)->ExceptionClear(env);
    }
}

static void RunFramework(void) {
    RunJava("framework");
}
static void RunResources(void) {
    RunJava("resources");
}
static void RunNetJava(void) {
    RunJava("net");
}
static void RunJni(void) {
    RunJava("jni");
}
static void RunBionic(void) {
    RunJava("bionic");
}
static void RunGles(void) {
    RunJava("gles");
}

static void RunSupportSteps(void) {
    RunSupport();
    RunSupportPerf();
    RunSupportSettle();
}

// The judge first and always, as on the console; then the libc family, the C library under every
// frontend, then bionic's own; then the graphics, net, jni and framework families, Android's.
static const RunnerStep STEPS[] = {
    { "selfcheck", RunSelfcheck, 0, false, false },
    { "support", RunSupportSteps, 0, false, false },
    { "reference", RunReference, 0, false, false },
    { "libc", RunLibc, MODE_FAMILY_LIBC, false, true },
    { "system", RunSystem, MODE_FAMILY_LIBC, false, true },
    { "socket", RunSocket, MODE_FAMILY_LIBC, false, true },
    { "bionic", RunBionic, MODE_FAMILY_LIBC, false, true },
    { "gles", RunGles, MODE_FAMILY_GRAPHICS, false, true },
    { "net", RunNetJava, MODE_FAMILY_NET, false, true },
    { "jni", RunJni, MODE_FAMILY_JNI, false, true },
    { "framework", RunFramework, MODE_FAMILY_FRAMEWORK, false, true },
    { "resources", RunResources, MODE_FAMILY_FRAMEWORK, false, true },
};

#define ANDROID_FAMILIES                                                                          \
    ((uint32_t)(MODE_FAMILY_LIBC | MODE_FAMILY_GRAPHICS | MODE_FAMILY_NET | MODE_FAMILY_JNI |      \
                MODE_FAMILY_FRAMEWORK))

// Every family runs when the file names none: none of them needs a person, a server or sound.
static RunnerFrontend g_android = {
    .version = EDENPROBE_VERSION,
    .build = EDENPROBE_BUILD,
    .hash = EDENPROBE_HASH,
    .steps = STEPS,
    .step_count = sizeof(STEPS) / sizeof(STEPS[0]),
    .present = ANDROID_FAMILIES,
    .asked = 0u,
};

static Runner g_runner;

static void ReportTier(const char *word, CheckTier *tier) {
    static const struct {
        const char *word;
        CheckTier tier;
    } TIERS[] = { { "none", CHECK_NONE },         { "exact", CHECK_EXACT },
                  { "ulp", CHECK_ULP },           { "position", CHECK_POSITION },
                  { "level", CHECK_LEVEL },       { "bound", CHECK_BOUND } };
    for (size_t i = 0; i < sizeof(TIERS) / sizeof(TIERS[0]); ++i) {
        if (strcmp(word, TIERS[i].word) == 0) {
            *tier = TIERS[i].tier;
            return;
        }
    }
    *tier = CHECK_NONE;
    ReportNote("bookkeeping: a Java case gave the tier \"%s\", which is none of none, exact, ulp, "
               "position, level or bound; its line says none",
               word);
}

/// What the device is, for the note after the mode line: Java's words, with what only the native
/// side knows.
static void NoteDevice(const char *java) {
#if defined(__aarch64__)
    static const char *const ABI = "arm64-v8a";
#elif defined(__x86_64__)
    static const char *const ABI = "x86_64";
#else
    static const char *const ABI = "another ABI";
#endif
    ReportNote("device: %s, native library %s, page size %ld", java, ABI, sysconf(_SC_PAGESIZE));
}

JNIEXPORT jint JNICALL Java_org_scarletnx_edenprobe_Engine_nativeRun(JNIEnv *env, jclass engine,
                                                                    jstring root, jstring tmp,
                                                                    jstring device) {
    g_frontend.env = env;
    g_frontend.engine = engine;
    g_frontend.suite = (*env)->GetStaticMethodID(env, engine, "suite", "(Ljava/lang/String;)V");
    if (g_frontend.suite == NULL) {
        return -1;
    }
    // The C library's place for temporary files, which the support suite writes its checks in:
    // the app's cache folder, the one an app may write without asking.
    const char *const tmp_chars = (*env)->GetStringUTFChars(env, tmp, NULL);
    setenv("TMPDIR", tmp_chars, 1);
    (*env)->ReleaseStringUTFChars(env, tmp, tmp_chars);
    static char root_path[RUNNER_ROOT_MAX + 1u];
    const char *const root_chars = (*env)->GetStringUTFChars(env, root, NULL);
    const bool fits = strlen(root_chars) <= RUNNER_ROOT_MAX;
    snprintf(root_path, sizeof(root_path), "%s", root_chars);
    (*env)->ReleaseStringUTFChars(env, root, root_chars);
    g_android.root = root_path;
    if (!fits || !RunnerInit(&g_runner, &g_android)) {
        char said[RUNNER_ROOT_MAX + 96u];
        snprintf(said, sizeof(said), "edenprobe: the folder %s is longer than %u characters",
                 root_path, (unsigned)RUNNER_ROOT_MAX);
        PlatformLog(said, strlen(said));
        return -1;
    }

    const RunMode from_file = RunnerModeFromFile(g_runner.mode_path);
    RunnerPlan(&g_runner, &from_file);
    // No screen to ask whether to go on with a run left in the middle: it goes on, as a quick run
    // does on the console.
    RunMode mode = RunnerModeAtStart(&g_runner, &from_file);
    RunnerOpen(&g_runner, &mode);
    if (g_runner.plan.start == JOURNAL_FRESH) {
        RunnerBeginJournal(&g_runner, &from_file, &mode);
    }
    const uint32_t families = RunnerFamilies(&g_runner, &mode);
    RunnerApplyMode(&g_runner, &mode);
    if (g_runner.plan.start == JOURNAL_FRESH) {
        if (mode.detailed) {
            ReportNote("detailed: Android's frontend has no screens, so the run leaves once its "
                       "report is closed, as a quick one does");
        }
        const char *const device_chars = (*env)->GetStringUTFChars(env, device, NULL);
        NoteDevice(device_chars);
        (*env)->ReleaseStringUTFChars(env, device, device_chars);
    }
    const bool timing = !mode.no_timing;
    RunnerSteps(&g_runner, families, timing, NULL);
    RunnerFinish(&g_runner, &mode, timing);
    return (jint)ReportFailed();
}

JNIEXPORT jboolean JNICALL Java_org_scarletnx_edenprobe_Engine_start(JNIEnv *env, jclass engine,
                                                                    jstring suite, jstring name) {
    (void)engine;
    const char *const s = (*env)->GetStringUTFChars(env, suite, NULL);
    const char *const n = (*env)->GetStringUTFChars(env, name, NULL);
    const bool runs = ReportStart(s, n);
    (*env)->ReleaseStringUTFChars(env, name, n);
    (*env)->ReleaseStringUTFChars(env, suite, s);
    return runs ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_org_scarletnx_edenprobe_Engine_result(JNIEnv *env, jclass engine,
                                                                 jstring suite, jstring name,
                                                                 jstring tier, jboolean passed,
                                                                 jstring detail) {
    (void)engine;
    const char *const s = (*env)->GetStringUTFChars(env, suite, NULL);
    const char *const n = (*env)->GetStringUTFChars(env, name, NULL);
    const char *const t = (*env)->GetStringUTFChars(env, tier, NULL);
    const char *const d = (*env)->GetStringUTFChars(env, detail, NULL);
    CheckTier claimed;
    ReportTier(t, &claimed);
    ReportCase(s, n, claimed, passed == JNI_TRUE, passed == JNI_TRUE ? NULL : d);
    (*env)->ReleaseStringUTFChars(env, detail, d);
    (*env)->ReleaseStringUTFChars(env, tier, t);
    (*env)->ReleaseStringUTFChars(env, name, n);
    (*env)->ReleaseStringUTFChars(env, suite, s);
}

JNIEXPORT void JNICALL Java_org_scarletnx_edenprobe_Engine_skip(JNIEnv *env, jclass engine,
                                                               jstring suite, jstring name,
                                                               jstring detail) {
    (void)engine;
    const char *const s = (*env)->GetStringUTFChars(env, suite, NULL);
    const char *const n = (*env)->GetStringUTFChars(env, name, NULL);
    const char *const d = (*env)->GetStringUTFChars(env, detail, NULL);
    ReportSkip(s, n, REPORT_REASON_UNAVAILABLE, d);
    (*env)->ReleaseStringUTFChars(env, detail, d);
    (*env)->ReleaseStringUTFChars(env, name, n);
    (*env)->ReleaseStringUTFChars(env, suite, s);
}
