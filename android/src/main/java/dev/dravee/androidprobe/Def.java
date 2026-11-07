// SPDX-License-Identifier: GPL-3.0-or-later
package dev.dravee.androidprobe;

/**
 * A case as the report names it. id is the case's key for scripts and never changes; suite and name are the key that
 * edenprobe-diff.py and edenprobe-table.py compare by. tier says how a value is compared, as edenprobe's does: exact,
 * ulp (units in the last place), level (a pixel channel within a step), none (a behaviour, no value).
 */
final class Def {
    final String id, suite, name, tier, className;

    Def(String id, String suite, String name, String tier, String className) {
        this.id = id;
        this.suite = suite;
        this.name = name;
        this.tier = tier;
        this.className = className;
    }
}
