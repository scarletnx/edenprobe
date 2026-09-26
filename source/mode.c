// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT

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
    { "graphics", MODE_FAMILY_GRAPHICS },
    { "fs", MODE_FAMILY_FS },
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
        if (longer && IsLetter(text[length])) {
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
    if (!mode->detailed) {
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
        { mode->listen, "listen" },
        { (mode->families_named & MODE_FAMILY_NET) != 0u, "net" },
        { mode->no_timing, "notime" },
        { mode->retry, "retry" },
        { mode->stress, "stress" },
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
