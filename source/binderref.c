// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "binderref.h"

#include <stdio.h>
#include <string.h>

static uint32_t U32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t U64(const uint8_t *p) {
    return (uint64_t)U32(p) | (uint64_t)U32(p + 4) << 32;
}

void BinderParse(const uint8_t *raw, size_t size, BinderReply *out) {
    memset(out, 0, sizeof(*out));
    out->layout = BINDER_UNKNOWN;
    if (size < 16u) {
        return;
    }
    out->payload_size = U32(raw);
    out->payload_off = U32(raw + 4);
    out->objects_size = U32(raw + 8);
    out->objects_off = U32(raw + 12);
    out->framed = out->payload_off >= 16u && out->payload_off <= size &&
                  out->payload_size <= size - out->payload_off;
    if (!out->framed) {
        return;
    }
    const uint8_t *const p = raw + out->payload_off;
    const uint32_t n = out->payload_size;
    out->payload = p;
    if (n % 4u != 0u) {
        return;
    }
    if (n == 4u && U32(p) == 0u) {
        out->layout = BINDER_STATUS_ONLY;
    } else if (n == 12u && (int32_t)U32(p) == BINDER_BAD_VALUE && U32(p + 4) == 0u &&
               U32(p + 8) == 0u) {
        out->layout = BINDER_REFUSED;
    } else if (n >= 12u && (n - 12u) % BINDER_ENTRY_SIZE == 0u && U32(p) == 0u &&
               U32(p + n - 4u) == 0u && U32(p + 4) == (n - 12u) / BINDER_ENTRY_SIZE &&
               U32(p + 4) <= BINDER_ENTRIES_MAX) {
        out->layout = BINDER_HISTORY;
        out->count = U32(p + 4);
        for (uint32_t i = 0; i < out->count; ++i) {
            const uint8_t *const e = p + 8u + i * BINDER_ENTRY_SIZE;
            out->frame[i] = U64(e);
            out->queued[i] = (int64_t)U64(e + 8);
            out->presented[i] = (int64_t)U64(e + 16);
            out->state[i] = U32(e + 24);
        }
    }
}

static const char *const CASE_NAMES[BINDER_CASES] = {
    "a request for nought, before any frame",
    "the history after three frames, eight asked for",
    "the history after ten frames comes newest first, eight at most",
};

const char *BinderCaseName(BinderCaseId id) {
    return id < BINDER_CASES ? CASE_NAMES[id] : "?";
}

uint32_t BinderCaseFrames(BinderCaseId id) {
    return id == BINDER_AFTER_THREE ? 3u : id == BINDER_NEWEST_FIRST ? 10u : 0u;
}

int32_t BinderCaseRequest(BinderCaseId id) {
    return id == BINDER_ZERO ? 0 : 8;
}

/// The reply's header and payload as the line carries them, in hex.
static size_t Bytes(const BinderReply *r, char *out, size_t size) {
    size_t n = (size_t)snprintf(out, size, "reply: payload %u bytes at %u, objects %u at %u",
                                (unsigned)r->payload_size, (unsigned)r->payload_off,
                                (unsigned)r->objects_size, (unsigned)r->objects_off);
    if (r->payload != NULL && n < size) {
        n += (size_t)snprintf(out + n, size - n, ":");
        for (uint32_t i = 0; i < r->payload_size && n < size; ++i) {
            n += (size_t)snprintf(out + n, size - n, "%s%02x", i % 4u == 0u ? " " : "",
                                  r->payload[i]);
        }
    }
    return n < size ? n : size - 1u;
}

static bool Newest(const BinderReply *r) {
    for (uint32_t i = 1; i < r->count; ++i) {
        if (r->frame[i] >= r->frame[i - 1u]) {
            return false;
        }
    }
    return true;
}

bool BinderJudge(BinderCaseId id, const uint8_t *raw, size_t size, char *out, size_t out_size,
                 uint32_t *answers) {
    *answers = 0;
    BinderReply r;
    BinderParse(raw, size, &r);
    char bytes[1200];
    Bytes(&r, bytes, sizeof(bytes));
    const uint32_t want = BinderCaseFrames(id) < 8u ? BinderCaseFrames(id) : 8u;
    bool passed = false;
    if (r.layout == BINDER_STATUS_ONLY) {
        passed = true;
        *answers = 1u;
        snprintf(out, out_size, "the status alone (Q1 A)%s; %s",
                 id == BINDER_ZERO ? "" : ", no frames to count or order", bytes);
    } else if (r.layout == BINDER_HISTORY && id != BINDER_ZERO) {
        passed = r.count == want && Newest(&r);
        *answers = passed ? 2u : 0u;
        snprintf(out, out_size, "%u entries of %u (Q1 B), frames %llu down to %llu%s; %s",
                 (unsigned)r.count, (unsigned)want, (unsigned long long)r.frame[0],
                 (unsigned long long)(r.count > 0u ? r.frame[r.count - 1u] : 0u),
                 Newest(&r) ? ", newest first" : ", not newest first", bytes);
    } else if (r.layout == BINDER_REFUSED && id == BINDER_ZERO) {
        passed = true;
        *answers = 2u;
        snprintf(out, out_size, "refused with BadValue and a count of nought (Q1 B); %s", bytes);
    } else {
        snprintf(out, out_size, "%s; %s",
                 !r.framed                       ? "a reply whose header points outside it"
                 : r.layout == BINDER_HISTORY    ? "a history for a request of nought"
                 : r.layout == BINDER_REFUSED    ? "refused a request of eight"
                                                 : "a reply in neither layout known, the status "
                                                   "alone or the history",
                 bytes);
    }
    return passed;
}
