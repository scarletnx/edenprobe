// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
package org.scarletnx.edenprobe;

import java.nio.ByteBuffer;
import java.util.Locale;

/** JNI as ART gives it to an app's native code. */
final class JniCases {
    private JniCases() {}

    static final class Version implements Case {
        @Override public Outcome run(Env env) {
            return Outcome.check("0x00010006", String.format(Locale.ROOT, "0x%08x", NativeLib.version()));
        }
    }

    /** JNI_OnLoad ran once with a JavaVM whose GetEnv gives JNI 1.6, and both ways of binding a native method work. */
    static final class OnLoadAndBinding implements Case {
        @Override public Outcome run(Env env) {
            return Outcome.check("onload 1, GetEnv 0|registered|by name", NativeLib.onLoad() + "|" + NativeLib.registered() + "|" + NativeLib.byName());
        }
    }

    /**
     * FindClass of an app class: found from a thread that Java called in on; from a thread the native code started
     * and attached, the system class loader has no app class, so it fails with ART's ClassNotFoundException.
     */
    static final class FindClassThreads implements Case {
        @Override public Outcome run(Env env) {
            return Outcome.check("java thread found|attached thread java.lang.ClassNotFoundException", NativeLib.findClass());
        }
    }

    /**
     * GetStringUTFChars of a string with U+00E7, U+1F600 (a surrogate pair), U+0000: its bytes and lengths. ART writes a
     * supplementary character as 4-byte UTF-8, not as the two 3-byte surrogates of the JNI specification; NUL is C0 80.
     */
    static final class StringUtf implements Case {
        @Override public Outcome run(Env env) {
            return Outcome.check("61c3a7f09f9880c08062 utf-length 10 length 6", NativeLib.utf("aç😀\u0000b"));
        }
    }

    /** A Java exception seen from C (ExceptionCheck, its class and message), and ThrowNew seen from Java. */
    static final class Exceptions implements Case {
        @Override public Outcome run(Env env) {
            String fromC = NativeLib.catchJava();
            String fromJava;
            try {
                NativeLib.throwNative();
                fromJava = "returned";
            } catch (IllegalStateException e) {
                fromJava = e.getClass().getName() + ": " + e.getMessage();
            }
            return Outcome.check("java.lang.IllegalArgumentException: from java|java.lang.IllegalStateException: from native", fromC + "|" + fromJava);
        }
    }

    /** Array elements copied back on release, a critical region, and direct buffers both ways. */
    static final class ArraysAndBuffers implements Case {
        @Override public Outcome run(Env env) {
            int[] ints = {1, 2, 3, 4};
            byte[] bytes = new byte[4];
            ByteBuffer direct = ByteBuffer.allocateDirect(16);
            String c = NativeLib.arrays(ints, bytes, direct, ByteBuffer.wrap(new byte[8]));
            ByteBuffer made = NativeLib.newDirect();
            String got = c + "|java sees " + ints[0] + " " + bytes[3] + " " + direct.get(5) + "|new direct " + made.capacity() + " "
                + made.isDirect() + " " + made.get(31);
            return Outcome.check("sum 10, direct capacity 16, heap capacity -1|java sees 100 3 5|new direct 32 true 31", got);
        }
    }

    /** Call*Method with mixed arguments (variadic floats are doubles), its A form, and a float return. */
    static final class Calls implements Case {
        @Override public Outcome run(Env env) {
            String s = "1 -2 3.5 4.25 five true x -6 7 8.125 9.5";
            return Outcome.check(s + "|" + s + "|1.25", NativeLib.calls());
        }
    }
}
