// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
package org.scarletnx.edenprobe;

/**
 * The native side of Android's own cases, in libedenprobe.so beside the engine, which Engine loaded: each returns
 * what it measured as text, and the case compares it with what Android gives.
 */
final class NativeLib {
    private NativeLib() {}

    // the library: JNI_OnLoad's count and checks
    static native String onLoad();

    // bionic's own words and the system under it
    static native String printfWords();
    static native String strerror();
    static native String dl();
    static native String memory();
    static native String procSelf(String packageName);
    static native String signals();
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

    // bionic's resolver
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
