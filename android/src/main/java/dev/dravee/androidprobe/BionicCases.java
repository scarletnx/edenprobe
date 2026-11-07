// SPDX-License-Identifier: GPL-3.0-or-later
package dev.dravee.androidprobe;

import android.os.Build;

/** bionic as an app's native code calls it: formatting, parsing, errno, threads, time, the loader, memory, signals. */
final class BionicCases {
    private BionicCases() {}

    /** A negative NaN keeps its sign: -nan. */
    static final class Printf implements Case {
        @Override public Outcome run(Env env) {
            NativeLib.require();
            return Outcome.check("-42| 3.14|1.234568e+04|0.0001|0x1p+0|ff|str|c|%|-9000000000|7|abc|+0042|7   |10|inf nan INF -nan",
                NativeLib.printf());
        }
    }

    static final class Scanf implements Case {
        @Override public Outcome run(Env env) {
            NativeLib.require();
            return Outcome.check("4|42|3.500|word|31|17", NativeLib.scanf());
        }
    }

    /** Hex floats, overflow to inf and LONG_MAX with ERANGE (34), "-1" as unsigned, base 0's prefixes. */
    static final class Strtod implements Case {
        @Override public Outcome run(Env env) {
            NativeLib.require();
            return Outcome.check("12.000000 end|inf errno 34|9223372036854775807 errno 34|18446744073709551615|31 15", NativeLib.strtod());
        }
    }

    /** errno of a failed open, and strerror's text: bionic's own table, as the kernel's headers word them. */
    static final class Strerror implements Case {
        @Override public Outcome run(Env env) {
            NativeLib.require();
            return Outcome.check("open -1 errno 2|No such file or directory|Try again|Function not implemented|Unknown error 9999",
                NativeLib.strerror());
        }
    }

    /** Four threads add under a mutex; each key's destructor runs at its thread's end; __thread is per thread. */
    static final class Threads implements Case {
        @Override public Outcome run(Env env) {
            NativeLib.require();
            return Outcome.check("sum 40000|destructors 4|own tls 4", NativeLib.threads());
        }
    }

    static final class Time implements Case {
        @Override public Outcome run(Env env) {
            NativeLib.require();
            return Outcome.check("2023-11-14T22:13:20 Tue Nov 318 2|monotonic advanced 20 ms or more", NativeLib.time());
        }
    }

    static final class Dl implements Case {
        @Override public Outcome run(Env env) {
            NativeLib.require();
            return Outcome.check("libc yes|strlen 5|missing symbol NULL, error yes|missing library NULL, error names it", NativeLib.dl());
        }
    }

    /** sysconf's page size, mmap, mprotect, MADV_DONTNEED reading zeros again, munmap. */
    static final class Memory implements Case {
        @Override public Outcome run(Env env) {
            NativeLib.require();
            return Outcome.check("page size agrees, a power of two from 4096|protect 0|dontneed reads 0|untouched 0xab|unmap 0",
                NativeLib.memory());
        }
    }

    /** The process as Android shows it: cmdline the package, the library in maps, exe app_process64. */
    static final class ProcSelf implements Case {
        @Override public Outcome run(Env env) {
            NativeLib.require();
            String pkg = env.activity.getPackageName();
            return Outcome.check("cmdline " + pkg + "|maps names libandroidprobe.so|exe /system/bin/app_process64", NativeLib.procSelf(pkg));
        }
    }

    /** A handler for SIGUSR1 runs on raise; blocked, the signal waits as pending and runs on the unblock. */
    static final class Signals implements Case {
        @Override public Outcome run(Env env) {
            NativeLib.require();
            return Outcome.check("ran 1 signo 10 code -6|blocked: pending yes, ran 1|unblocked: ran 2", NativeLib.signals());
        }
    }

    static final class Jumps implements Case {
        @Override public Outcome run(Env env) {
            NativeLib.require();
            return Outcome.check("setjmp 0 then 7, stage 1|sigsetjmp 0 then 9", NativeLib.jumps());
        }
    }

    /** libm against the correctly rounded values (tools/math-expected.py): each within 1 ulp. */
    static final class Math implements Case {
        @Override public Outcome run(Env env) {
            NativeLib.require();
            String got = NativeLib.math();
            long worst = 0;
            for (String part : got.split(", ")) {
                String[] w = part.split(" ");
                worst = java.lang.Math.max(worst, java.lang.Math.abs(Long.parseLong(w[w.length - 1])));
            }
            String want = "each within 1 ulp";
            return worst <= 1 ? Outcome.pass(got, want) : Outcome.fail("a function is " + worst + " ulp off: " + got, got, want);
        }
    }

    /** __system_property_get of ro.build.version.sdk is Build.VERSION.SDK_INT. */
    static final class Property implements Case {
        @Override public Outcome run(Env env) {
            NativeLib.require();
            return Outcome.check(String.valueOf(Build.VERSION.SDK_INT), NativeLib.property());
        }
    }

    /** bionic's locale is UTF-8 from the start: mbstowcs counts characters, MB_CUR_MAX is 4. */
    static final class Multibyte implements Case {
        @Override public Outcome run(Env env) {
            NativeLib.require();
            return Outcome.check("mbstowcs 4|MB_CUR_MAX 4|wcslen 4", NativeLib.multibyte());
        }
    }
}
