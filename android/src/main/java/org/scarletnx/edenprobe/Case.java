// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
package org.scarletnx.edenprobe;

/** One behaviour checked. Each is a class of its own, loaded only when it runs, so one the layer cannot load fails alone. */
interface Case {
    Outcome run(Env env) throws Throwable;
}
