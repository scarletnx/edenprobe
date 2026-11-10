// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <string.h>

#include "bans.h"
#include "mode.h"
#include "tex8kref.h"

// Longer than any word this file knows. A longer one is unknown whatever it spells, and is
// quoted cut short rather than in full.
#define WORD_MAX 24u

static const struct {
    const char *word;
    uint32_t family;
} FAMILIES[] = {
    { "cpu", MODE_FAMILY_CPU },
    { "pad", MODE_FAMILY_PAD },
    { "graphics", MODE_FAMILY_GRAPHICS },
    { "fs", MODE_FAMILY_FS },
    { "kernel", MODE_FAMILY_KERNEL },
    { "display", MODE_FAMILY_DISPLAY },
    { "video", MODE_FAMILY_VIDEO },
    { "audio", MODE_FAMILY_AUDIO },
    { "service", MODE_FAMILY_SERVICE },
    { "net", MODE_FAMILY_NET },
    { "game", MODE_FAMILY_GAME },
    { "nvn", MODE_FAMILY_NVN },
    { "libc", MODE_FAMILY_LIBC },
    { "jni", MODE_FAMILY_JNI },
    { "framework", MODE_FAMILY_FRAMEWORK },
};

static bool IsLetter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/// Adds one word to the list of those not understood, whole or not at all, and closes the list
/// with "..." the first time one does not fit.
static void NoteUnknown(RunMode *mode, const char *word) {
    ++mode->unknown_count;
    const size_t used = strlen(mode->unknown);
    if (used >= 3u && strcmp(mode->unknown + used - 3u, "...") == 0) {
        return;
    }
    const size_t need = (used > 0u ? 1u : 0u) + strlen(word);
    if (used + need + 4u < sizeof(mode->unknown)) {
        snprintf(mode->unknown + used, sizeof(mode->unknown) - used, "%s%s",
                 used > 0u ? " " : "", word);
    } else {
        snprintf(mode->unknown + used, sizeof(mode->unknown) - used, "%s...",
                 used > 0u ? " " : "");
    }
}

static void Take(RunMode *mode, const char *word, bool cut) {
    if (!cut) {
        if (strcmp(word, "quick") == 0) {
            mode->quick = true;
            return;
        }
        if (strcmp(word, "detailed") == 0) {
            mode->detailed = true;
            return;
        }
        if (strcmp(word, "images") == 0) {
            mode->every_case = true;
            return;
        }
        if (strcmp(word, "notime") == 0) {
            mode->no_timing = true;
            return;
        }
        if (strcmp(word, "stress") == 0) {
            mode->stress = true;
            return;
        }
        if (strcmp(word, "fatal") == 0) {
            mode->fatal = true;
            return;
        }
        if (strcmp(word, "retry") == 0) {
            mode->retry = true;
            return;
        }
        if (strcmp(word, "hang") == 0) {
            mode->hang = true;
            return;
        }
        if (strcmp(word, "unban") == 0) {
            mode->unban = true;
            return;
        }
        if (strcmp(word, "nojournal") == 0) {
            mode->no_journal = true;
            return;
        }
        if (strcmp(word, "noprogress") == 0) {
            mode->no_progress = true;
            return;
        }
        if (strcmp(word, "listen") == 0) {
            mode->listen = true;
            return;
        }
        if (strcmp(word, "all") == 0) {
            mode->all = true;
            return;
        }
        for (size_t i = 0; i < sizeof(FAMILIES) / sizeof(FAMILIES[0]); ++i) {
            if (strcmp(word, FAMILIES[i].word) == 0) {
                mode->families_named |= FAMILIES[i].family;
                return;
            }
        }
    }
    NoteUnknown(mode, word);
}

/// What ends a label's name: a space, a tab, a line end, or the end of the text.
static bool EndsValue(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\0';
}

static bool LabelCharacter(char c) {
    return IsLetter(c) || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
}

bool ModeLabelValid(const char *label) {
    if (label == NULL || label[0] == '\0' || label[0] == '.') {
        return false;
    }
    size_t n = 0;
    for (; label[n] != '\0'; ++n) {
        if (n >= MODE_LABEL_MAX || !LabelCharacter(label[n])) {
            return false;
        }
    }
    return true;
}

/// Takes the name after a word's sign, @p length bytes of it, into @p slot: kept when a file can
/// be named after it, quoted in the mode line otherwise.
static void TakeName(ModeName *slot, const char *value, size_t length) {
    char name[MODE_LABEL_MAX + 2u];
    const size_t kept = length < sizeof(name) - 1u ? length : sizeof(name) - 1u;
    memcpy(name, value, kept);
    name[kept] = '\0';
    if (length <= MODE_LABEL_MAX && ModeLabelValid(name)) {
        memcpy(slot->name, name, kept + 1u);
        slot->was_refused = false;
        slot->refused[0] = '\0';
        slot->refused_cut = false;
        return;
    }
    slot->was_refused = true;
    // Quoted as written, as much as fits; anything a line could not carry as itself is a '?'.
    const size_t quoted = length < MODE_LABEL_MAX ? length : MODE_LABEL_MAX;
    for (size_t i = 0; i < quoted; ++i) {
        const unsigned char c = (unsigned char)value[i];
        slot->refused[i] = c < 0x20u || c >= 0x7Fu || c == '"' || c == '\\' ? '?' : (char)c;
    }
    slot->refused[quoted] = '\0';
    slot->refused_cut = length > MODE_LABEL_MAX;
}

/// Takes the name after `label=`, which the label's own fields keep.
static void TakeLabel(RunMode *mode, const char *value, size_t length) {
    ModeName slot;
    memcpy(slot.name, mode->label, sizeof(slot.name));
    TakeName(&slot, value, length);
    memcpy(mode->label, slot.name, sizeof(mode->label));
    if (slot.was_refused) {
        mode->label_was_refused = true;
        memcpy(mode->label_refused, slot.refused, sizeof(mode->label_refused));
        mode->label_refused_cut = slot.refused_cut;
    } else {
        mode->label_was_refused = false;
        mode->label_refused[0] = '\0';
        mode->label_refused_cut = false;
    }
}

/// Takes the number after `frames=`, @p length bytes of it: kept when it is a whole number from 1
/// to MODE_FRAMES_MAX, quoted in the mode line otherwise.
static void TakeFrames(RunMode *mode, const char *value, size_t length) {
    uint32_t number = 0;
    bool whole = length > 0u;
    for (size_t i = 0; i < length && whole; ++i) {
        const char c = value[i];
        // Checked before each digit is added, so no length of digits can wrap round to a
        // number that looks good.
        whole = c >= '0' && c <= '9' && number <= MODE_FRAMES_MAX;
        number = whole ? number * 10u + (uint32_t)(c - '0') : number;
    }
    if (whole && number >= 1u && number <= MODE_FRAMES_MAX) {
        mode->frames = number;
        mode->frames_named = true;
        mode->frames_was_refused = false;
        mode->frames_refused[0] = '\0';
        mode->frames_refused_cut = false;
        return;
    }
    mode->frames_was_refused = true;
    const size_t quoted = length < MODE_FRAMES_QUOTED ? length : MODE_FRAMES_QUOTED;
    for (size_t i = 0; i < quoted; ++i) {
        const unsigned char c = (unsigned char)value[i];
        mode->frames_refused[i] = c < 0x20u || c >= 0x7Fu || c == '"' || c == '\\' ? '?' : (char)c;
    }
    mode->frames_refused[quoted] = '\0';
    mode->frames_refused_cut = length > MODE_FRAMES_QUOTED;
}

/// Takes the number after `vram=`, @p length bytes of it: kept when it is a whole number of MiB
/// from 1 to MODE_VRAM_MAX, quoted in the mode line otherwise.
static void TakeVram(RunMode *mode, const char *value, size_t length) {
    uint32_t number = 0;
    bool whole = length > 0u;
    for (size_t i = 0; i < length && whole; ++i) {
        const char c = value[i];
        // Checked before each digit is added, so no length of digits can wrap round to a
        // number that looks good.
        whole = c >= '0' && c <= '9' && number <= MODE_VRAM_MAX;
        number = whole ? number * 10u + (uint32_t)(c - '0') : number;
    }
    if (whole && number >= 1u && number <= MODE_VRAM_MAX) {
        mode->vram = number;
        mode->vram_was_refused = false;
        mode->vram_refused[0] = '\0';
        mode->vram_refused_cut = false;
        return;
    }
    mode->vram_was_refused = true;
    const size_t quoted = length < MODE_VRAM_QUOTED ? length : MODE_VRAM_QUOTED;
    for (size_t i = 0; i < quoted; ++i) {
        const unsigned char c = (unsigned char)value[i];
        mode->vram_refused[i] = c < 0x20u || c >= 0x7Fu || c == '"' || c == '\\' ? '?' : (char)c;
    }
    mode->vram_refused[quoted] = '\0';
    mode->vram_refused_cut = length > MODE_VRAM_QUOTED;
}

/// Takes the machine after `machine=`, @p length bytes of it, `<name>` or `<name>@<version>`: kept
/// when the ban list knows the name and the version is one a machine can have, quoted in the mode
/// line otherwise, an earlier good one left in effect.
static void TakeMachine(RunMode *mode, const char *value, size_t length) {
    size_t at = 0;
    while (at < length && value[at] != '@') {
        ++at;
    }
    char name[MODE_MACHINE_MAX + 1u] = "";
    char version[MODE_MACHINE_VERSION_MAX + 1u] = "";
    const size_t version_length = at < length ? length - at - 1u : 0u;
    bool known = at > 0u && at <= MODE_MACHINE_MAX;
    if (known) {
        memcpy(name, value, at);
        name[at] = '\0';
        known = BanMachineKnown(name);
    }
    bool versioned = at == length;
    if (!versioned && version_length <= MODE_MACHINE_VERSION_MAX) {
        memcpy(version, value + at + 1u, version_length);
        version[version_length] = '\0';
        versioned = BanVersionValid(version);
    }
    if (known && versioned) {
        memcpy(mode->machine, name, sizeof(mode->machine));
        memcpy(mode->machine_version, version, sizeof(mode->machine_version));
        mode->machine_was_refused = false;
        mode->machine_refused[0] = '\0';
        mode->machine_refused_cut = false;
        mode->machine_refused_version = false;
        return;
    }
    mode->machine_was_refused = true;
    mode->machine_refused_version = known;
    const size_t quoted = length < MODE_MACHINE_QUOTED ? length : MODE_MACHINE_QUOTED;
    for (size_t i = 0; i < quoted; ++i) {
        const unsigned char c = (unsigned char)value[i];
        mode->machine_refused[i] = c < 0x20u || c >= 0x7Fu || c == '"' || c == '\\' ? '?' : (char)c;
    }
    mode->machine_refused[quoted] = '\0';
    mode->machine_refused_cut = length > MODE_MACHINE_QUOTED;
}

/// Takes the suite after `skip=`, @p length bytes of it: kept, lower case and in the order of the
/// names, when a suite can be called that and there is room, quoted in the mode line otherwise.
static void TakeSkip(RunMode *mode, const char *value, size_t length) {
    char name[MODE_SKIP_NAME_MAX + 1u];
    bool good = length >= 1u && length <= MODE_SKIP_NAME_MAX && IsLetter(value[0]);
    for (size_t i = 0; good && i < length; ++i) {
        const char c = value[i];
        good = IsLetter(c) || (c >= '0' && c <= '9') || c == '-';
        name[i] = (char)(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    }
    if (good) {
        name[length] = '\0';
        size_t at = 0;
        while (at < mode->skip_count && strcmp(mode->skip[at], name) < 0) {
            ++at;
        }
        if (at < mode->skip_count && strcmp(mode->skip[at], name) == 0) {
            return;
        }
        if (mode->skip_count < MODE_SKIPS_MAX) {
            for (size_t i = mode->skip_count; i > at; --i) {
                memcpy(mode->skip[i], mode->skip[i - 1u], sizeof(mode->skip[i]));
            }
            memcpy(mode->skip[at], name, length + 1u);
            ++mode->skip_count;
            return;
        }
    }
    mode->skip_was_refused = true;
    mode->skip_refused_full = good;
    const size_t quoted = length < MODE_SKIP_NAME_MAX ? length : MODE_SKIP_NAME_MAX;
    for (size_t i = 0; i < quoted; ++i) {
        const unsigned char c = (unsigned char)value[i];
        mode->skip_refused[i] = c < 0x20u || c >= 0x7Fu || c == '"' || c == '\\' ? '?' : (char)c;
    }
    mode->skip_refused[quoted] = '\0';
    mode->skip_refused_cut = length > MODE_SKIP_NAME_MAX;
}

/// The words a value follows (a name, the number `frames=` takes, a machine, a suite), and the
/// length of each without its sign.
static const struct {
    const char *word;
    size_t length;
} NAMED[] = {
    { "label", 5u },
    { "baseline", 8u },
    { "compare", 7u },
    { "frames", 6u },
    { "machine", 7u },
    { "skip", 4u },
    { "vram", 4u },
};

/// Which of NAMED the @p length letters of @p word are, or -1.
static int NamedWord(const char *word, size_t length) {
    for (size_t i = 0; i < sizeof(NAMED) / sizeof(NAMED[0]); ++i) {
        if (length == NAMED[i].length && memcmp(word, NAMED[i].word, length) == 0) {
            return (int)i;
        }
    }
    return -1;
}

/// The value after the sign of NAMED[@p which], @p length bytes of @p value.
static void TakeNamed(RunMode *mode, int which, const char *value, size_t length) {
    if (which == 0) {
        TakeLabel(mode, value, length);
    } else if (which == 3) {
        TakeFrames(mode, value, length);
    } else if (which == 4) {
        TakeMachine(mode, value, length);
    } else if (which == 5) {
        TakeSkip(mode, value, length);
    } else if (which == 6) {
        TakeVram(mode, value, length);
    } else {
        TakeName(which == 1 ? &mode->baseline : &mode->compare, value, length);
    }
}

/// The words in the first @p size bytes of @p text, or up to a terminator if one comes first.
static void ReadWords(RunMode *mode, const char *text, size_t size) {
    char word[WORD_MAX + 1u];
    size_t length = 0;
    bool cut = false;
    for (size_t i = 0;; ++i) {
        const char c = i < size ? text[i] : '\0';
        if (IsLetter(c)) {
            if (length < WORD_MAX) {
                word[length++] = (char)(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
            } else {
                cut = true;
            }
            continue;
        }
        const int named = c == '=' && !cut ? NamedWord(word, length) : -1;
        if (named >= 0) {
            // A name or a number rather than words: it runs to the next space or line end,
            // whatever it holds.
            size_t end = i + 1u;
            while (end < size && !EndsValue(text[end])) {
                ++end;
            }
            TakeNamed(mode, named, text + i + 1u, end - i - 1u);
            length = 0;
            i = end - 1u;
            continue;
        }
        if (length > 0u) {
            word[length] = '\0';
            Take(mode, word, cut);
        }
        length = 0;
        cut = false;
        if (c == '\0') {
            break;
        }
    }
}

/// Where the last space-separated piece of the first @p length bytes starts.
static size_t LastPiece(const char *text, size_t length) {
    while (length > 0u && !EndsValue(text[length - 1u])) {
        --length;
    }
    return length;
}

/// The length of the word and sign @p text starts with when it is one of NAMED's, or 0. The word
/// is in the @p length bytes read, and the sign may be the byte past them, which a file longer than
/// the limit has: the limit falling just before the sign cuts the word from its name as surely as
/// one falling inside the name.
static size_t StartsWithNamed(const char *text, size_t length) {
    for (size_t n = 0; n < sizeof(NAMED) / sizeof(NAMED[0]); ++n) {
        const size_t sign = NAMED[n].length;
        if (length < sign || text[sign] != '=') {
            continue;
        }
        size_t i = 0;
        for (; i < sign; ++i) {
            const char c = text[i];
            if ((c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c) != NAMED[n].word[i]) {
                break;
            }
        }
        if (i == sign) {
            return sign + 1u;
        }
    }
    return 0;
}

/// Detailed unless the file asked for the quick run: the default is for a person, and a script
/// is the one that knows to write a word first.
static void Decide(RunMode *mode) {
    mode->detailed = mode->detailed || !mode->quick;
}

RunMode ModeFromText(const char *text) {
    return ModeFromFile(text != NULL ? MODE_SOURCE_FILE : MODE_SOURCE_NONE, text,
                        text != NULL ? strlen(text) : 0u, false);
}

RunMode ModeFromFile(ModeSource source, const char *text, size_t length, bool longer) {
    RunMode mode;
    memset(&mode, 0, sizeof(mode));
    mode.source = source;
    mode.frames = MODE_FRAMES_DEFAULT;
    if (source == MODE_SOURCE_FILE && text != NULL) {
        mode.longer = longer;
        const size_t piece = LastPiece(text, length);
        if (longer && !EndsValue(text[length]) &&
            StartsWithNamed(text + piece, length - piece) > 0u) {
            // A value runs on past the limit: what was read of a name names another folder or
            // file, and of a number another number.
            length = piece;
        } else if (longer && IsLetter(text[length])) {
            // The last word runs on past the limit, so what was read of it is a fragment.
            while (length > 0u && IsLetter(text[length - 1u])) {
                --length;
            }
        }
        ReadWords(&mode, text, length);
    }
    Decide(&mode);
    return mode;
}

RunMode ModeWithButton(RunMode mode, bool detailed_button_held) {
    if (detailed_button_held && !mode.detailed) {
        mode.detailed = true;
        mode.detailed_by_button = true;
    }
    return mode;
}

RunMode ModeAtLaunch(const char *text, bool detailed_button_held) {
    return ModeWithButton(ModeFromText(text), detailed_button_held);
}

bool ModeAsksPad(const RunMode *mode) {
    return mode != NULL && !mode->detailed;
}

uint32_t ModeFamiliesNamed(const RunMode *mode, uint32_t present) {
    if (mode == NULL) {
        return 0u;
    }
    const uint32_t all = mode->all ? present & ~MODE_FAMILIES_NOT_ALL : 0u;
    return (mode->families_named | all) & present;
}

uint32_t ModeFamiliesToRunOf(const RunMode *mode, uint32_t present, uint32_t asked) {
    if (mode == NULL) {
        return present & ~asked;
    }
    const uint32_t listened = mode->listen ? MODE_FAMILY_AUDIO : 0u;
    // The default only when no word names a family: one this build lacks still names one, and
    // the run is then the named families it has, which may be none.
    if (mode->families_named == 0u && !mode->all) {
        return (present & ~asked) | (listened & present);
    }
    return (ModeFamiliesNamed(mode, present) | listened) & present;
}

uint32_t ModeFamiliesToRun(const RunMode *mode, uint32_t present) {
    return ModeFamiliesToRunOf(mode, present, MODE_FAMILIES_ASKED);
}

/// Appends the names of the families in @p set, space separated, or "none".
static void AppendFamilies(char *out, size_t out_size, uint32_t set) {
    bool any = false;
    for (size_t i = 0; i < sizeof(FAMILIES) / sizeof(FAMILIES[0]); ++i) {
        if ((set & FAMILIES[i].family) == 0u) {
            continue;
        }
        const size_t used = strlen(out);
        snprintf(out + used, out_size - used, "%s%s", any ? " " : "", FAMILIES[i].word);
        any = true;
    }
    if (!any) {
        const size_t used = strlen(out);
        snprintf(out + used, out_size - used, "none");
    }
}

/// The part of the mode line a `<word>=<name>` adds: the name and what it does, then a refused
/// one as written.
static void DescribeName(char *out, size_t out_size, const char *word, const ModeName *slot,
                         const char *what) {
    if (slot->name[0] != '\0') {
        const size_t used = strlen(out);
        snprintf(out + used, out_size - used, "; %s %s, %s", word, slot->name, what);
    }
    if (slot->was_refused) {
        const size_t used = strlen(out);
        snprintf(out + used, out_size - used,
                 "; %s refused: \"%s%s\", a file name takes 1 to %u letters, digits, dots, "
                 "dashes and underscores, the first not a dot",
                 word, slot->refused, slot->refused_cut ? "..." : "", (unsigned)MODE_LABEL_MAX);
    }
}

/// The suites `skip=` leaves out, space separated, or "none".
static void AppendSkips(char *out, size_t out_size, const RunMode *mode) {
    for (uint32_t i = 0; i < mode->skip_count; ++i) {
        const size_t used = strlen(out);
        snprintf(out + used, out_size - used, "%s%s", i > 0u ? " " : "", mode->skip[i]);
    }
    if (mode->skip_count == 0u) {
        const size_t used = strlen(out);
        snprintf(out + used, out_size - used, "none");
    }
}

/// The part of the mode line `skip=` adds: the suites left out, then a refused one as written
/// and the suites in effect after it.
static void DescribeSkips(char *out, size_t out_size, const RunMode *mode) {
    if (mode->skip_count > 0u) {
        const size_t used = strlen(out);
        snprintf(out + used, out_size - used, "; suites left out by skip=: ");
        AppendSkips(out, out_size, mode);
    }
    if (!mode->skip_was_refused) {
        return;
    }
    size_t used = strlen(out);
    if (mode->skip_refused_full) {
        snprintf(out + used, out_size - used,
                 "; skip=%s is one past the %u suites skip= takes: the run leaves out ",
                 mode->skip_refused, (unsigned)MODE_SKIPS_MAX);
    } else {
        snprintf(out + used, out_size - used,
                 "; skip=%s%s is not a suite name, 1 to %u letters, digits and dashes, the first "
                 "a letter: the run leaves out ",
                 mode->skip_refused, mode->skip_refused_cut ? "..." : "",
                 (unsigned)MODE_SKIP_NAME_MAX);
    }
    AppendSkips(out, out_size, mode);
}

/// ModeDescribe's line, whole when @p out_size is large enough.
static void Describe(char *out, size_t out_size, const RunMode *mode, uint32_t present,
                     uint32_t asked) {
    const char *why;
    if (mode->chosen_on_screen) {
        why = mode->detailed ? "detailed, chosen on the start screen"
                             : "quick, chosen on the start screen";
    } else if (!mode->detailed) {
        why = "quick, from the mode file";
    } else if (mode->detailed_by_button) {
        why = "detailed, X held at launch";
    } else if (mode->source == MODE_SOURCE_DIRECTORY) {
        why = "detailed, mode is a directory, read as no file";
    } else if (mode->source == MODE_SOURCE_NONE) {
        why = "detailed, no mode file";
    } else if (mode->quick) {
        why = "detailed, the word detailed overrides quick";
    } else {
        why = "detailed, the mode file has no quick";
    }
    snprintf(out, out_size, "mode: %s; pictures of %s; families ", why,
             mode->every_case ? "every drawing case" : "failures only");
    AppendFamilies(out, out_size, ModeFamiliesToRunOf(mode, present, asked));
    // Only when asked for, so a run that times its cases describes its mode as it always did.
    if (mode->no_timing) {
        const size_t used = strlen(out);
        snprintf(out + used, out_size - used, "; no timing passes");
    }
    if (mode->stress) {
        const size_t used = strlen(out);
        snprintf(out + used, out_size - used, "; stress cases");
    }
    if (mode->fatal) {
        const size_t used = strlen(out);
        snprintf(out + used, out_size - used, "; %s", MODE_FATAL_SAID);
    }
    if (mode->hang) {
        const size_t used = strlen(out);
        snprintf(out + used, out_size - used, "; the cases that hang");
    }
    if (mode->machine[0] != '\0') {
        const size_t used = strlen(out);
        snprintf(out + used, out_size - used, "; machine %s, %s%s", mode->machine,
                 mode->machine_version[0] != '\0' ? "version " : "no version given",
                 mode->machine_version);
    }
    if (mode->machine_was_refused) {
        const size_t used = strlen(out);
        char kept[MODE_MACHINE_MAX + MODE_MACHINE_VERSION_MAX + 48u];
        if (mode->machine[0] != '\0') {
            snprintf(kept, sizeof(kept), "the run keeps machine %s%s%s", mode->machine,
                     mode->machine_version[0] != '\0' ? "@" : "", mode->machine_version);
        } else {
            snprintf(kept, sizeof(kept), "the run has no machine, and no ban holds");
        }
        if (mode->machine_refused_version) {
            snprintf(out + used, out_size - used,
                     "; machine=%s%s has a version that is not 1 to %u letters, digits, dots, dashes "
                     "and underscores: %s",
                     mode->machine_refused, mode->machine_refused_cut ? "..." : "",
                     (unsigned)MODE_MACHINE_VERSION_MAX, kept);
        } else {
            snprintf(out + used, out_size - used,
                     "; machine=%s%s names no machine the ban list knows (%s): %s",
                     mode->machine_refused, mode->machine_refused_cut ? "..." : "", BanMachineList(),
                     kept);
        }
    }
    if (mode->unban) {
        const size_t used = strlen(out);
        snprintf(out + used, out_size - used, "; the cases banned on the machine run too");
    }
    DescribeSkips(out, out_size, mode);
    if (mode->retry) {
        const size_t used = strlen(out);
        snprintf(out + used, out_size - used, "; the crash list's cases again%s",
                 mode->retry_by_screen ? ", chosen on the start screen" : "");
    }
    if (mode->no_journal) {
        const size_t used = strlen(out);
        snprintf(out + used, out_size - used, "; no journal");
    }
    if (mode->listen) {
        const size_t used = strlen(out);
        snprintf(out + used, out_size - used, "; listening sequence");
    }
    if (mode->no_progress) {
        const size_t used = strlen(out);
        snprintf(out + used, out_size - used, "; no running screen");
    }
    if (mode->label[0] != '\0') {
        const size_t used = strlen(out);
        snprintf(out + used, out_size - used, "; label %s", mode->label);
    }
    if (mode->label_was_refused) {
        const size_t used = strlen(out);
        snprintf(out + used, out_size - used,
                 "; label refused: \"%s%s\", a folder name takes 1 to %u letters, digits, dots, "
                 "dashes and underscores, the first not a dot",
                 mode->label_refused, mode->label_refused_cut ? "..." : "",
                 (unsigned)MODE_LABEL_MAX);
    }
    if (mode->frames_named) {
        const size_t used = strlen(out);
        const bool game = (ModeFamiliesToRunOf(mode, present, asked) & MODE_FAMILY_GAME) != 0u;
        snprintf(out + used, out_size - used, "; frames %u%s", (unsigned)mode->frames,
                 game ? "" : ", for the game family, which this run leaves out");
    }
    if (mode->frames_was_refused) {
        const size_t used = strlen(out);
        snprintf(out + used, out_size - used,
                 "; frames=%s%s is not a whole number from 1 to %u: it keeps %u",
                 mode->frames_refused, mode->frames_refused_cut ? "..." : "",
                 (unsigned)MODE_FRAMES_MAX, (unsigned)mode->frames);
    }
    if (mode->vram != 0u) {
        const size_t used = strlen(out);
        const bool graphics =
            (ModeFamiliesToRunOf(mode, present, asked) & MODE_FAMILY_GRAPHICS) != 0u;
        const bool under = mode->vram < TEX8K_VRAM_MIB;
        const char *const then = !graphics ? ", in the graphics family, which this run leaves out"
                                 : under   ? ": their cases are skipped as unavailable"
                                           : "";
        snprintf(out + used, out_size - used, "; vram %u MiB, %s the %u tex8k's textures want%s",
                 (unsigned)mode->vram, under ? "under" : "not under", (unsigned)TEX8K_VRAM_MIB,
                 then);
    }
    if (mode->vram_was_refused) {
        const size_t used = strlen(out);
        char kept[96];
        if (mode->vram != 0u) {
            snprintf(kept, sizeof(kept), "it keeps %u", (unsigned)mode->vram);
        } else {
            snprintf(kept, sizeof(kept), "the run has no vram, and tex8k's textures are tried");
        }
        snprintf(out + used, out_size - used,
                 "; vram=%s%s is not a whole number of MiB from 1 to %u: %s", mode->vram_refused,
                 mode->vram_refused_cut ? "..." : "", (unsigned)MODE_VRAM_MAX, kept);
    }
    DescribeName(out, out_size, "baseline", &mode->baseline, "this run's times added as a sample");
    DescribeName(out, out_size, "compare", &mode->compare, "this run's times held to its samples");
    if (mode->saved_by_screen) {
        const size_t used = strlen(out);
        snprintf(out + used, out_size - used, "; saved to the mode file by the start screen");
    }
    const uint32_t missing = mode->families_named & ~present;
    if (missing != 0u) {
        size_t used = strlen(out);
        snprintf(out + used, out_size - used, "; not in this build: ");
        AppendFamilies(out, out_size, missing);
    }
    if (mode->longer) {
        const size_t used = strlen(out);
        snprintf(out + used, out_size - used, "; mode file longer than %u bytes, the rest ignored",
                 (unsigned)MODE_FILE_MAX);
    }
    if (mode->unknown_count > 0u) {
        const size_t used = strlen(out);
        snprintf(out + used, out_size - used, "; %u unknown word%s ignored: %s",
                 (unsigned)mode->unknown_count, mode->unknown_count == 1u ? "" : "s",
                 mode->unknown);
    }
}

size_t ModeDescribe(char *out, size_t out_size, const RunMode *mode, uint32_t present) {
    return ModeDescribeOf(out, out_size, mode, present, MODE_FAMILIES_ASKED);
}

size_t ModeDescribeOf(char *out, size_t out_size, const RunMode *mode, uint32_t present,
                      uint32_t asked) {
    if (out == NULL || out_size == 0u) {
        return 0;
    }
    out[0] = '\0';
    if (mode == NULL) {
        return 0;
    }
    // Whole first, so a line too long for @p out (three refused names with long words say a few
    // hundred bytes each) says where it was cut and how long it was instead of ending mid-quote.
    char whole[2048];
    whole[0] = '\0';
    Describe(whole, sizeof(whole), mode, present, asked);
    const size_t length = strlen(whole);
    if (length < out_size) {
        memcpy(out, whole, length + 1u);
        return length;
    }
    char ending[96];
    snprintf(ending, sizeof(ending), "...; the mode line is cut here, its whole is %s%zu bytes",
             length + 1u >= sizeof(whole) ? "at least " : "", length);
    const size_t ends = strlen(ending);
    const size_t kept = out_size > ends ? out_size - 1u - ends : out_size - 1u;
    memcpy(out, whole, kept);
    out[kept] = '\0';
    if (out_size > ends) {
        memcpy(out + kept, ending, ends + 1u);
    }
    return strlen(out);
}

size_t ModeKey(char *out, size_t out_size, const RunMode *mode) {
    if (out == NULL || out_size == 0u) {
        return 0;
    }
    out[0] = '\0';
    if (mode == NULL) {
        return 0;
    }
    // Only when it is not the default, since that is the same scene and the same report.
    char frames[24];
    snprintf(frames, sizeof(frames), "frames=%u", (unsigned)mode->frames);
    // The machine decides which bans hold, so a run on another machine is another run.
    char machine[MODE_MACHINE_MAX + MODE_MACHINE_VERSION_MAX + 16u];
    snprintf(machine, sizeof(machine), "machine=%s%s%s", mode->machine,
             mode->machine_version[0] != '\0' ? "@" : "", mode->machine_version);
    // The suites a run leaves out change what it reports, in the order of their names, so two
    // files that name them in another order are one run.
    char skips[MODE_SKIPS_MAX * (MODE_SKIP_NAME_MAX + 6u) + 1u] = "";
    for (uint32_t i = 0; i < mode->skip_count && i < MODE_SKIPS_MAX; ++i) {
        const size_t used = strlen(skips);
        snprintf(skips + used, sizeof(skips) - used, "%sskip=%s", i > 0u ? " " : "",
                 mode->skip[i]);
    }
    // The memory decides whether tex8k's textures are tried, so another memory is another run.
    char vram[24];
    snprintf(vram, sizeof(vram), "vram=%u", (unsigned)mode->vram);
    const struct {
        bool on;
        const char *word;
    } WORDS[] = {
        { mode->all, "all" },
        { (mode->families_named & MODE_FAMILY_AUDIO) != 0u, "audio" },
        { (mode->families_named & MODE_FAMILY_CPU) != 0u, "cpu" },
        { (mode->families_named & MODE_FAMILY_DISPLAY) != 0u, "display" },
        // It changes no case, but the mode line says what it does now: a run that has it says so.
        { mode->fatal, "fatal" },
        { mode->frames != MODE_FRAMES_DEFAULT, frames },
        { (mode->families_named & MODE_FAMILY_FRAMEWORK) != 0u, "framework" },
        { (mode->families_named & MODE_FAMILY_FS) != 0u, "fs" },
        { (mode->families_named & MODE_FAMILY_GAME) != 0u, "game" },
        { (mode->families_named & MODE_FAMILY_GRAPHICS) != 0u, "graphics" },
        { mode->hang, "hang" },
        { mode->every_case, "images" },
        { (mode->families_named & MODE_FAMILY_JNI) != 0u, "jni" },
        { (mode->families_named & MODE_FAMILY_KERNEL) != 0u, "kernel" },
        { (mode->families_named & MODE_FAMILY_LIBC) != 0u, "libc" },
        { mode->listen, "listen" },
        { mode->machine[0] != '\0', machine },
        { (mode->families_named & MODE_FAMILY_NET) != 0u, "net" },
        { mode->no_timing, "notime" },
        { (mode->families_named & MODE_FAMILY_NVN) != 0u, "nvn" },
        { (mode->families_named & MODE_FAMILY_PAD) != 0u, "pad" },
        { mode->retry, "retry" },
        { (mode->families_named & MODE_FAMILY_SERVICE) != 0u, "service" },
        { mode->skip_count > 0u, skips },
        { mode->stress, "stress" },
        { mode->unban, "unban" },
        { (mode->families_named & MODE_FAMILY_VIDEO) != 0u, "video" },
        { mode->vram != 0u, vram },
    };
    for (size_t i = 0; i < sizeof(WORDS) / sizeof(WORDS[0]); ++i) {
        if (!WORDS[i].on) {
            continue;
        }
        const size_t used = strlen(out);
        snprintf(out + used, out_size - used, "%s%s", used > 0u ? " " : "", WORDS[i].word);
    }
    return strlen(out);
}

size_t ModeRunFolder(char *out, size_t out_size, const char *hash, const RunMode *mode) {
    if (out == NULL || out_size == 0u) {
        return 0;
    }
    out[0] = '\0';
    char kept[MODE_HASH_MAX + 1u];
    size_t n = 0;
    for (; hash != NULL && hash[n] != '\0' && n < MODE_HASH_MAX; ++n) {
        const char c = hash[n];
        const bool fits = IsLetter(c) || (c >= '0' && c <= '9') || c == '.' || c == '-';
        kept[n] = fits ? c : '-';
    }
    kept[n] = '\0';
    // A hash of dots alone would name this folder's parent or itself.
    bool only_dots = true;
    for (size_t i = 0; i < n; ++i) {
        only_dots = only_dots && kept[i] == '.';
    }
    const char *const name = n > 0u && !only_dots ? kept : "unknown";
    const bool labelled = mode != NULL && ModeLabelValid(mode->label);
    const int wrote = snprintf(out, out_size, "%s%s%s", name, labelled ? "_" : "",
                               labelled ? mode->label : "");
    if (wrote < 0 || (size_t)wrote >= out_size) {
        out[0] = '\0';
        return 0;
    }
    return (size_t)wrote;
}

/// Appends @p word to @p out with a space before it when @p out has anything, and says whether it
/// fitted.
static bool AppendWord(char *out, size_t out_size, const char *word) {
    const size_t used = strlen(out);
    const int wrote = snprintf(out + used, out_size - used, "%s%s", used > 0u ? " " : "", word);
    return wrote >= 0 && (size_t)wrote < out_size - used;
}

size_t ModeFormat(char *out, size_t out_size, const RunMode *mode) {
    if (out == NULL || out_size == 0u) {
        return 0;
    }
    out[0] = '\0';
    if (mode == NULL) {
        return 0;
    }
    char key[MODE_KEY_MAX];
    ModeKey(key, sizeof(key), mode);
    bool fits = (mode->detailed || AppendWord(out, out_size, "quick")) &&
                (key[0] == '\0' || AppendWord(out, out_size, key)) &&
                (!mode->no_journal || AppendWord(out, out_size, "nojournal")) &&
                (!mode->no_progress || AppendWord(out, out_size, "noprogress"));
    if (fits && ModeLabelValid(mode->label)) {
        char label[MODE_LABEL_MAX + 8u];
        snprintf(label, sizeof(label), "label=%s", mode->label);
        fits = AppendWord(out, out_size, label);
    }
    // The file's baseline and compare names, which the start screen does not choose and keeps.
    if (fits && ModeLabelValid(mode->baseline.name)) {
        char named[MODE_LABEL_MAX + 12u];
        snprintf(named, sizeof(named), "baseline=%s", mode->baseline.name);
        fits = AppendWord(out, out_size, named);
    }
    if (fits && ModeLabelValid(mode->compare.name)) {
        char named[MODE_LABEL_MAX + 12u];
        snprintf(named, sizeof(named), "compare=%s", mode->compare.name);
        fits = AppendWord(out, out_size, named);
    }
    if (!fits) {
        out[0] = '\0';
        return 0;
    }
    return strlen(out);
}

size_t ModeChosenWords(char *out, size_t out_size, const RunMode *mode) {
    if (out == NULL || out_size == 0u) {
        return 0;
    }
    out[0] = '\0';
    if (mode == NULL) {
        return 0;
    }
    char key[MODE_KEY_MAX];
    ModeKey(key, sizeof(key), mode);
    const bool fits = AppendWord(out, out_size, "screen") &&
                      (key[0] == '\0' || AppendWord(out, out_size, key)) &&
                      (mode->detailed || AppendWord(out, out_size, "quick"));
    if (!fits) {
        out[0] = '\0';
        return 0;
    }
    return strlen(out);
}

RunMode ModeWithChosen(RunMode from_file, const char *chosen) {
    if (chosen == NULL || chosen[0] == '\0') {
        return from_file;
    }
    RunMode mode = from_file;
    if (strncmp(chosen, "screen", 6u) != 0 || (chosen[6] != '\0' && chosen[6] != ' ')) {
        // Not a choice this build wrote: read as words added to the file's, which is what a
        // chosen record said before the start screen chose more than fatal and retry.
        const RunMode added = ModeFromText(chosen);
        mode.retry_by_screen = added.retry && !from_file.retry;
        mode.fatal = from_file.fatal || added.fatal;
        mode.retry = from_file.retry || added.retry;
        return mode;
    }
    const RunMode screen = ModeFromText(chosen + 6);
    mode.families_named = screen.families_named;
    mode.all = screen.all;
    mode.listen = screen.listen;
    mode.every_case = screen.every_case;
    mode.no_timing = screen.no_timing;
    mode.stress = screen.stress;
    mode.hang = screen.hang;
    mode.fatal = screen.fatal;
    mode.retry = screen.retry;
    mode.retry_by_screen = screen.retry && !from_file.retry;
    mode.quick = screen.quick;
    mode.detailed = !screen.quick;
    mode.detailed_by_button = false;
    // Beyond retry, which the mode line already says was chosen there.
    RunMode file_rest = from_file;
    RunMode chosen_rest = mode;
    file_rest.retry = chosen_rest.retry = false;
    char before[MODE_KEY_MAX];
    char after[MODE_KEY_MAX];
    ModeKey(before, sizeof(before), &file_rest);
    ModeKey(after, sizeof(after), &chosen_rest);
    mode.chosen_on_screen = strcmp(before, after) != 0 || mode.detailed != from_file.detailed;
    return mode;
}

void ModeJournalWords(char *run_key, size_t run_key_size, char *chosen, size_t chosen_size,
                      const RunMode *from_file, const RunMode *mode) {
    if (chosen != NULL && chosen_size > 0u) {
        chosen[0] = '\0';
    }
    if (run_key == NULL || run_key_size == 0u || from_file == NULL || mode == NULL) {
        if (run_key != NULL && run_key_size > 0u) {
            run_key[0] = '\0';
        }
        return;
    }
    const RunMode *const in_file = mode->saved_by_screen ? mode : from_file;
    ModeKey(run_key, run_key_size, in_file);
    char run[MODE_KEY_MAX];
    ModeKey(run, sizeof(run), mode);
    if (chosen != NULL && (strcmp(run, run_key) != 0 || mode->detailed != in_file->detailed)) {
        ModeChosenWords(chosen, chosen_size, mode);
    }
}
