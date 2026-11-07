// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
package org.scarletnx.edenprobe;

/**
 * edenprobe's engine in libedenprobe.so, the same C the console's NRO runs (docs/Frontends.md): it reads the mode
 * file, keeps the journal, runs every step in its order and writes the report. The Java suites are steps too: the
 * engine calls {@link #suite} for each, and each case says through {@link #start} that it begins and through
 * {@link #result} or {@link #skip} how it ended, so a case that ends the process is the one the next launch skips.
 */
final class Engine {
    private Engine() {}

    static {
        System.loadLibrary("edenprobe");
    }

    private static Env env;

    /**
     * Runs the probe under root (its mode file, crash list and runs/), with tmp as the C library's TMPDIR; device is
     * what the report's device note says the machine is. Returns how many cases failed, or -1 when the run could not
     * start (logcat says why); the report says the rest.
     */
    static int run(Env with, String root, String tmp, String device) {
        env = with;
        return nativeRun(root, tmp, device);
    }

    private static native int nativeRun(String root, String tmp, String device);

    /** Whether the case runs: false when the crash list or a crash in this run says not, its skip line written. */
    static native boolean start(String suite, String name);

    /** The case's line; tier is the report's (exact, ulp, level, position, none), detail read only on a failure. */
    static native void result(String suite, String name, String tier, boolean passed, String detail);

    /** A case this machine cannot ask, with the report's reason "unavailable". */
    static native void skip(String suite, String name, String detail);

    /** Called by the engine for the Java suites' steps, on the thread that called run. */
    @SuppressWarnings("unused")
    private static void suite(String name) {
        Suites.run(name, env);
    }
}
