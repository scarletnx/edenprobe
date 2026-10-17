// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <string.h>

#include "padref.h"

// The buttons are asked for by the name printed on them, never by where they sit: an emulator
// that binds a host controller by position hands a game B for the button labelled A on a
// controller whose A is the bottom one, and the case has to see that rather than assume either.
static const PadPrompt PROMPTS[] = {
    { "a controller answers before the first prompt", PAD_ASK_READY, "ready?",
      "press any button on the controller you will use; up to 30 s", 0u, 0.0f },
    { "the button labelled A arrives as A alone", PAD_ASK_BUTTON, "A",
      "press the button labelled A, once", PAD_A, 0.0f },
    { "the button labelled B arrives as B alone", PAD_ASK_BUTTON, "B",
      "press the button labelled B, once", PAD_B, 0.0f },
    { "the button labelled X arrives as X alone", PAD_ASK_BUTTON, "X",
      "press the button labelled X, once", PAD_X, 0.0f },
    { "the button labelled Y arrives as Y alone", PAD_ASK_BUTTON, "Y",
      "press the button labelled Y, once", PAD_Y, 0.0f },
    { "the button labelled L arrives as L alone", PAD_ASK_BUTTON, "L",
      "press the button labelled L, the left shoulder, once", PAD_L, 0.0f },
    { "the button labelled R arrives as R alone", PAD_ASK_BUTTON, "R",
      "press the button labelled R, the right shoulder, once", PAD_R, 0.0f },
    { "the button labelled ZL arrives as ZL alone", PAD_ASK_BUTTON, "ZL",
      "press ZL, the left trigger, once and all the way", PAD_ZL, 0.0f },
    { "the button labelled ZR arrives as ZR alone", PAD_ASK_BUTTON, "ZR",
      "press ZR, the right trigger, once and all the way", PAD_ZR, 0.0f },
    { "the button labelled + arrives as plus alone", PAD_ASK_BUTTON, "+",
      "press plus (start, menu), once", PAD_PLUS, 0.0f },
    { "the button labelled - arrives as minus alone", PAD_ASK_BUTTON, "-",
      "press minus (select, view), once", PAD_MINUS, 0.0f },
    { "the left stick pressed in arrives as LS alone", PAD_ASK_BUTTON, "L stick",
      "press the left stick in, once, without tilting it", PAD_STICK_L, 0.0f },
    { "the right stick pressed in arrives as RS alone", PAD_ASK_BUTTON, "R stick",
      "press the right stick in, once, without tilting it", PAD_STICK_R, 0.0f },
    { "up on the d-pad arrives as up alone", PAD_ASK_BUTTON, "up",
      "press up on the d-pad, once", PAD_UP, 0.0f },
    { "down on the d-pad arrives as down alone", PAD_ASK_BUTTON, "down",
      "press down on the d-pad, once", PAD_DOWN, 0.0f },
    { "left on the d-pad arrives as left alone", PAD_ASK_BUTTON, "left",
      "press left on the d-pad, once", PAD_LEFT, 0.0f },
    { "right on the d-pad arrives as right alone", PAD_ASK_BUTTON, "right",
      "press right on the d-pad, once", PAD_RIGHT, 0.0f },
    { "the controller that answered is player 1 with the full key style", PAD_ASK_STYLE, "",
      "", 0u, 0.0f },
    { "every connected slot of the eight players and the handheld has a style", PAD_ASK_SLOTS,
      "", "", 0u, 0.0f },
    { "each controller pressed in turn arrives in one player slot", PAD_ASK_EACH, "each pad",
      "press A on every controller you have, one after the other; 5 s with nothing pressed ends "
      "this", 0u, 0.0f },
    { "both sticks let go read nought on both axes", PAD_ASK_REST, "let go",
      "take your thumbs off both sticks and leave them", 0u, 0.0f },
    { "the left stick at its edge reaches full scale on both axes", PAD_ASK_EDGE_LEFT,
      "L stick", "push the left stick to its edge and turn it slowly round, for 3 seconds", 0u,
      0.0f },
    { "the right stick at its edge reaches full scale on both axes", PAD_ASK_EDGE_RIGHT,
      "R stick", "push the right stick to its edge and turn it slowly round, for 3 seconds", 0u,
      0.0f },
    { "a vibration at half amplitude is felt", PAD_ASK_VIBRATION, "feel it?",
      "the controller vibrates at half strength for a second. felt it? up: yes, down: no, or tap",
      0u, 0.5f },
    { "a vibration at full amplitude is felt", PAD_ASK_VIBRATION, "feel it?",
      "the controller vibrates at full strength for a second. felt it? up: yes, down: no, or tap",
      0u, 1.0f },
    { "a vibration sent nought stops", PAD_ASK_STOPPED, "stopped?",
      "did that vibration stop when its second ran out? up: yes, down: no, or tap", 0u, 0.0f },
};

size_t PadPromptCount(void) {
    return sizeof(PROMPTS) / sizeof(PROMPTS[0]);
}

const PadPrompt *PadPromptAt(size_t index) {
    return index < PadPromptCount() ? &PROMPTS[index] : NULL;
}

// Bit by bit, the names libnx's comments give them, short.
static const char *const BUTTON_WORDS[] = {
    "A",        "B",         "X",        "Y",        "LS",       "RS",        "L",
    "R",        "ZL",        "ZR",       "plus",     "minus",    "left",      "up",
    "right",    "down",      "LS-left",  "LS-up",    "LS-right", "LS-down",   "RS-left",
    "RS-up",    "RS-right",  "RS-down",  "left-SL",  "left-SR",  "right-SL",  "right-SR",
    "palma",    "verify",    "hh-left-B", "C-left",  "C-up",     "C-right",   "C-down",
};

size_t PadButtonWords(char *out, size_t out_size, uint64_t mask) {
    if (out == NULL || out_size == 0u) {
        return 0;
    }
    out[0] = '\0';
    size_t used = 0;
    for (unsigned bit = 0; bit < 64u; ++bit) {
        if ((mask & (1ull << bit)) == 0u) {
            continue;
        }
        char unnamed[16];
        const char *word = unnamed;
        if (bit < sizeof(BUTTON_WORDS) / sizeof(BUTTON_WORDS[0])) {
            word = BUTTON_WORDS[bit];
        } else {
            snprintf(unnamed, sizeof(unnamed), "bit%u", bit);
        }
        const int wrote = snprintf(out + used, out_size - used, "%s%s", used > 0u ? " " : "", word);
        if (wrote < 0 || (size_t)wrote >= out_size - used) {
            out[used] = '\0';
            break;
        }
        used += (size_t)wrote;
    }
    if (used == 0u) {
        snprintf(out, out_size, "nothing");
    }
    return strlen(out);
}

uint64_t PadEdges(uint64_t previous, uint64_t held) {
    return held & ~previous & ~PAD_STICK_DIRECTIONS;
}

void PadPressBegin(PadPress *press, uint64_t held, uint64_t now_ns) {
    memset(press, 0, sizeof(*press));
    press->previous = held;
    press->began_ns = now_ns;
    press->state = PAD_PRESS_WAITING;
}

PadPressState PadPressStep(PadPress *press, uint64_t held, uint64_t now_ns) {
    if (press->state == PAD_PRESS_DONE || press->state == PAD_PRESS_TIMED_OUT) {
        return press->state;
    }
    const uint64_t edges = PadEdges(press->previous, held);
    press->previous = held;
    if (press->state == PAD_PRESS_WAITING) {
        if (edges != 0u) {
            press->state = PAD_PRESS_COLLECTING;
            press->first_ns = now_ns;
            press->arrived = edges;
        } else if (now_ns - press->began_ns >= PAD_PROMPT_NANOSECONDS) {
            press->state = PAD_PRESS_TIMED_OUT;
        }
        return press->state;
    }
    if (now_ns - press->first_ns < PAD_WINDOW_NANOSECONDS) {
        press->arrived |= edges;
    } else {
        press->state = PAD_PRESS_DONE;
    }
    return press->state;
}

bool PadPressJudge(uint64_t asked, uint64_t arrived, char *detail, size_t detail_size) {
    char want[64];
    char got[256];
    PadButtonWords(want, sizeof(want), asked);
    PadButtonWords(got, sizeof(got), arrived);
    const bool passed = arrived == asked;
    if (detail != NULL && detail_size > 0u) {
        snprintf(detail, detail_size, "asked for %s, arrived %s", want, got);
    }
    return passed;
}

PadAnswer PadAnswerOf(uint64_t edges) {
    const bool yes = (edges & PAD_UP) != 0u;
    const bool no = (edges & PAD_DOWN) != 0u;
    if (yes == no) {
        return PAD_ANSWER_NONE;
    }
    return yes ? PAD_ANSWER_YES : PAD_ANSWER_NO;
}

void PadStickReset(PadStick *stick) {
    memset(stick, 0, sizeof(*stick));
}

static uint32_t Magnitude(int32_t value) {
    return value < 0 ? (uint32_t)(-(int64_t)value) : (uint32_t)value;
}

uint32_t PadRadius(int32_t x, int32_t y) {
    const uint64_t ax = Magnitude(x);
    const uint64_t ay = Magnitude(y);
    const uint64_t square = ax * ax + ay * ay;
    // The integer square root, bit by bit: no floating point, so both builds agree to the unit.
    uint64_t root = 0;
    uint64_t bit = 1ull << 62;
    uint64_t rest = square;
    while (bit > rest) {
        bit >>= 2;
    }
    while (bit != 0u) {
        if (rest >= root + bit) {
            rest -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return (uint32_t)root;
}

void PadStickAdd(PadStick *stick, int32_t x, int32_t y) {
    const uint32_t ax = Magnitude(x);
    const uint32_t ay = Magnitude(y);
    const uint32_t radius = PadRadius(x, y);
    stick->max_x = ax > stick->max_x ? ax : stick->max_x;
    stick->max_y = ay > stick->max_y ? ay : stick->max_y;
    stick->max_radius = radius > stick->max_radius ? radius : stick->max_radius;
    ++stick->samples;
}

bool PadStickStarted(int32_t x, int32_t y) {
    return PadRadius(x, y) >= (uint32_t)(PAD_STICK_FULL / 2 + 1);
}

bool PadRestJudge(const PadStick *left, const PadStick *right, char *detail, size_t detail_size) {
    const bool passed = left->max_x == 0u && left->max_y == 0u && right->max_x == 0u &&
                        right->max_y == 0u;
    if (detail != NULL && detail_size > 0u) {
        snprintf(detail, detail_size,
                 "at most: left x %u y %u, right x %u y %u, over %u frames",
                 (unsigned)left->max_x, (unsigned)left->max_y, (unsigned)right->max_x,
                 (unsigned)right->max_y, (unsigned)left->samples);
    }
    return passed;
}

const char *PadEdgeShape(const PadStick *stick) {
    const uint64_t axis = stick->max_x > stick->max_y ? stick->max_x : stick->max_y;
    const uint64_t radius = stick->max_radius;
    if (axis == 0u) {
        return "not reached";
    }
    // A circle's farthest radius is its farthest axis; a square's is that times 1.414.
    if (radius * 10u <= axis * 11u) {
        return "a circle";
    }
    if (radius * 10u >= axis * 13u) {
        return "a square";
    }
    return "between a circle and a square";
}

static bool Reaches(uint32_t value) {
    return (uint64_t)value * 1000u >=
           (uint64_t)PAD_STICK_FULL * (1000u - PAD_EDGE_TOLERANCE_PER_MILLE);
}

bool PadEdgeJudge(const char *which, const PadStick *stick, char *detail, size_t detail_size) {
    const bool passed = stick->samples > 0u && Reaches(stick->max_x) && Reaches(stick->max_y);
    if (detail != NULL && detail_size > 0u) {
        snprintf(detail, detail_size,
                 "%s stick at most: x %u, y %u, radius %u over %u frames; the edge is %s",
                 which != NULL ? which : "the", (unsigned)stick->max_x, (unsigned)stick->max_y,
                 (unsigned)stick->max_radius, (unsigned)stick->samples, PadEdgeShape(stick));
    }
    return passed;
}

const char *PadIdWord(uint32_t id) {
    static const char *const PLAYERS[PAD_ID_PLAYERS] = { "No1", "No2", "No3", "No4",
                                                         "No5", "No6", "No7", "No8" };
    if (id < PAD_ID_PLAYERS) {
        return PLAYERS[id];
    }
    if (id == PAD_ID_HANDHELD) {
        return "Handheld";
    }
    if (id == PAD_ID_OTHER) {
        return "Other";
    }
    return "unknown";
}

size_t PadStyleWords(char *out, size_t out_size, uint32_t style) {
    static const struct {
        uint32_t bit;
        const char *word;
    } STYLES[] = {
        { 1u << 0, "FullKey" },   { 1u << 1, "Handheld" },      { 1u << 2, "JoyDual" },
        { 1u << 3, "JoyLeft" },   { 1u << 4, "JoyRight" },      { 1u << 5, "Gc" },
        { 1u << 6, "Palma" },     { 1u << 7, "Lark" },          { 1u << 8, "HandheldLark" },
        { 1u << 9, "Lucia" },     { 1u << 10, "Lagon" },        { 1u << 11, "Lager" },
        { 1u << 29, "SystemExt" }, { 1u << 30, "System" },
    };
    if (out == NULL || out_size == 0u) {
        return 0;
    }
    out[0] = '\0';
    size_t used = 0;
    uint32_t named = 0;
    for (size_t i = 0; i < sizeof(STYLES) / sizeof(STYLES[0]); ++i) {
        if ((style & STYLES[i].bit) == 0u) {
            continue;
        }
        named |= STYLES[i].bit;
        const int wrote =
            snprintf(out + used, out_size - used, "%s%s", used > 0u ? " " : "", STYLES[i].word);
        if (wrote < 0 || (size_t)wrote >= out_size - used) {
            out[used] = '\0';
            return strlen(out);
        }
        used += (size_t)wrote;
    }
    if ((style & ~named) != 0u) {
        snprintf(out + used, out_size - used, "%s0x%x", used > 0u ? " " : "",
                 (unsigned)(style & ~named));
    } else if (used == 0u) {
        snprintf(out, out_size, "none");
    }
    return strlen(out);
}

PadStyleVerdict PadStyleJudge(uint32_t id, uint32_t style, char *detail, size_t detail_size) {
    char styles[96];
    PadStyleWords(styles, sizeof(styles), style);
    if (detail != NULL && detail_size > 0u) {
        snprintf(detail, detail_size, "the buttons arrived from %s, style %s", PadIdWord(id),
                 styles);
    }
    if (id == PAD_ID_HANDHELD) {
        return PAD_STYLE_HANDHELD_ONLY;
    }
    return id == 0u && (style & PAD_STYLE_FULL_KEY) != 0u ? PAD_STYLE_PASS : PAD_STYLE_FAIL;
}

bool PadNobodyThere(uint32_t unanswered_in_a_row) {
    return unanswered_in_a_row >= PAD_UNANSWERED_LIMIT;
}

uint32_t PadSlotId(uint32_t slot) {
    return slot < PAD_ID_PLAYERS ? slot : PAD_ID_HANDHELD;
}

bool PadSlotsJudge(const bool connected[PAD_SLOTS], const uint32_t styles[PAD_SLOTS], char *detail,
                   size_t detail_size) {
    bool any = false;
    bool styled = true;
    size_t used = 0;
    if (detail != NULL && detail_size > 0u) {
        detail[0] = '\0';
    }
    for (uint32_t slot = 0; slot < PAD_SLOTS; ++slot) {
        any = any || connected[slot];
        styled = styled && (!connected[slot] || styles[slot] != 0u);
        if (detail == NULL || used >= detail_size) {
            continue;
        }
        char words[96] = "-";
        if (connected[slot]) {
            PadStyleWords(words, sizeof(words), styles[slot]);
        }
        const int wrote = snprintf(detail + used, detail_size - used, "%s%s %s",
                                   slot > 0u ? ", " : "", PadIdWord(PadSlotId(slot)), words);
        used = wrote < 0 ? detail_size : used + (size_t)wrote;
    }
    return any && styled;
}

void PadEachBegin(PadEach *each, uint64_t now_ns) {
    memset(each, 0, sizeof(*each));
    each->began_ns = now_ns;
    each->state = PAD_PRESS_WAITING;
}

PadPressState PadEachStep(PadEach *each, uint32_t slots_pressed, uint64_t now_ns) {
    if (each->state == PAD_PRESS_DONE || each->state == PAD_PRESS_TIMED_OUT) {
        return each->state;
    }
    if (each->collecting && now_ns - each->first_ns >= PAD_WINDOW_NANOSECONDS) {
        each->collecting = false;
    }
    if (slots_pressed != 0u) {
        if (each->collecting) {
            each->presses[each->count - 1u] |= slots_pressed;
        } else if (each->count < PAD_EACH_MAX) {
            each->presses[each->count++] = slots_pressed;
            each->first_ns = now_ns;
            each->collecting = true;
        }
        each->last_ns = now_ns;
        each->state = PAD_PRESS_COLLECTING;
        return each->state;
    }
    if (each->count == 0u) {
        if (now_ns - each->began_ns >= PAD_PROMPT_NANOSECONDS) {
            each->state = PAD_PRESS_TIMED_OUT;
        }
    } else if (now_ns - each->last_ns >= PAD_EACH_QUIET_NANOSECONDS) {
        each->state = PAD_PRESS_DONE;
    }
    return each->state;
}

/// The slots in @p mask, as "No1" or "No1 and No2".
static void SlotWords(char *out, size_t out_size, uint32_t mask) {
    out[0] = '\0';
    size_t used = 0;
    for (uint32_t slot = 0; slot < PAD_SLOTS && used < out_size; ++slot) {
        if ((mask & (1u << slot)) == 0u) {
            continue;
        }
        const int wrote = snprintf(out + used, out_size - used, "%s%s", used > 0u ? " and " : "",
                                   PadIdWord(PadSlotId(slot)));
        used = wrote < 0 ? out_size : used + (size_t)wrote;
    }
}

bool PadEachJudge(const PadEach *each, char *detail, size_t detail_size) {
    bool single = each->count > 0u;
    for (uint32_t i = 0; i < each->count; ++i) {
        const uint32_t mask = each->presses[i];
        single = single && mask != 0u && (mask & (mask - 1u)) == 0u;
    }
    if (detail == NULL || detail_size == 0u) {
        return single;
    }
    int wrote = snprintf(detail, detail_size, "%u press%s, from", (unsigned)each->count,
                         each->count == 1u ? "" : "es");
    size_t used = wrote < 0 ? detail_size : (size_t)wrote;
    for (uint32_t i = 0; i < each->count && used < detail_size; ++i) {
        char slots[64];
        SlotWords(slots, sizeof(slots), each->presses[i]);
        wrote = snprintf(detail + used, detail_size - used, "%s %s", i > 0u ? "," : "", slots);
        used = wrote < 0 ? detail_size : used + (size_t)wrote;
    }
    return single;
}
