// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <string.h>
#include <switch.h>

#include "../binderref.h"
#include "../check.h"
#include "../report.h"
#include "tests.h"

// The buffer producer behind a layer, asked for its buffer history (transaction 17) after a
// number of frames were queued to it. The suite opens one layer of its own on the default
// display, with a window and two small buffers; each case queues frames up to its count, sends
// the transaction and keeps the raw reply. The program's own window, which the screens draw
// into, is never touched, and the layer is closed before the suite returns, so the screen is as
// it was. One layer for the three cases, since asking the console for four managed layers might
// be refused for reasons that have nothing to do with the history.

static const char *const SUITE = "binder";
static const char *const OPENS = "a layer of its own opens and takes a window";

#define SIZE 64u

typedef struct {
    ViDisplay display;
    ViLayer layer;
    NWindow window;
    Framebuffer buffers;
    bool display_up, layer_up, window_up, buffers_up;
} Layer;

static Layer layer;
static uint32_t queued;
static uint8_t reply[BINDER_REPLY_MAX];
static uint32_t settled;

static void Close(Layer *l) {
    if (l->buffers_up) {
        framebufferClose(&l->buffers);
    }
    if (l->window_up) {
        nwindowClose(&l->window);
    }
    if (l->layer_up) {
        viCloseLayer(&l->layer);
    }
    if (l->display_up) {
        viCloseDisplay(&l->display);
    }
    memset(l, 0, sizeof(*l));
}

static bool Open(Layer *l, char *why, size_t why_size) {
    memset(l, 0, sizeof(*l));
    const char *step = "viOpenDefaultDisplay";
    Result rc = viOpenDefaultDisplay(&l->display);
    l->display_up = R_SUCCEEDED(rc);
    if (l->display_up) {
        step = "viCreateLayer";
        rc = viCreateLayer(&l->display, &l->layer);
        l->layer_up = R_SUCCEEDED(rc);
    }
    if (l->layer_up) {
        step = "nwindowCreateFromLayer";
        rc = nwindowCreateFromLayer(&l->window, &l->layer);
        l->window_up = R_SUCCEEDED(rc);
    }
    if (l->window_up) {
        step = "framebufferCreate";
        rc = framebufferCreate(&l->buffers, &l->window, SIZE, SIZE, PIXEL_FORMAT_RGBA_8888, 2);
        l->buffers_up = R_SUCCEEDED(rc);
    }
    if (!l->buffers_up) {
        snprintf(why, why_size, "%s failed: 0x%x", step, (unsigned)rc);
        Close(l);
        return false;
    }
    // Never the program's own window, whose binder the screens draw through.
    if (l->window.bq.id == nwindowGetDefault()->bq.id) {
        snprintf(why, why_size, "the new layer's producer is the program's own, binder %d",
                 (int)l->window.bq.id);
        Close(l);
        return false;
    }
    snprintf(why, why_size, "layer %llu, binder %d (the program's window is binder %d)",
             (unsigned long long)l->layer.layer_id, (int)l->window.bq.id,
             (int)nwindowGetDefault()->bq.id);
    return true;
}

/// Dequeues and queues @p frames black frames, without the library's framebuffer calls, which
/// end the program on a failed queue rather than say so.
static bool Queue(Layer *l, uint32_t frames, char *why, size_t why_size) {
    for (uint32_t i = 0; i < frames; ++i) {
        s32 slot = -1;
        Result rc = nwindowDequeueBuffer(&l->window, &slot, NULL);
        if (R_SUCCEEDED(rc)) {
            uint8_t *const pixels = (uint8_t *)l->buffers.buf + (size_t)slot * l->buffers.fb_size;
            memset(pixels, 0, l->buffers.fb_size);
            armDCacheFlush(pixels, l->buffers.fb_size);
            rc = nwindowQueueBuffer(&l->window, slot, NULL);
        }
        if (R_FAILED(rc)) {
            snprintf(why, why_size, "frame %u of %u did not queue: 0x%x", (unsigned)(i + 1u),
                     (unsigned)frames, (unsigned)rc);
            return false;
        }
        ++queued;
    }
    return true;
}

/// Sends transaction 17 with @p request and keeps the whole reply parcel, as the library would
/// send it: the interface token and the count, after a header of four words.
static bool AskHistory(Layer *l, int32_t request, char *why, size_t why_size) {
    Parcel parcel;
    parcelCreate(&parcel);
    parcelWriteInterfaceToken(&parcel, "android.gui.IGraphicBufferProducer");
    parcelWriteInt32(&parcel, request);
    static uint8_t in[sizeof(ParcelHeader) + PARCEL_MAX_PAYLOAD];
    const ParcelHeader header = {
        .payload_size = parcel.payload_size,
        .payload_off = sizeof(ParcelHeader),
        .objects_size = 0,
        .objects_off = sizeof(ParcelHeader) + parcel.payload_size,
    };
    memcpy(in, &header, sizeof(header));
    memcpy(in + sizeof(header), parcel.payload, parcel.payload_size);
    memset(reply, 0, sizeof(reply));
    const Result rc =
        binderTransactParcel(&l->window.bq, BINDER_TRANSACTION_HISTORY, in,
                             sizeof(header) + parcel.payload_size, reply, sizeof(reply), 0);
    if (R_FAILED(rc)) {
        snprintf(why, why_size, "the transaction failed: 0x%x", (unsigned)rc);
        return false;
    }
    return true;
}

static void RunCase(BinderCaseId id, const char *name) {
    char why[200];
    static char detail[1400];
    // Frames add up on the one layer: queue what the case still needs, so a case skipped before
    // it changes nothing.
    const uint32_t frames = BinderCaseFrames(id);
    if (frames < queued) {
        snprintf(why, sizeof(why), "%u frames queued already, the case wants %u",
                 (unsigned)queued, (unsigned)frames);
        ReportCase(SUITE, name, CHECK_EXACT, false, why);
        return;
    }
    if (!Queue(&layer, frames - queued, why, sizeof(why)) ||
        !AskHistory(&layer, BinderCaseRequest(id), why, sizeof(why))) {
        ReportCase(SUITE, name, CHECK_EXACT, false, why);
        return;
    }
    uint32_t answers = 0;
    const bool passed = BinderJudge(id, reply, sizeof(reply), detail, sizeof(detail), &answers);
    if (passed) {
        settled |= answers;
    }
    // The newest-first case judges an order; the others, sizes and words.
    ReportCase(SUITE, name, id == BINDER_NEWEST_FIRST ? CHECK_POSITION : CHECK_EXACT, passed,
               detail);
}

void RunBinder(void) {
    settled = 0;
    queued = 0;
    bool open = false;
    if (ReportStart(SUITE, OPENS)) {
        char why[200];
        open = Open(&layer, why, sizeof(why));
        ReportCase(SUITE, OPENS, CHECK_NONE, open, why);
    }
    for (uint32_t i = 0; i < BINDER_CASES; ++i) {
        const char *const name = BinderCaseName((BinderCaseId)i);
        if (!open) {
            ReportSkip(SUITE, name, REPORT_REASON_DEPENDS, "no layer of its own to ask");
            continue;
        }
        if (ReportStart(SUITE, name)) {
            RunCase((BinderCaseId)i, name);
        }
    }
    if (open) {
        Close(&layer);
    }
    if (settled != 0u) {
        ReportNote("binder: the passing cases answer Q1 %s",
                   settled == 3u ? "A and B, cases disagree" : settled == 1u ? "A" : "B");
    }
}
