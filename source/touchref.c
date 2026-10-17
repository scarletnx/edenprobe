// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <string.h>

#include "padref.h"
#include "touchref.h"

#define FAR_X (TOUCH_WIDTH - TOUCH_INSET)
#define FAR_Y (TOUCH_HEIGHT - TOUCH_INSET)

// The corners first and the middle last, so a mapping that is off by a scale shows at the first
// corner and one off by an offset shows everywhere.
static const TouchPrompt PROMPTS[] = {
    { "a touch arrives before the first target", TOUCH_ASK_READY, "touch",
      "touch the screen anywhere, then lift your finger; up to 30 s", 0u, 0u },
    { "a tap on the top left target lands within 48 pixels of it", TOUCH_ASK_TARGET, "top left",
      "tap the middle of the cross at the top left, then lift", TOUCH_INSET, TOUCH_INSET },
    { "a tap on the top right target lands within 48 pixels of it", TOUCH_ASK_TARGET, "top right",
      "tap the middle of the cross at the top right, then lift", FAR_X, TOUCH_INSET },
    { "a tap on the bottom left target lands within 48 pixels of it", TOUCH_ASK_TARGET,
      "bottom left", "tap the middle of the cross at the bottom left, then lift", TOUCH_INSET,
      FAR_Y },
    { "a tap on the bottom right target lands within 48 pixels of it", TOUCH_ASK_TARGET,
      "bottom right", "tap the middle of the cross at the bottom right, then lift", FAR_X,
      FAR_Y },
    { "a tap on the middle target lands within 48 pixels of it", TOUCH_ASK_TARGET, "middle",
      "tap the middle of the cross in the middle of the screen, then lift", TOUCH_WIDTH / 2u,
      TOUCH_HEIGHT / 2u },
    { "two fingers at once arrive as two touches with different ids", TOUCH_ASK_TWO,
      "two fingers", "put two fingers on the screen together, a hand's width apart, then lift both",
      0u, 0u },
    { "a finger dragged left to right moves right under one id", TOUCH_ASK_DRAG, "drag",
      "put one finger on the left mark, slide it to the right mark without lifting, then lift",
      0u, 0u },
    { "a lifted finger leaves no touch behind", TOUCH_ASK_RELEASE, "", "", 0u, 0u },
};

size_t TouchPromptCount(void) {
    return sizeof(PROMPTS) / sizeof(PROMPTS[0]);
}

const TouchPrompt *TouchPromptAt(size_t index) {
    return index < TouchPromptCount() ? &PROMPTS[index] : NULL;
}

void TouchWatchBegin(TouchWatch *watch, uint32_t count_now, uint64_t now_ns) {
    memset(watch, 0, sizeof(*watch));
    watch->state = TOUCH_WAITING;
    watch->blocked = count_now > 0u;
    watch->began_ns = now_ns;
}

TouchState TouchWatchStep(TouchWatch *watch, const TouchFrame *frame, uint64_t now_ns,
                          uint64_t timeout_ns) {
    if (watch->state == TOUCH_DONE || watch->state == TOUCH_TIMED_OUT) {
        return watch->state;
    }
    const uint32_t count = frame->count < TOUCH_MAX ? frame->count : TOUCH_MAX;
    if (watch->state == TOUCH_WAITING) {
        // A finger left on the screen from before is not the answer, nor is it until lifted.
        watch->blocked = watch->blocked && count > 0u;
        if (watch->blocked || count == 0u) {
            if (now_ns - watch->began_ns >= timeout_ns) {
                watch->state = TOUCH_TIMED_OUT;
            }
            return watch->state;
        }
        watch->state = TOUCH_DOWN;
        watch->down_ns = now_ns;
        watch->first = frame->points[0];
        watch->max_x = frame->points[0].x;
    }
    if (count == 0u || now_ns - watch->down_ns >= TOUCH_HOLD_NANOSECONDS) {
        watch->held_too_long = count > 0u;
        watch->state = TOUCH_DONE;
        watch->lifted_ns = now_ns;
        return watch->state;
    }
    const TouchPoint *const point = &frame->points[0];
    watch->id_changed = watch->id_changed || (watch->frames > 0u && point->id != watch->last.id);
    watch->last = *point;
    ++watch->frames;
    watch->most = count > watch->most ? count : watch->most;
    if (point->x > watch->max_x) {
        watch->max_x = point->x;
    } else if (watch->max_x - point->x > watch->back) {
        watch->back = watch->max_x - point->x;
    }
    if (count >= 2u && !watch->two) {
        watch->two = true;
        watch->pair[0] = frame->points[0];
        watch->pair[1] = frame->points[1];
    }
    return watch->state;
}

TouchReady TouchReadyJudge(const TouchWatch *watch, uint64_t states, uint64_t samplings,
                           bool docked, char *detail, size_t detail_size) {
    if (watch->state == TOUCH_DONE) {
        snprintf(detail, detail_size,
                 "a touch arrived: id %u at (%u,%u), %u states while down, at most %u at once",
                 (unsigned)watch->first.id, (unsigned)watch->first.x, (unsigned)watch->first.y,
                 (unsigned)watch->frames, (unsigned)watch->most);
        return TOUCH_READY_YES;
    }
    if (docked) {
        snprintf(detail, detail_size,
                 "nothing touched within 30 s, and the console says it is docked, where its "
                 "touch screen is off");
        return TOUCH_READY_UNAVAILABLE;
    }
    if (states == 0u) {
        snprintf(detail, detail_size, "the touch screen handed back no state within 30 s");
        return TOUCH_READY_UNAVAILABLE;
    }
    if (samplings < 2u) {
        snprintf(detail, detail_size,
                 "the touch screen's state never moved: one sampling number over %llu reads",
                 (unsigned long long)states);
        return TOUCH_READY_UNAVAILABLE;
    }
    snprintf(detail, detail_size,
             "nothing touched within 30 s; the touch screen sent %llu states, %llu of them new",
             (unsigned long long)states, (unsigned long long)samplings);
    return TOUCH_READY_UNANSWERED;
}

bool TouchTargetJudge(const TouchPrompt *prompt, const TouchWatch *watch, char *detail,
                      size_t detail_size) {
    const int32_t dx = (int32_t)watch->last.x - (int32_t)prompt->x;
    const int32_t dy = (int32_t)watch->last.y - (int32_t)prompt->y;
    const uint32_t off = PadRadius(dx, dy);
    const bool passed = watch->state == TOUCH_DONE && off <= TOUCH_TOLERANCE;
    snprintf(detail, detail_size,
             "lifted at (%u,%u), %+d %+d from the target at (%u,%u): %u pixels off, %s %u; came "
             "down at (%u,%u), id %u, %u states%s",
             (unsigned)watch->last.x, (unsigned)watch->last.y, (int)dx, (int)dy,
             (unsigned)prompt->x, (unsigned)prompt->y, (unsigned)off,
             off <= TOUCH_TOLERANCE ? "within" : "past", (unsigned)TOUCH_TOLERANCE,
             (unsigned)watch->first.x, (unsigned)watch->first.y, (unsigned)watch->first.id,
             (unsigned)watch->frames, watch->most > 1u ? ", more than one finger" : "");
    return passed;
}

bool TouchTwoJudge(const TouchWatch *watch, char *detail, size_t detail_size) {
    if (!watch->two) {
        snprintf(detail, detail_size,
                 "never two at once: the most in one state was %u, over %u states",
                 (unsigned)watch->most, (unsigned)watch->frames);
        return false;
    }
    const bool apart = watch->pair[0].id != watch->pair[1].id;
    snprintf(detail, detail_size, "two at once, %s %u and %u, at (%u,%u) and (%u,%u); at most %u",
             apart ? "ids" : "both with one id:", (unsigned)watch->pair[0].id,
             (unsigned)watch->pair[1].id, (unsigned)watch->pair[0].x,
             (unsigned)watch->pair[0].y, (unsigned)watch->pair[1].x,
             (unsigned)watch->pair[1].y, (unsigned)watch->most);
    return apart;
}

bool TouchDragJudge(const TouchWatch *watch, char *detail, size_t detail_size) {
    const bool one = watch->most == 1u;
    const bool same = !watch->id_changed;
    const bool left = watch->first.x < TOUCH_WIDTH / 3u;
    const bool right = watch->last.x >= TOUCH_WIDTH - TOUCH_WIDTH / 3u;
    const bool far = watch->last.x >= watch->first.x + TOUCH_DRAG_TRAVEL;
    const bool steady = watch->back <= TOUCH_DRAG_BACK;
    const bool seen = watch->frames >= TOUCH_DRAG_FRAMES;
    const bool passed = watch->state == TOUCH_DONE && one && same && left && right && far &&
                        steady && seen;
    char why[240] = "";
    if (!passed) {
        snprintf(why, sizeof(why), "%s%s%s%s%s%s%s",
                 !one ? "more than one finger; " : "", !same ? "the id changed; " : "",
                 !left ? "began right of the left third; " : "",
                 !right ? "ended short of the right third; " : "",
                 !far ? "travelled under half the screen; " : "",
                 !steady ? "went back past the jitter allowed; " : "",
                 !seen ? "too few states; " : "");
    }
    snprintf(detail, detail_size,
             "%sfrom x %u to %u under id %u, %u states, back at most %u of %u allowed; start "
             "attribute on the first state %s, end on the last %s",
             why, (unsigned)watch->first.x, (unsigned)watch->last.x, (unsigned)watch->first.id,
             (unsigned)watch->frames, (unsigned)watch->back, (unsigned)TOUCH_DRAG_BACK,
             (watch->first.attributes & TOUCH_ATTRIBUTE_START) != 0u ? "yes" : "no",
             (watch->last.attributes & TOUCH_ATTRIBUTE_END) != 0u ? "yes" : "no");
    return passed;
}

void TouchReleaseBegin(TouchRelease *release, uint64_t lifted_ns) {
    memset(release, 0, sizeof(*release));
    release->lifted_ns = lifted_ns;
}

bool TouchReleaseStep(TouchRelease *release, const TouchFrame *frame, uint64_t now_ns) {
    if (now_ns - release->lifted_ns >= TOUCH_RELEASE_NANOSECONDS) {
        return true;
    }
    ++release->frames;
    if (frame->count > 0u) {
        if (release->touched == 0u) {
            release->seen = frame->points[0];
            release->seen_ns = now_ns;
        }
        ++release->touched;
    }
    return false;
}

bool TouchReleaseJudge(const TouchRelease *release, char *detail, size_t detail_size) {
    const unsigned ms = (unsigned)(TOUCH_RELEASE_NANOSECONDS / 1000000u);
    if (release->frames == 0u) {
        snprintf(detail, detail_size, "no state was read in the %u ms after the lift", ms);
        return false;
    }
    if (release->touched == 0u) {
        snprintf(detail, detail_size, "no touch in any of %u states over the %u ms after the lift",
                 (unsigned)release->frames, ms);
        return true;
    }
    snprintf(detail, detail_size,
             "%u of %u states after the lift held a touch, the first %u ms after it: id %u at "
             "(%u,%u)",
             (unsigned)release->touched, (unsigned)release->frames,
             (unsigned)((release->seen_ns - release->lifted_ns) / 1000000u),
             (unsigned)release->seen.id, (unsigned)release->seen.x, (unsigned)release->seen.y);
    return false;
}

void TouchShapeReset(TouchShape *shape) {
    memset(shape, 0, sizeof(*shape));
}

static void Widen(uint32_t *low, uint32_t *high, uint32_t value, bool first) {
    *low = first || value < *low ? value : *low;
    *high = first || value > *high ? value : *high;
}

void TouchShapeAdd(TouchShape *shape, const TouchFrame *frame) {
    const uint32_t count = frame->count < TOUCH_MAX ? frame->count : TOUCH_MAX;
    for (uint32_t i = 0; i < count; ++i) {
        const TouchPoint *const p = &frame->points[i];
        const bool first = shape->touches == 0u;
        Widen(&shape->min_diameter_x, &shape->max_diameter_x, p->diameter_x, first);
        Widen(&shape->min_diameter_y, &shape->max_diameter_y, p->diameter_y, first);
        Widen(&shape->min_angle, &shape->max_angle, p->angle, first);
        ++shape->touches;
        bool known = false;
        for (uint32_t k = 0; k < shape->id_count; ++k) {
            known = known || shape->ids[k] == p->id;
        }
        if (!known && shape->id_count < sizeof(shape->ids) / sizeof(shape->ids[0])) {
            shape->ids[shape->id_count++] = p->id;
        }
    }
}

bool TouchShapeNote(const TouchShape *shape, char *out, size_t out_size) {
    if (shape->touches == 0u) {
        return false;
    }
    char ids[64] = "";
    size_t used = 0;
    for (uint32_t k = 0; k < shape->id_count && used < sizeof(ids); ++k) {
        const int wrote = snprintf(ids + used, sizeof(ids) - used, "%s%u", k > 0u ? " " : "",
                                   (unsigned)shape->ids[k]);
        used = wrote < 0 ? sizeof(ids) : used + (size_t)wrote;
    }
    snprintf(out, out_size,
             "touch: %u finger states read; diameter %u..%u across and %u..%u down, angle "
             "%u..%u; ids %s",
             (unsigned)shape->touches, (unsigned)shape->min_diameter_x,
             (unsigned)shape->max_diameter_x, (unsigned)shape->min_diameter_y,
             (unsigned)shape->max_diameter_y, (unsigned)shape->min_angle,
             (unsigned)shape->max_angle, ids);
    return true;
}

bool TouchNobodyThere(uint32_t unanswered_in_a_row) {
    return unanswered_in_a_row >= TOUCH_UNANSWERED_LIMIT;
}
