// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <string.h>

#include "mode.h"

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
    { "video", MODE_FAMILY_VIDEO },
    { "audio", MODE_FAMILY_AUDIO },
    { "net", MODE_FAMILY_NET },
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

/// Takes the name after `label=`, @p length bytes of it: kept when a folder can be named after
/// it, quoted in the mode line otherwise.
static void TakeLabel(RunMode *mode, const char *value, size_t length) {
    char name[MODE_LABEL_MAX + 2u];
    const size_t kept = length < sizeof(name) - 1u ? length : sizeof(name) - 1u;
    memcpy(name, value, kept);
    name[kept] = '\0';
    if (length <= MODE_LABEL_MAX && ModeLabelValid(name)) {
        memcpy(mode->label, name, kept + 1u);
        mode->label_was_refused = false;
        mode->label_refused[0] = '\0';
        mode->label_refused_cut = false;
        return;
    }
    mode->label_was_refused = true;
    // Quoted as written, as much as fits; anything a line could not carry as itself is a '?'.
    const size_t quoted = length < MODE_LABEL_MAX ? length : MODE_LABEL_MAX;
    for (size_t i = 0; i < quoted; ++i) {
        const unsigned char c = (unsigned char)value[i];
        mode->label_refused[i] = c < 0x20u || c >= 0x7Fu || c == '"' || c == '\\' ? '?' : (char)c;
    }
    mode->label_refused[quoted] = '\0';
    mode->label_refused_cut = length > MODE_LABEL_MAX;
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
        if (c == '=' && length == 5u && !cut && memcmp(word, "label", 5u) == 0) {
            // A name rather than words: it runs to the next space or line end, whatever it holds.
            size_t end = i + 1u;
            while (end < size && !EndsValue(text[end])) {
                ++end;
            }
            TakeLabel(mode, text + i + 1u, end - i - 1u);
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

static bool StartsWithLabel(const char *text) {
    static const char LABEL[] = "label=";
    for (size_t i = 0; i < sizeof(LABEL) - 1u; ++i) {
        const char c = text[i];
        if ((c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c) != LABEL[i]) {
            return false;
        }
    }
    return true;
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
    if (source == MODE_SOURCE_FILE && text != NULL) {
        mode.longer = longer;
        const size_t piece = LastPiece(text, length);
        if (longer && !EndsValue(text[length]) && length - piece >= 6u &&
            StartsWithLabel(text + piece)) {
            // A label's name runs on past the limit: what was read of it names another folder.
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

uint32_t ModeFamiliesToRun(const RunMode *mode, uint32_t present) {
    if (mode == NULL) {
        return present & ~MODE_FAMILIES_ASKED;
    }
    const uint32_t listened = mode->listen ? MODE_FAMILY_AUDIO : 0u;
    if (mode->families_named == 0u) {
        return (present & ~MODE_FAMILIES_ASKED) | (listened & present);
    }
    return (mode->families_named | listened) & present;
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

size_t ModeDescribe(char *out, size_t out_size, const RunMode *mode, uint32_t present) {
    if (out == NULL || out_size == 0u) {
        return 0;
    }
    out[0] = '\0';
    if (mode == NULL) {
        return 0;
    }
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
    AppendFamilies(out, out_size, ModeFamiliesToRun(mode, present));
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
        snprintf(out + used, out_size - used, "; the cases that may crash%s",
                 mode->fatal_by_screen ? ", chosen on the start screen" : "");
    }
    if (mode->hang) {
        const size_t used = strlen(out);
        snprintf(out + used, out_size - used, "; the cases that hang");
    }
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
    const struct {
        bool on;
        const char *word;
    } WORDS[] = {
        { (mode->families_named & MODE_FAMILY_AUDIO) != 0u, "audio" },
        { (mode->families_named & MODE_FAMILY_CPU) != 0u, "cpu" },
        { mode->fatal, "fatal" },
        { (mode->families_named & MODE_FAMILY_FS) != 0u, "fs" },
        { (mode->families_named & MODE_FAMILY_GRAPHICS) != 0u, "graphics" },
        { mode->hang, "hang" },
        { mode->every_case, "images" },
        { (mode->families_named & MODE_FAMILY_KERNEL) != 0u, "kernel" },
        { mode->listen, "listen" },
        { (mode->families_named & MODE_FAMILY_NET) != 0u, "net" },
        { mode->no_timing, "notime" },
        { (mode->families_named & MODE_FAMILY_PAD) != 0u, "pad" },
        { mode->retry, "retry" },
        { mode->stress, "stress" },
        { (mode->families_named & MODE_FAMILY_VIDEO) != 0u, "video" },
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
    char key[128];
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
    char key[128];
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
        mode.fatal_by_screen = added.fatal && !from_file.fatal;
        mode.retry_by_screen = added.retry && !from_file.retry;
        mode.fatal = from_file.fatal || added.fatal;
        mode.retry = from_file.retry || added.retry;
        return mode;
    }
    const RunMode screen = ModeFromText(chosen + 6);
    mode.families_named = screen.families_named;
    mode.listen = screen.listen;
    mode.every_case = screen.every_case;
    mode.no_timing = screen.no_timing;
    mode.stress = screen.stress;
    mode.hang = screen.hang;
    mode.fatal = screen.fatal;
    mode.retry = screen.retry;
    mode.fatal_by_screen = screen.fatal && !from_file.fatal;
    mode.retry_by_screen = screen.retry && !from_file.retry;
    mode.quick = screen.quick;
    mode.detailed = !screen.quick;
    mode.detailed_by_button = false;
    // Beyond fatal and retry, which the mode line already says were chosen there.
    RunMode file_rest = from_file;
    RunMode chosen_rest = mode;
    file_rest.fatal = file_rest.retry = chosen_rest.fatal = chosen_rest.retry = false;
    char before[128];
    char after[128];
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
    char run[128];
    ModeKey(run, sizeof(run), mode);
    if (chosen != NULL && (strcmp(run, run_key) != 0 || mode->detailed != in_file->detailed)) {
        ModeChosenWords(chosen, chosen_size, mode);
    }
}
