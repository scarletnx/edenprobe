// SPDX-License-Identifier: GPL-3.0-or-later
package dev.dravee.androidprobe;

/** One behaviour checked. Each is a class of its own, loaded only when it runs, so one the layer cannot load fails alone. */
interface Case {
    Outcome run(Env env) throws Throwable;
}
