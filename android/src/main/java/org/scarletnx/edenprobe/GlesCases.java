// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
package org.scarletnx.edenprobe;

/**
 * EGL and GLES from native code on a 16 by 16 pbuffer of its own, read back with glReadPixels. A pixel is RGBA as
 * 8 hex digits. (0,0) is the bottom left, as GL's window coordinates are.
 */
final class GlesCases {
    private GlesCases() {}

    static final class EglInit implements Case {
        @Override public Outcome run(Env env) {
            return Outcome.check("EGL 1.4 or newer|an ES2 pbuffer config|an ES3 pbuffer config", NativeLib.eglInit());
        }
    }

    static final class ClearRead implements Case {
        @Override public Outcome run(Env env) {
            return Outcome.check("ff0000ff ff0000ff", NativeLib.glClearRead());
        }
    }

    /** glClear writes only the channels the color mask lets through. */
    static final class ColorMaskClear implements Case {
        @Override public Outcome run(Env env) {
            return Outcome.check("ff00ffff", NativeLib.glColorMask());
        }
    }

    /** A blue triangle over the bottom-left half: a pixel inside it, one outside on the black clear. */
    static final class Triangle implements Case {
        @Override public Outcome run(Env env) {
            return Outcome.check("0000ffff 000000ff", NativeLib.glTriangle());
        }
    }

    /** A 2 by 2 texture, nearest, over the whole surface: its first row is GL's bottom. */
    static final class TextureNearest implements Case {
        @Override public Outcome run(Env env) {
            return Outcome.check("ff0000ff 00ff00ff 0000ffff ffffffff", NativeLib.glTexture());
        }
    }

    /** White at alpha 0.5 blended over opaque black with SRC_ALPHA, ONE_MINUS_SRC_ALPHA. */
    static final class Blend implements Case {
        @Override public Outcome run(Env env) {
            String got = NativeLib.glBlend();
            String want = "128 128 128 191, each channel within 1";
            int[] w = {128, 128, 128, 191};
            String[] g = got.split(" ");
            boolean ok = g.length == 4;
            for (int i = 0; ok && i < 4; ++i) ok = java.lang.Math.abs(Integer.parseInt(g[i]) - w[i]) <= 1;
            return ok ? Outcome.pass() : Outcome.fail("got " + got + ", expected " + want);
        }
    }

    /** A shader that does not compile, with its log; an enum glEnable does not know, then no error left. */
    static final class Errors implements Case {
        @Override public Outcome run(Env env) {
            return Outcome.check("compile status 0, log yes|0x0500 then 0x0000", NativeLib.glErrors());
        }
    }

    /** A GLES 3 context: its major version, a texture framebuffer, a GLSL ES 3.00 shader's integer operations. */
    static final class Gl3Framebuffer implements Case {
        @Override public Outcome run(Env env) {
            return Outcome.check("major 3 or more|complete 0x8cd5|050300ff", NativeLib.gl3Framebuffer());
        }
    }

    /** A GLES 1.1 context cleared and read back. */
    static final class Gl1ClearRead implements Case {
        @Override public Outcome run(Env env) {
            return Outcome.check("00ff00ff", NativeLib.gl1ClearRead());
        }
    }
}
