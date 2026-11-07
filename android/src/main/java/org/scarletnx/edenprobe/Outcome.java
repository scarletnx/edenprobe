// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
package org.scarletnx.edenprobe;

/**
 * What a case found: whether it passed, and in words what it got against what Android gives; or that it could not ask
 * here (a skip, the report's reason "unavailable").
 */
final class Outcome {
    final boolean passed, skipped;
    final String detail;

    private Outcome(boolean passed, boolean skipped, String detail) {
        this.passed = passed;
        this.skipped = skipped;
        this.detail = detail;
    }

    /** pass when got is want, character for character */
    static Outcome check(String want, String got) {
        return want.equals(got) ? pass() : fail("got " + got + ", expected " + want);
    }

    static Outcome pass() {
        return new Outcome(true, false, "");
    }

    static Outcome fail(String detail) {
        return new Outcome(false, false, detail);
    }

    /** a case this machine cannot ask: an API level below the one it needs */
    static Outcome skip(String detail) {
        return new Outcome(false, true, detail);
    }

    /** a case that threw where it expected none: the exception is what it found */
    static Outcome threw(Throwable t) {
        return fail("threw " + describe(t));
    }

    /** an exception's class and message, and its cause's, as a case reports one */
    static String describe(Throwable t) {
        StringBuilder b = new StringBuilder(t.getClass().getName());
        if (t.getMessage() != null) b.append(": ").append(t.getMessage());
        Throwable cause = t.getCause();
        if (cause != null && cause != t) {
            b.append(" (caused by ").append(cause.getClass().getName());
            if (cause.getMessage() != null) b.append(": ").append(cause.getMessage());
            b.append(')');
        }
        return b.toString();
    }
}
