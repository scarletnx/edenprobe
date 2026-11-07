// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
package org.scarletnx.edenprobe;

/**
 * The Java suites, each case in the order a run takes it. A suite and a name never change once a report has them: a
 * new behaviour is a new case. Each case is a class of its own, loaded only when it runs, so a class the machine
 * cannot load fails that case alone.
 */
final class Suites {
    private Suites() {}

    private static final class Def {
        final String name, tier, className;

        Def(String name, String tier, String className) {
            this.name = name;
            this.tier = tier;
            this.className = "org.scarletnx.edenprobe." + className;
        }
    }

    private static Def def(String name, String tier, String className) {
        return new Def(name, tier, className);
    }

    private static final Def[] FRAMEWORK = {
        def("LinkProperties.getDhcpServerAddress is the SDK's Inet4Address method, null on a new object", "exact", "FrameworkCases$LinkPropertiesDhcp"),
        def("MediaPlayer.setDataSource of a missing file throws FileNotFoundException", "exact", "FrameworkCases$MediaPlayerMissingFile"),
        def("PdfDocument.writeTo writes what the page's canvas drew", "exact", "FrameworkCases$PdfDocumentDraws"),
        def("AtomicFile.finishWrite leaves the last write in the base file alone", "exact", "FrameworkCases$AtomicFileFinish"),
        def("AtomicFile reads a leftover .bak as the last whole write", "exact", "FrameworkCases$AtomicFileLegacyBackup"),
        def("a PopupWindow shown at a location attaches its content at its size", "exact", "FrameworkCases$PopupWindowShows"),
        def("a Handler runs the front of the queue, then posted, then delayed messages", "exact", "FrameworkCases$HandlerOrder"),
        def("Parcel lays out an int, a string and a long as Android does", "exact", "FrameworkCases$ParcelLayout"),
        def("Uri.parse splits and decodes scheme, authority, path, query and fragment", "exact", "FrameworkCases$UriParts"),
        def("SharedPreferences.commit writes shared_prefs/NAME.xml in Android's XML", "exact", "FrameworkCases$SharedPreferencesFile"),
        def("SQLite sums groups and reports the column types", "exact", "FrameworkCases$SqliteGroupBy"),
        def("org.json keeps insertion order and escapes the slash", "exact", "FrameworkCases$JsonOrder"),
        def("Log.isLoggable is false for DEBUG and true for INFO by default", "exact", "FrameworkCases$LogIsLoggable"),
        def("Double.toString and Float.toString print the runtime's digits", "exact", "FrameworkCases$FloatingToString"),
    };

    private static final Def[] RESOURCES = {
        def("strings with format arguments and aapt2 escapes, and arrays", "exact", "ResourceCases$Strings"),
        def("English plurals through a configuration context", "exact", "ResourceCases$Plurals"),
        def("typed values: a color's type and data, dimensions, integer, bool, fraction", "exact", "ResourceCases$TypedValues"),
        def("a binary XML resource walked with its parser", "exact", "ResourceCases$BinaryXml"),
        def("a layout inflates into its views with their attributes", "exact", "ResourceCases$InflateLayout"),
        def("a raw resource, the assets, and openFd of a compressed asset refused", "exact", "ResourceCases$RawAndAssets"),
    };

    private static final Def[] NET = {
        def("a Java socket echoes over loopback", "exact", "NetCases$JavaTcpLoopback"),
        def("a connection to a closed loopback port is refused in Java", "exact", "NetCases$ConnectRefused"),
        def("a name under .invalid does not resolve in Java or in C", "exact", "NetCases$InvalidName"),
        def("localhost resolves to 127.0.0.1 alone", "exact", "NetCases$Localhost"),
    };

    private static final Def[] JNI = {
        def("GetVersion is JNI 1.6", "exact", "JniCases$Version"),
        def("JNI_OnLoad runs once, RegisterNatives and JNI names both bind", "exact", "JniCases$OnLoadAndBinding"),
        def("FindClass finds an app class from a Java thread, not from an attached one", "exact", "JniCases$FindClassThreads"),
        def("GetStringUTFChars gives a supplementary character and NUL as ART encodes them", "exact", "JniCases$StringUtf"),
        def("exceptions cross JNI both ways with their class and message", "exact", "JniCases$Exceptions"),
        def("array elements copy back, a critical region writes, direct buffers both ways", "exact", "JniCases$ArraysAndBuffers"),
        def("Call*Method passes mixed arguments in variadic and array forms", "exact", "JniCases$Calls"),
    };

    private static final Def[] BIONIC = {
        def("snprintf words a negative NaN -nan and 1.0 in %a 0x1p+0, as bionic does", "exact", "BionicCases$PrintfWords"),
        def("a failed open sets errno, and strerror words errors as bionic does", "exact", "BionicCases$Strerror"),
        def("dlopen and dlsym find libc's strlen, and missing names fail with an error", "exact", "BionicCases$Dl"),
        def("mmap, mprotect, MADV_DONTNEED and munmap behave as on Linux", "exact", "BionicCases$Memory"),
        def("/proc/self shows the package, the library and app_process64", "exact", "BionicCases$ProcSelf"),
        def("a SIGUSR1 handler runs on raise, and a blocked signal waits as pending", "exact", "BionicCases$Signals"),
        def("ro.build.version.sdk is the SDK level Java sees", "exact", "BionicCases$Property"),
        def("the C library's locale is UTF-8 from the start", "exact", "BionicCases$Multibyte"),
    };

    private static final Def[] GLES = {
        def("EGL initializes at 1.4 or newer with ES2 and ES3 pbuffer configs", "exact", "GlesCases$EglInit"),
        def("a GLES 2 clear reads back in both corners", "exact", "GlesCases$ClearRead"),
        def("glClear writes only the channels of the color mask", "exact", "GlesCases$ColorMaskClear"),
        def("a triangle covers the pixels inside it and no others", "exact", "GlesCases$Triangle"),
        def("a 2 by 2 texture sampled nearest lands in its four quadrants", "exact", "GlesCases$TextureNearest"),
        def("half-alpha white over black blends to half grey", "level", "GlesCases$Blend"),
        def("a bad shader fails to compile with a log, a bad enum is GL_INVALID_ENUM once", "exact", "GlesCases$Errors"),
        def("a GLES 3 context renders integer shader math into a texture framebuffer", "exact", "GlesCases$Gl3Framebuffer"),
        def("a GLES 1.1 clear reads back", "exact", "GlesCases$Gl1ClearRead"),
    };

    private static Def[] of(String suite) {
        switch (suite) {
            case "framework": return FRAMEWORK;
            case "resources": return RESOURCES;
            case "net": return NET;
            case "jni": return JNI;
            case "bionic": return BIONIC;
            case "gles": return GLES;
            default: throw new IllegalArgumentException("no Java suite " + suite);
        }
    }

    /** Runs every case of the suite that may run here, each line through the engine. */
    static void run(String suite, Env env) {
        for (Def d : of(suite)) {
            if (!Engine.start(suite, d.name)) continue;
            Outcome o;
            try {
                Case c = (Case) Class.forName(d.className).getDeclaredConstructor().newInstance();
                o = c.run(env);
            } catch (java.lang.reflect.InvocationTargetException t) {
                o = Outcome.threw(t.getCause() != null ? t.getCause() : t);
            } catch (Throwable t) {
                o = Outcome.threw(t);
            }
            if (o.skipped) Engine.skip(suite, d.name, o.detail);
            else Engine.result(suite, d.name, d.tier, o.passed, o.detail);
        }
    }
}
