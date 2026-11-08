// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <string.h>

#include "bans.h"

// The table runs are held to: source/bans.txt's, unless a judge's check put its own in place.
static const BanLine *replacement;
static size_t replacement_count;
static bool replaced;

bool BanMachineKnown(const char *name) {
    for (size_t i = 0; name != NULL && i < BAN_MACHINE_COUNT; ++i) {
        if (strcmp(BAN_MACHINES[i], name) == 0) {
            return true;
        }
    }
    return false;
}

const char *BanMachineList(void) {
    static char list[(BAN_MACHINE_MAX + 1u) * 16u];
    if (list[0] == '\0') {
        for (size_t i = 0; i < BAN_MACHINE_COUNT; ++i) {
            const size_t used = strlen(list);
            snprintf(list + used, sizeof(list) - used, "%s%s", i > 0u ? " " : "", BAN_MACHINES[i]);
        }
    }
    return list;
}

static bool VersionCharacter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
           c == '-' || c == '_';
}

/// Whether the @p length bytes at @p text are a version a machine= word may carry.
static bool VersionSpan(const char *text, size_t length) {
    if (length == 0u || length > BAN_VERSION_MAX) {
        return false;
    }
    for (size_t i = 0; i < length; ++i) {
        if (!VersionCharacter(text[i])) {
            return false;
        }
    }
    return true;
}

bool BanVersionValid(const char *version) {
    return version != NULL && VersionSpan(version, strlen(version));
}

/// Reads dotted numbers, a leading v allowed, into @p parts; how many, or 0 when @p text is not that.
static size_t Dotted(const char *text, size_t length, unsigned long parts[8]) {
    size_t i = 0;
    size_t count = 0;
    if (length > 0u && text[0] == 'v') {
        ++i;
    }
    while (i < length && count < 8u) {
        if (text[i] < '0' || text[i] > '9') {
            return 0;
        }
        unsigned long value = 0;
        for (; i < length && text[i] >= '0' && text[i] <= '9'; ++i) {
            value = value * 10u + (unsigned long)(text[i] - '0');
        }
        parts[count++] = value;
        if (i < length) {
            if (text[i] != '.' || i + 1u == length) {
                return 0;
            }
            ++i;
        }
    }
    return i == length ? count : 0;
}

/// -1, 0 or 1 as @p a is below, at or above @p b; missing parts are nought.
static int Compare(const unsigned long *a, size_t na, const unsigned long *b, size_t nb) {
    for (size_t i = 0; i < na || i < nb; ++i) {
        const unsigned long x = i < na ? a[i] : 0u;
        const unsigned long y = i < nb ? b[i] : 0u;
        if (x != y) {
            return x < y ? -1 : 1;
        }
    }
    return 0;
}

typedef struct {
    // "<", "<=", ">=" or ">"
    char op[3];
    unsigned long parts[8];
    size_t count;
} Bound;

static const char *SkipSpaces(const char *p) {
    while (*p == ' ') {
        ++p;
    }
    return p;
}

/// Reads one or two bounds; how many, or 0 when @p versions is not bounds the program reads.
static size_t Bounds(const char *versions, Bound bounds[2]) {
    size_t n = 0;
    const char *p = SkipSpaces(versions);
    while (*p != '\0') {
        if (n == 2u || (*p != '<' && *p != '>')) {
            return 0;
        }
        Bound *const b = &bounds[n];
        b->op[0] = *p++;
        b->op[1] = *p == '=' ? *p++ : '\0';
        b->op[2] = '\0';
        p = SkipSpaces(p);
        const char *const start = p;
        while (*p != '\0' && *p != ' ' && *p != '<' && *p != '>') {
            ++p;
        }
        b->count = Dotted(start, (size_t)(p - start), b->parts);
        if (b->count == 0u) {
            return 0;
        }
        ++n;
        p = SkipSpaces(p);
    }
    // Two are a lower bound then an upper one.
    if (n == 2u && (bounds[0].op[0] != '>' || bounds[1].op[0] != '<')) {
        return 0;
    }
    return n;
}

static bool Inside(const Bound *b, const unsigned long *parts, size_t count) {
    const int c = Compare(parts, count, b->parts, b->count);
    if (b->op[0] == '<') {
        return b->op[1] == '=' ? c <= 0 : c < 0;
    }
    return b->op[1] == '=' ? c >= 0 : c > 0;
}

bool BanVersionsValid(const char *versions) {
    if (versions == NULL || versions[0] == '\0') {
        return false;
    }
    if (strcmp(versions, "*") == 0) {
        return true;
    }
    Bound bounds[2];
    if (versions[0] == '<' || versions[0] == '>') {
        return Bounds(versions, bounds) > 0u;
    }
    // A list: each version between commas, spaces around it allowed.
    const char *p = versions;
    for (;;) {
        p = SkipSpaces(p);
        const char *const start = p;
        while (*p != '\0' && *p != ',' && *p != ' ') {
            ++p;
        }
        if (!VersionSpan(start, (size_t)(p - start))) {
            return false;
        }
        p = SkipSpaces(p);
        if (*p == '\0') {
            return true;
        }
        if (*p++ != ',') {
            return false;
        }
    }
}

bool BanVersionsHold(const char *versions, const char *version) {
    if (!BanVersionsValid(versions)) {
        return false;
    }
    if (strcmp(versions, "*") == 0) {
        return true;
    }
    if (version == NULL || version[0] == '\0') {
        return false;
    }
    const size_t length = strlen(version);
    if (versions[0] == '<' || versions[0] == '>') {
        Bound bounds[2];
        const size_t n = Bounds(versions, bounds);
        unsigned long parts[8];
        const size_t count = Dotted(version, length, parts);
        bool inside = count > 0u;
        for (size_t i = 0; i < n && inside; ++i) {
            inside = Inside(&bounds[i], parts, count);
        }
        return inside;
    }
    for (const char *p = versions; *p != '\0';) {
        p = SkipSpaces(p);
        const char *const start = p;
        while (*p != '\0' && *p != ',' && *p != ' ') {
            ++p;
        }
        if ((size_t)(p - start) == length && strncmp(start, version, length) == 0) {
            return true;
        }
        p = SkipSpaces(p);
        if (*p == ',') {
            ++p;
        }
    }
    return false;
}

size_t BanCount(void) {
    return replaced ? replacement_count : BAN_LINE_COUNT;
}

const BanLine *BanAt(size_t index) {
    if (index >= BanCount()) {
        return NULL;
    }
    return replaced ? &replacement[index] : &BAN_LINES[index];
}

const BanLine *BanFind(const char *suite, const char *key, const char *machine, const char *version) {
    if (suite == NULL || key == NULL || machine == NULL || machine[0] == '\0') {
        return NULL;
    }
    for (size_t i = 0; i < BanCount(); ++i) {
        const BanLine *const ban = BanAt(i);
        if (strcmp(ban->machine, machine) == 0 && strcmp(ban->suite, suite) == 0 &&
            strcmp(ban->name, key) == 0 && BanVersionsHold(ban->versions, version)) {
            return ban;
        }
    }
    return NULL;
}

size_t BanCountFor(const char *machine, const char *version, size_t *holding) {
    size_t named = 0;
    size_t held = 0;
    for (size_t i = 0; machine != NULL && machine[0] != '\0' && i < BanCount(); ++i) {
        const BanLine *const ban = BanAt(i);
        if (strcmp(ban->machine, machine) == 0) {
            ++named;
            held += BanVersionsHold(ban->versions, version) ? 1u : 0u;
        }
    }
    if (holding != NULL) {
        *holding = held;
    }
    return named;
}

size_t BanDescribe(char *out, size_t out_size, const char *machine, const char *version, bool unban) {
    if (out == NULL || out_size == 0u) {
        return 0;
    }
    out[0] = '\0';
    const bool named = machine != NULL && machine[0] != '\0';
    const bool versioned = version != NULL && version[0] != '\0';
    if (!named) {
        if (unban) {
            snprintf(out, out_size,
                     "the word unban, with no machine= word: no line of source/bans.txt holds, so it "
                     "runs nothing more");
        }
        return strlen(out);
    }
    char who[BAN_MACHINE_MAX + BAN_VERSION_MAX + 24u];
    snprintf(who, sizeof(who), "%s%s%s", machine, versioned ? " at " : ", no version given",
             versioned ? version : "");
    size_t holding = 0;
    const size_t lines = BanCountFor(machine, version, &holding);
    if (lines == 0u) {
        snprintf(out, out_size, "no line of source/bans.txt names %s, so every case runs", machine);
    } else if (holding == 0u) {
        snprintf(out, out_size,
                 "none of the %zu line%s of source/bans.txt that name %s holds for %s, so every "
                 "case runs",
                 lines, lines == 1u ? "" : "s", machine, who);
    } else {
        snprintf(out, out_size, "%zu of the %zu line%s of source/bans.txt hold for %s%s", holding,
                 BanCount(), BanCount() == 1u ? "" : "s", who,
                 unban ? ", and the word unban runs their cases"
                       : ": their cases are skipped as banned, and the mode word unban runs them");
        if (lines > holding) {
            const size_t used = strlen(out);
            snprintf(out + used, out_size - used, "; %zu more name %s and hold only at other versions",
                     lines - holding, machine);
        }
    }
    return strlen(out);
}

void BanUseTable(const BanLine *lines, size_t count) {
    replaced = lines != NULL;
    replacement = lines;
    replacement_count = lines != NULL ? count : 0u;
}
