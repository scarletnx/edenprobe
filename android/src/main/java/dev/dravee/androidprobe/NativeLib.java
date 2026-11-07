// SPDX-License-Identifier: GPL-3.0-or-later
package dev.dravee.androidprobe;

/**
 * libandroidprobe.so: the bionic, JNI, GLES and native net cases. Each native case calls require() first, so a library
 * that did not load fails every one of them with the same cause instead of a bare UnsatisfiedLinkError.
 */
final class NativeLib {
    private NativeLib() {}

    private static final Throwable LOAD_ERROR;
    static {
        Throwable error = null;
        try {
            System.loadLibrary("androidprobe");
        } catch (Throwable t) {
            error = t;
        }
        LOAD_ERROR = error;
    }

    static void require() {
        if (LOAD_ERROR != null) throw new IllegalStateException("libandroidprobe.so did not load: " + Outcome.describe(LOAD_ERROR), LOAD_ERROR);
    }

    /** null when the library loaded, else why not */
    static String loadError() {
        return LOAD_ERROR == null ? null : Outcome.describe(LOAD_ERROR);
    }

    // the library: its ABI, and JNI_OnLoad's count and checks
    static native String abi();
    static native String onLoad();

    // bionic
    static native String printf();
    static native String scanf();
    static native String strtod();
    static native String strerror();
    static native String threads();
    static native String time();
    static native String dl();
    static native String memory();
    static native String procSelf(String packageName);
    static native String signals();
    static native String jumps();
    static native String math();
    static native String property();
    static native String multibyte();

    // JNI
    static native int version();
    static native String findClass();
    static native String utf(String s);
    static native String catchJava();
    static native void throwNative();
    static native String arrays(int[] ints, byte[] bytes, java.nio.ByteBuffer direct, java.nio.ByteBuffer heap);
    static native java.nio.ByteBuffer newDirect();
    static native String calls();
    static native String registered(); // by RegisterNatives in JNI_OnLoad
    static native String byName();     // by its JNI name

    // GLES
    static native String eglInit();
    static native String glClearRead();
    static native String glColorMask();
    static native String glTriangle();
    static native String glTexture();
    static native String glBlend();
    static native String glErrors();
    static native String gl3Framebuffer();
    static native String gl1ClearRead();

    // net
    static native String tcpLoopback();
    static native String connectRefused(int port);
    static native String resolve(String host);

    // what the JNI cases call back
    static String mix(int a, long b, float c, double d, String e, boolean f, char g, short h, byte i, double j, float k) {
        return a + " " + b + " " + c + " " + d + " " + e + " " + f + " " + g + " " + h + " " + i + " " + j + " " + k;
    }

    static float half(float x) {
        return x / 2;
    }

    static void thrower() {
        throw new IllegalArgumentException("from java");
    }
}
