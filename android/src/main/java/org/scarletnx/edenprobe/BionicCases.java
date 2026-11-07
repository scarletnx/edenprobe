// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
package org.scarletnx.edenprobe;

import android.os.Build;

/**
 * bionic where it is bionic's own: what the C standard leaves to the library (its words for errors, a NaN's sign),
 * and the system under it, the loader, memory, /proc, signals, properties. What the standard defines is the libc
 * family's, the same cases on every frontend (source/tests/t_libc.c).
 */
final class BionicCases {
    private BionicCases() {}

    /** A negative NaN keeps its sign, and %a starts a normal number at 1. */
    static final class PrintfWords implements Case {
        @Override public Outcome run(Env env) {
            return Outcome.check("-nan|0x1p+0", NativeLib.printfWords());
        }
    }

    /** errno of a failed open, and strerror's text: bionic's own table, as the kernel's headers word them. */
    static final class Strerror implements Case {
        @Override public Outcome run(Env env) {
            return Outcome.check("open -1 errno 2|No such file or directory|Try again|Function not implemented|Unknown error 9999",
                NativeLib.strerror());
        }
    }

    static final class Dl implements Case {
        @Override public Outcome run(Env env) {
            return Outcome.check("libc yes|strlen 5|missing symbol NULL, error yes|missing library NULL, error names it", NativeLib.dl());
        }
    }

    /** sysconf's page size, mmap, mprotect, MADV_DONTNEED reading zeros again, munmap. */
    static final class Memory implements Case {
        @Override public Outcome run(Env env) {
            return Outcome.check("page size agrees, a power of two from 4096|protect 0|dontneed reads 0|untouched 0xab|unmap 0",
                NativeLib.memory());
        }
    }

    /** The process as Android shows it: cmdline the package, the library in maps, exe app_process64. */
    static final class ProcSelf implements Case {
        @Override public Outcome run(Env env) {
            String pkg = env.activity.getPackageName();
            return Outcome.check("cmdline " + pkg + "|maps names libedenprobe.so|exe /system/bin/app_process64", NativeLib.procSelf(pkg));
        }
    }

    /** A handler for SIGUSR1 runs on raise; blocked, the signal waits as pending and runs on the unblock. */
    static final class Signals implements Case {
        @Override public Outcome run(Env env) {
            return Outcome.check("ran 1 signo 10 code -6|blocked: pending yes, ran 1|unblocked: ran 2", NativeLib.signals());
        }
    }

    /** __system_property_get of ro.build.version.sdk is Build.VERSION.SDK_INT. */
    static final class Property implements Case {
        @Override public Outcome run(Env env) {
            return Outcome.check(String.valueOf(Build.VERSION.SDK_INT), NativeLib.property());
        }
    }

    /** bionic's locale is UTF-8 from the start: mbstowcs counts characters, MB_CUR_MAX is 4. */
    static final class Multibyte implements Case {
        @Override public Outcome run(Env env) {
            return Outcome.check("mbstowcs 4|MB_CUR_MAX 4|wcslen 4", NativeLib.multibyte());
        }
    }
}
