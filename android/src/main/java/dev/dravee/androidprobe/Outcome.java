// SPDX-License-Identifier: GPL-3.0-or-later
package dev.dravee.androidprobe;

/** What a case found: its result, the value it measured (got), the value Android gives (want) and why, in words. */
final class Outcome {
    final String result, detail, got, want, reason;

    private Outcome(String result, String detail, String got, String want, String reason) {
        this.result = result;
        this.detail = detail;
        this.got = got;
        this.want = want;
        this.reason = reason;
    }

    /** pass when got is want, character for character */
    static Outcome check(String want, String got) {
        return want.equals(got) ? new Outcome("pass", "", got, want, null) : fail("got " + got + ", expected " + want, got, want);
    }

    static Outcome pass(String got, String want) {
        return new Outcome("pass", "", got, want, null);
    }

    static Outcome fail(String detail, String got, String want) {
        return new Outcome("fail", detail, got, want, null);
    }

    static Outcome skip(String reason, String detail) {
        return new Outcome("skip", detail, null, null, reason);
    }

    /** a case that threw where it expected none: the exception is what it measured */
    static Outcome threw(Throwable t) {
        String got = "threw " + describe(t);
        return fail(got, got, null);
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
