// SPDX-License-Identifier: GPL-3.0-or-later
package dev.dravee.androidprobe;

import java.util.Arrays;
import java.util.List;

/**
 * Every case, in the order a run takes them: the Java suites first, so a native library that does not load or a
 * native case that ends the process leaves them reported. An id, a suite and a name never change once a report has
 * them: a new behaviour is a new case.
 */
final class Cases {
    private Cases() {}

    private static Def def(String id, String suite, String name, String tier, String cls) {
        return new Def(id, suite, name, tier, "dev.dravee.androidprobe." + cls);
    }

    static final List<Def> ALL = Arrays.asList(
        def("framework.link-properties-dhcp", "framework", "LinkProperties.getDhcpServerAddress is the SDK's Inet4Address method, null on a new object", "exact", "FrameworkCases$LinkPropertiesDhcp"),
        def("framework.media-player-missing-file", "framework", "MediaPlayer.setDataSource of a missing file throws FileNotFoundException", "exact", "FrameworkCases$MediaPlayerMissingFile"),
        def("framework.pdf-document-draws", "framework", "PdfDocument.writeTo writes what the page's canvas drew", "exact", "FrameworkCases$PdfDocumentDraws"),
        def("framework.atomic-file-finish", "framework", "AtomicFile.finishWrite leaves the last write in the base file alone", "exact", "FrameworkCases$AtomicFileFinish"),
        def("framework.atomic-file-legacy-backup", "framework", "AtomicFile reads a leftover .bak as the last whole write", "exact", "FrameworkCases$AtomicFileLegacyBackup"),
        def("framework.popup-window-shows", "framework", "a PopupWindow shown at a location attaches its content at its size", "exact", "FrameworkCases$PopupWindowShows"),
        def("framework.handler-order", "framework", "a Handler runs the front of the queue, then posted, then delayed messages", "exact", "FrameworkCases$HandlerOrder"),
        def("framework.parcel-layout", "framework", "Parcel lays out an int, a string and a long as Android does", "exact", "FrameworkCases$ParcelLayout"),
        def("framework.uri-parts", "framework", "Uri.parse splits and decodes scheme, authority, path, query and fragment", "exact", "FrameworkCases$UriParts"),
        def("framework.shared-preferences-file", "framework", "SharedPreferences.commit writes shared_prefs/NAME.xml in Android's XML", "exact", "FrameworkCases$SharedPreferencesFile"),
        def("framework.sqlite-group-by", "framework", "SQLite sums groups and reports the column types", "exact", "FrameworkCases$SqliteGroupBy"),
        def("framework.json-order", "framework", "org.json keeps insertion order and escapes the slash", "exact", "FrameworkCases$JsonOrder"),
        def("framework.log-is-loggable", "framework", "Log.isLoggable is false for DEBUG and true for INFO by default", "exact", "FrameworkCases$LogIsLoggable"),
        def("framework.floating-to-string", "framework", "Double.toString and Float.toString print the runtime's digits", "exact", "FrameworkCases$FloatingToString"),
        def("resources.strings", "resources", "strings with format arguments and aapt2 escapes, and arrays", "exact", "ResourceCases$Strings"),
        def("resources.plurals", "resources", "English plurals through a configuration context", "exact", "ResourceCases$Plurals"),
        def("resources.typed-values", "resources", "typed values: a color's type and data, dimensions, integer, bool, fraction", "exact", "ResourceCases$TypedValues"),
        def("resources.binary-xml", "resources", "a binary XML resource walked with its parser", "exact", "ResourceCases$BinaryXml"),
        def("resources.inflate-layout", "resources", "a layout inflates into its views with their attributes", "exact", "ResourceCases$InflateLayout"),
        def("resources.raw-and-assets", "resources", "a raw resource, the assets, and openFd of a compressed asset refused", "exact", "ResourceCases$RawAndAssets"),
        def("net.java-tcp-loopback", "net", "a Java socket echoes over loopback", "exact", "NetCases$JavaTcpLoopback"),
        def("net.native-tcp-loopback", "net", "a native socket listens, connects and echoes over loopback", "exact", "NetCases$NativeTcpLoopback"),
        def("net.connect-refused", "net", "a connection to a closed loopback port is refused in Java and in C", "exact", "NetCases$ConnectRefused"),
        def("net.invalid-name", "net", "a name under .invalid does not resolve in Java or in C", "exact", "NetCases$InvalidName"),
        def("net.localhost", "net", "localhost resolves to 127.0.0.1 alone", "exact", "NetCases$Localhost"),
        def("jni.version", "jni", "GetVersion is JNI 1.6", "exact", "JniCases$Version"),
        def("jni.onload-and-binding", "jni", "JNI_OnLoad runs once, RegisterNatives and JNI names both bind", "exact", "JniCases$OnLoadAndBinding"),
        def("jni.find-class-threads", "jni", "FindClass finds an app class from a Java thread, not from an attached one", "exact", "JniCases$FindClassThreads"),
        def("jni.string-utf", "jni", "GetStringUTFChars gives a supplementary character and NUL as ART encodes them", "exact", "JniCases$StringUtf"),
        def("jni.exceptions", "jni", "exceptions cross JNI both ways with their class and message", "exact", "JniCases$Exceptions"),
        def("jni.arrays-and-buffers", "jni", "array elements copy back, a critical region writes, direct buffers both ways", "exact", "JniCases$ArraysAndBuffers"),
        def("jni.calls", "jni", "Call*Method passes mixed arguments in variadic and array forms", "exact", "JniCases$Calls"),
        def("bionic.printf", "bionic", "snprintf formats integers, floats, hex floats, strings and infinities", "exact", "BionicCases$Printf"),
        def("bionic.scanf", "bionic", "sscanf reads an int, a double, a word, hex and the count", "exact", "BionicCases$Scanf"),
        def("bionic.strtod", "bionic", "strtod and strtol parse hex, overflow with ERANGE and take base 0's prefixes", "exact", "BionicCases$Strtod"),
        def("bionic.strerror", "bionic", "a failed open sets errno, and strerror words errors as bionic does", "exact", "BionicCases$Strerror"),
        def("bionic.threads", "bionic", "threads add under a mutex, key destructors run, __thread is per thread", "exact", "BionicCases$Threads"),
        def("bionic.time", "bionic", "gmtime_r and strftime of a fixed time, and the monotonic clock over a sleep", "exact", "BionicCases$Time"),
        def("bionic.dl", "bionic", "dlopen and dlsym find libc's strlen, and missing names fail with an error", "exact", "BionicCases$Dl"),
        def("bionic.memory", "bionic", "mmap, mprotect, MADV_DONTNEED and munmap behave as on Linux", "exact", "BionicCases$Memory"),
        def("bionic.proc-self", "bionic", "/proc/self shows the package, the library and app_process64", "exact", "BionicCases$ProcSelf"),
        def("bionic.signals", "bionic", "a SIGUSR1 handler runs on raise, and a blocked signal waits as pending", "exact", "BionicCases$Signals"),
        def("bionic.jumps", "bionic", "setjmp and sigsetjmp return twice with the value longjmp gave", "exact", "BionicCases$Jumps"),
        def("bionic.math", "bionic", "libm is within 1 ulp of the correctly rounded results", "ulp", "BionicCases$Math"),
        def("bionic.property", "bionic", "ro.build.version.sdk is the SDK level Java sees", "exact", "BionicCases$Property"),
        def("bionic.multibyte", "bionic", "the C library's locale is UTF-8 from the start", "exact", "BionicCases$Multibyte"),
        def("gles.egl-init", "gles", "EGL initializes at 1.4 or newer with ES2 and ES3 pbuffer configs", "exact", "GlesCases$EglInit"),
        def("gles.clear-read", "gles", "a GLES 2 clear reads back in both corners", "exact", "GlesCases$ClearRead"),
        def("gles.color-mask-clear", "gles", "glClear writes only the channels of the color mask", "exact", "GlesCases$ColorMaskClear"),
        def("gles.triangle", "gles", "a triangle covers the pixels inside it and no others", "exact", "GlesCases$Triangle"),
        def("gles.texture-nearest", "gles", "a 2 by 2 texture sampled nearest lands in its four quadrants", "exact", "GlesCases$TextureNearest"),
        def("gles.blend", "gles", "half-alpha white over black blends to half grey", "level", "GlesCases$Blend"),
        def("gles.errors", "gles", "a bad shader fails to compile with a log, a bad enum is GL_INVALID_ENUM once", "exact", "GlesCases$Errors"),
        def("gles.gl3-framebuffer", "gles", "a GLES 3 context renders integer shader math into a texture framebuffer", "exact", "GlesCases$Gl3Framebuffer"),
        def("gles.gl1-clear-read", "gles", "a GLES 1.1 clear reads back", "exact", "GlesCases$Gl1ClearRead"));

    static Def find(String id) {
        for (Def d : ALL)
            if (d.id.equals(id)) return d;
        return null;
    }
}
