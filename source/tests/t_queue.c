// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <string.h>
#include <switch.h>

#include "../check.h"
#include "../gpu.h"
#include "../report.h"
#include "tests.h"

// A queue that waits on a word in memory until another queue, or the processor, writes it: the
// 3D class's semaphore acquire, which a game's driver uses to order work between its queues and
// an emulator runs on threads of its own, or one after the other. Each case submits a list that
// waits on the word and then copies a buffer with the copy engine, checks the copy has not
// happened while the word is short of what it waits for, releases it, and checks the copy
// happened, and after what.
//
// No case can hang the run: the processor waits for the waiting list's fence a second at most,
// and when the release it expected never came writes the word itself, so the list goes on and
// the device can be closed; the case then fails and says so. A machine that runs the list
// without waiting shows as a copy made too early.

static const char *const SUITE = "queue";

#define WORDS 256u
#define BYTES (WORDS * 4u)
/// How long the processor leaves a waiting list before it looks whether the copy was made.
#define HOLD_NS 50000000ull
/// How long it waits for the list's fence once the word is released.
#define FENCE_NS 1000000000ll
#define OLD 0xeeeeeeeeu

typedef struct {
    GpuContext gpu;
    /// The second queue, with a command list and command memory of its own.
    DkQueue other;
    DkCmdBuf other_cmdbuf;
    DkMemBlock other_memory;
    /// The word, in a block of its own: the library wants a variable uncached on both sides.
    DkMemBlock word_memory;
    DkVariable word;
    /// A list never came back even after the processor wrote its word: closing the device would
    /// wait for it forever, so the device is left open.
    bool stuck;
} QueueContext;

static QueueContext context;

typedef struct {
    uint32_t *cpu;
    DkGpuAddr gpu;
} Buffer;

static bool Alloc(QueueContext *x, Buffer *b, uint32_t fill) {
    b->cpu = (uint32_t *)GpuAllocData(&x->gpu, BYTES, 256u, &b->gpu);
    if (b->cpu == NULL) {
        return false;
    }
    for (uint32_t i = 0; i < WORDS; ++i) {
        b->cpu[i] = fill == OLD ? OLD : fill + i;
    }
    return true;
}

/// Whether every word of @p b is @p fill's pattern.
static bool Holds(const Buffer *b, uint32_t fill) {
    for (uint32_t i = 0; i < WORDS; ++i) {
        if (b->cpu[i] != (fill == OLD ? OLD : fill + i)) {
            return false;
        }
    }
    return true;
}

/// Submits on the first queue a list that waits until the word compares to @p value by @p op,
/// copies @p from to @p to, and signals @p fence once the copy is in memory.
static void SubmitWaiter(QueueContext *x, DkVarCompareOp op, uint32_t value, const Buffer *from,
                         const Buffer *to, DkFence *fence) {
    dkCmdBufClear(x->gpu.cmdbuf);
    dkCmdBufWaitVariable(x->gpu.cmdbuf, &x->word, op, value);
    dkCmdBufCopyBuffer(x->gpu.cmdbuf, from->gpu, to->gpu, BYTES);
    dkCmdBufSignalFence(x->gpu.cmdbuf, fence, true);
    dkQueueSubmitCommands(x->gpu.queue, dkCmdBufFinishList(x->gpu.cmdbuf));
    dkQueueFlush(x->gpu.queue);
}

/// Waits for @p fence a second at most; when it does not come, writes the word to @p release
/// so the list goes on, and waits for it again. False when it did not come the first time.
static bool Finish(QueueContext *x, DkFence *fence, uint32_t release) {
    if (dkFenceWait(fence, FENCE_NS) == DkResult_Success) {
        return true;
    }
    dkVariableSignal(&x->word, DkVarOp_Set, release);
    if (dkFenceWait(fence, FENCE_NS) != DkResult_Success) {
        x->stuck = true;
    }
    return false;
}

/// Whether the case @p name cannot run because a list before it never came back; its skip line is
/// then written.
static bool Stuck(QueueContext *x, const char *name) {
    if (x->stuck) {
        ReportSkip(SUITE, name, REPORT_REASON_DEPENDS,
                   "a list before it never came back, even after its word was written");
    }
    return x->stuck;
}

static void SetWord(QueueContext *x, uint32_t value) {
    dkVariableSignal(&x->word, DkVarOp_Set, value);
}

/// The processor releases a list waiting for the word to reach @p wanted (@p op), after setting
/// it to @p short_value, which must hold the list back.
static void ProcessorCase(QueueContext *x, const char *name, DkVarCompareOp op, uint32_t wanted,
                          uint32_t short_value, const char *what) {
    if (Stuck(x, name) || !ReportStart(SUITE, name)) {
        return;
    }
    Buffer from, to;
    if (!Alloc(x, &from, 0x51000000u) || !Alloc(x, &to, OLD)) {
        ReportCase(SUITE, name, CHECK_NONE, false, "no scratch left for the buffers");
        return;
    }
    SetWord(x, 0);
    DkFence fence = { 0 };
    SubmitWaiter(x, op, wanted, &from, &to, &fence);
    SetWord(x, short_value);
    svcSleepThread(HOLD_NS);
    const bool held = Holds(&to, OLD);
    SetWord(x, wanted);
    const bool came = Finish(x, &fence, wanted);
    const bool copied = Holds(&to, 0x51000000u);
    CHECK_TRUE(SUITE, name, CHECK_EXACT, held && came && copied,
               "%s: %s while the word was %u, %s once it was %u, and the copy %s", what,
               held ? "held" : "the copy was made", (unsigned)short_value,
               came ? "its fence came" : "no fence within a second", (unsigned)wanted,
               copied ? "landed" : "is not in memory");
}

/// A list on the second queue writes a buffer and then sets the word, which releases the first
/// queue's list; the copy that list makes must be of what the second queue wrote.
static void QueueCase(QueueContext *x) {
    static const char *const NAME =
        "another queue's signal releases the wait after that queue's own copy";
    if (Stuck(x, NAME) || !ReportStart(SUITE, NAME)) {
        return;
    }
    Buffer pattern, middle, to;
    if (!Alloc(x, &pattern, 0x62000000u) || !Alloc(x, &middle, OLD) || !Alloc(x, &to, OLD)) {
        ReportCase(SUITE, NAME, CHECK_NONE, false, "no scratch left for the buffers");
        return;
    }
    SetWord(x, 0);
    DkFence first = { 0 }, second = { 0 };
    SubmitWaiter(x, DkVarCompareOp_Sequential, 1, &middle, &to, &first);
    svcSleepThread(HOLD_NS);
    const bool held = Holds(&to, OLD);
    // The second queue copies the pattern into the middle buffer, waits for the copy, and only
    // then sets the word: what the first list copies out of the middle is the pattern.
    dkCmdBufClear(x->other_cmdbuf);
    dkCmdBufCopyBuffer(x->other_cmdbuf, pattern.gpu, middle.gpu, BYTES);
    dkCmdBufBarrier(x->other_cmdbuf, DkBarrier_Full, 0);
    dkCmdBufSignalVariable(x->other_cmdbuf, &x->word, DkVarOp_Set, 1, DkPipelinePos_Bottom);
    dkCmdBufSignalFence(x->other_cmdbuf, &second, true);
    dkQueueSubmitCommands(x->other, dkCmdBufFinishList(x->other_cmdbuf));
    dkQueueFlush(x->other);
    const bool came = Finish(x, &first, 1);
    dkFenceWait(&second, FENCE_NS);
    const bool copied = Holds(&to, 0x62000000u);
    const bool stale = Holds(&to, OLD);
    CHECK_TRUE(SUITE, NAME, CHECK_EXACT, held && came && copied,
               "the first queue's list %s before the second queue ran; %s; the copy holds %s",
               held ? "held" : "copied",
               came ? "the second queue's signal released it"
                    : "no release from the second queue within a second",
               copied  ? "the second queue's pattern"
               : stale ? "the middle buffer before the second queue wrote it"
                       : "neither the pattern nor the buffer's old words");
}

static void SatisfiedCase(QueueContext *x) {
    static const char *const NAME = "a wait the word already satisfies passes at once";
    if (Stuck(x, NAME) || !ReportStart(SUITE, NAME)) {
        return;
    }
    Buffer from, to;
    if (!Alloc(x, &from, 0x73000000u) || !Alloc(x, &to, OLD)) {
        ReportCase(SUITE, NAME, CHECK_NONE, false, "no scratch left for the buffers");
        return;
    }
    SetWord(x, 5);
    DkFence fence = { 0 };
    SubmitWaiter(x, DkVarCompareOp_Sequential, 3, &from, &to, &fence);
    const bool came = Finish(x, &fence, 5);
    const bool copied = Holds(&to, 0x73000000u);
    CHECK_TRUE(SUITE, NAME, CHECK_EXACT, came && copied,
               "the word at 5, the wait for 3 or more: %s, and the copy %s",
               came ? "its fence came" : "no fence within a second",
               copied ? "landed" : "is not in memory");
}

static bool Open(QueueContext *x) {
    static const char *const NAME =
        "graphics device opens with a second queue and a word to wait on";
    if (!ReportStart(SUITE, NAME)) {
        return false;
    }
    GpuSpec spec;
    GpuSpecDefaults(&spec, 16u, 16u);
    spec.data_size = 256u * 1024u;
    bool ok = GpuOpenSpec(&x->gpu, &spec);
    const char *why = x->gpu.failure;
    if (ok) {
        DkQueueMaker queue_maker;
        dkQueueMakerDefaults(&queue_maker, x->gpu.device);
        queue_maker.flags = DkQueueFlags_Graphics;
        x->other = dkQueueCreate(&queue_maker);
        DkMemBlockMaker block_maker;
        dkMemBlockMakerDefaults(&block_maker, x->gpu.device, DK_QUEUE_MIN_CMDMEM_SIZE);
        block_maker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
        x->other_memory = x->other != NULL ? dkMemBlockCreate(&block_maker) : NULL;
        DkCmdBufMaker cmdbuf_maker;
        dkCmdBufMakerDefaults(&cmdbuf_maker, x->gpu.device);
        x->other_cmdbuf = x->other_memory != NULL ? dkCmdBufCreate(&cmdbuf_maker) : NULL;
        if (x->other_cmdbuf != NULL) {
            dkCmdBufAddMemory(x->other_cmdbuf, x->other_memory, 0, DK_QUEUE_MIN_CMDMEM_SIZE);
        }
        DkMemBlockMaker word_maker;
        dkMemBlockMakerDefaults(&word_maker, x->gpu.device, DK_MEMBLOCK_ALIGNMENT);
        word_maker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuUncached;
        x->word_memory = x->other_cmdbuf != NULL ? dkMemBlockCreate(&word_maker) : NULL;
        ok = x->word_memory != NULL;
        why = x->other == NULL          ? "dkQueueCreate made no second queue"
              : x->other_memory == NULL ? "no command memory for the second queue"
              : x->other_cmdbuf == NULL ? "no command list for the second queue"
              : x->word_memory == NULL  ? "no memory for the word"
                                        : "";
        if (ok) {
            dkVariableInitialize(&x->word, x->word_memory, 0);
        }
    }
    CHECK_TRUE(SUITE, NAME, CHECK_NONE, ok, "%s", why);
    return ok;
}

static void Close(QueueContext *x) {
    if (x->stuck) {
        ReportNote("queue: a list never came back after its word was written: the device is left "
                   "open, since closing it would wait for that list");
        return;
    }
    if (x->other != NULL) {
        dkQueueDestroy(x->other);
        x->other = NULL;
    }
    if (x->other_cmdbuf != NULL) {
        dkCmdBufDestroy(x->other_cmdbuf);
        x->other_cmdbuf = NULL;
    }
    if (x->other_memory != NULL) {
        dkMemBlockDestroy(x->other_memory);
        x->other_memory = NULL;
    }
    // After the device's close, whose last copy is ordered behind every list that waited on it.
    GpuClose(&x->gpu);
    if (x->word_memory != NULL) {
        dkMemBlockDestroy(x->word_memory);
        x->word_memory = NULL;
    }
}

void RunQueue(void) {
    QueueContext *const x = &context;
    memset(x, 0, sizeof(*x));
    if (!Open(x)) {
        Close(x);
        return;
    }
    ProcessorCase(x, "a wait for the word to reach 1 holds the list until the processor writes 1",
                  DkVarCompareOp_Sequential, 1, 0, "a wait for 1 or more");
    ProcessorCase(x, "a wait for the word to equal 2 holds the list at 1 and goes on at 2",
                  DkVarCompareOp_Equal, 2, 1, "a wait for exactly 2");
    QueueCase(x);
    SatisfiedCase(x);
    Close(x);
}
