// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "../check.h"
#include "../report.h"
#include "../svcref.h"
#include "tests.h"

// The kernel's answers to the supervisor calls a game makes around its handles, its events and
// its memory, where the console's kernel decides each result and an emulator's kernel can return
// another: svcWaitSynchronization with no handles, too many, or one that names nothing to wait
// on; which of two signaled events it names; a cancel pending before a wait; resetting, clearing
// and signaling an event by the wrong end; closing a handle twice or a pseudo-handle; what
// svcQueryMemory says of the heap and the program; a heap size off its step; and svcMapMemory's
// view, its locked source and what it leaves there.
//
// Every case compares the kernel's result code, and where there is one the index or the memory
// state, with what the console's kernel answers (Atmosphere's mesosphere, which every result here
// follows, and libnx's own notes): exact. svcref.c names the codes, pinned by the reference
// suite. Only the word kernel runs the suite, as it runs sched.

static const char *const SUITE = "svc";
static const char *const OPEN = "two events open and a handle is closed";

/// Two events, each a writable and a readable end, and a handle that was closed.
static Handle writable[2], readable[2], closed_handle;
static bool events_open;

static uint32_t Code(Result rc) {
    return (uint32_t)rc;
}

/// Holds @p got to @p want, both result codes, and @p extra true; the line names both codes and
/// adds @p what.
static void Expect(const char *name, Result got, SvcResult want, bool extra, const char *what) {
    char got_text[48], want_text[48];
    SvcDescribe(Code(got), got_text, sizeof(got_text));
    SvcDescribe(SvcCode(want), want_text, sizeof(want_text));
    CHECK_TRUE(SUITE, name, CHECK_EXACT, Code(got) == SvcCode(want) && extra, "%s, expected %s; %s",
               got_text, want_text, what);
}

static uint64_t Microseconds(uint64_t ticks) {
    return armTicksToNs(ticks) / 1000u;
}

static void ClearBoth(void) {
    svcClearEvent(writable[0]);
    svcClearEvent(writable[1]);
}

// ---- waits

static void NoHandlesSleep(void) {
    static const char *const NAME =
        "svcWaitSynchronization with no handles sleeps out its timeout and answers TimedOut";
    if (!ReportStart(SUITE, NAME)) {
        return;
    }
    s32 index = -1;
    const uint64_t start = armGetSystemTick();
    const Result rc = svcWaitSynchronization(&index, NULL, 0, 5000000u);
    const uint64_t us = Microseconds(armGetSystemTick() - start);
    char what[96];
    snprintf(what, sizeof(what), "it returned after %llu us of a 5000 us timeout (4500 or more)",
             (unsigned long long)us);
    Expect(NAME, rc, SVC_TIMED_OUT, us >= 4500u, what);
}

static void NoHandlesNow(void) {
    static const char *const NAME =
        "svcWaitSynchronization with no handles and a timeout of 0 answers TimedOut";
    if (!ReportStart(SUITE, NAME)) {
        return;
    }
    s32 index = -1;
    Expect(NAME, svcWaitSynchronization(&index, NULL, 0, 0), SVC_TIMED_OUT, true,
           "no handles, a timeout of 0");
}

static void TooMany(void) {
    static const char *const NAME = "svcWaitSynchronization of 65 handles answers OutOfRange";
    if (!ReportStart(SUITE, NAME)) {
        return;
    }
    Handle handles[SVC_WAIT_HANDLES_MAX + 1u];
    for (uint32_t i = 0; i <= SVC_WAIT_HANDLES_MAX; ++i) {
        handles[i] = readable[0];
    }
    s32 index = -1;
    Expect(NAME, svcWaitSynchronization(&index, handles, SVC_WAIT_HANDLES_MAX + 1u, 0),
           SVC_OUT_OF_RANGE, true, "65 copies of a readable event's handle, a timeout of 0");
}

/// A wait on a readable event and @p bad, which names nothing to wait on.
static void BadHandle(const char *name, Handle bad, const char *what) {
    if (!ReportStart(SUITE, name)) {
        return;
    }
    const Handle handles[2] = { readable[0], bad };
    s32 index = -1;
    Expect(name, svcWaitSynchronization(&index, handles, 2, 0), SVC_INVALID_HANDLE, true, what);
}

static void LowestIndex(void) {
    static const char *const NAME =
        "of two signaled events, svcWaitSynchronization names the first";
    if (!ReportStart(SUITE, NAME)) {
        return;
    }
    svcSignalEvent(writable[1]);
    svcSignalEvent(writable[0]);
    s32 index = -1;
    const Result rc = svcWaitSynchronization(&index, readable, 2, 0);
    ClearBoth();
    char what[64];
    snprintf(what, sizeof(what), "index %d, expected 0", (int)index);
    Expect(NAME, rc, SVC_SUCCESS, index == 0, what);
}

static void SecondIndex(void) {
    static const char *const NAME =
        "with only the second of two events signaled, svcWaitSynchronization names it";
    if (!ReportStart(SUITE, NAME)) {
        return;
    }
    svcSignalEvent(writable[1]);
    s32 index = -1;
    const Result rc = svcWaitSynchronization(&index, readable, 2, 0);
    ClearBoth();
    char what[64];
    snprintf(what, sizeof(what), "index %d, expected 1", (int)index);
    Expect(NAME, rc, SVC_SUCCESS, index == 1, what);
}

/// A cancel made before the waits: a timeout of 0 times out and leaves it pending, the next wait
/// with a timeout ends at once with Cancelled and uses it up, and the one after that times out.
static void Cancels(void) {
    static const char *const ZERO =
        "a pending cancel meets a timeout of 0 with TimedOut and stays pending";
    static const char *const NEXT =
        "the pending cancel ends the next wait at once with Cancelled and is used up";
    if (!ReportStart(SUITE, ZERO)) {
        ReportSkip(SUITE, NEXT, REPORT_REASON_DEPENDS,
                   "the case before it, which sets up the cancel, did not run");
        return;
    }
    const Result cancel = svcCancelSynchronization(threadGetCurHandle());
    s32 index = -1;
    const Result first = svcWaitSynchronization(&index, readable, 1, 0);
    char what[192];
    snprintf(what, sizeof(what),
             "svcCancelSynchronization of the thread's own handle answered 0x%x",
             (unsigned)Code(cancel));
    Expect(ZERO, first, SVC_TIMED_OUT, Code(cancel) == 0u, what);
    if (!ReportStart(SUITE, NEXT)) {
        return;
    }
    const uint64_t start = armGetSystemTick();
    const Result second = svcWaitSynchronization(&index, readable, 1, 1000000000u);
    const uint64_t us = Microseconds(armGetSystemTick() - start);
    const Result third = svcWaitSynchronization(&index, readable, 1, 1000000u);
    char third_text[48];
    SvcDescribe(Code(third), third_text, sizeof(third_text));
    snprintf(what, sizeof(what),
             "after %llu us of a 1 s timeout (under 500000); the wait after it answered %s",
             (unsigned long long)us, third_text);
    Expect(NEXT, second, SVC_CANCELLED, us < 500000u && Code(third) == SvcCode(SVC_TIMED_OUT),
           what);
}

// ---- events and handles

static void ResetSignal(void) {
    static const char *const NAME = "svcResetSignal clears a signaled event and is InvalidState on "
                                    "a clear one";
    if (!ReportStart(SUITE, NAME)) {
        return;
    }
    svcSignalEvent(writable[0]);
    const Result reset = svcResetSignal(readable[0]);
    s32 index = -1;
    const Result wait = svcWaitSynchronization(&index, readable, 1, 0);
    const Result again = svcResetSignal(readable[0]);
    char reset_text[48], wait_text[48];
    SvcDescribe(Code(reset), reset_text, sizeof(reset_text));
    SvcDescribe(Code(wait), wait_text, sizeof(wait_text));
    char what[160];
    snprintf(what, sizeof(what), "the first reset answered %s and the wait after it %s", reset_text,
             wait_text);
    Expect(NAME, again, SVC_INVALID_STATE,
           Code(reset) == 0u && Code(wait) == SvcCode(SVC_TIMED_OUT), what);
}

static void ClearEvent(void) {
    static const char *const NAME = "svcClearEvent of the writable end clears the event";
    if (!ReportStart(SUITE, NAME)) {
        return;
    }
    svcSignalEvent(writable[0]);
    const Result clear = svcClearEvent(writable[0]);
    s32 index = -1;
    char what[96];
    snprintf(what, sizeof(what), "svcClearEvent answered 0x%x; the wait after it",
             (unsigned)Code(clear));
    Expect(NAME, svcWaitSynchronization(&index, readable, 1, 0), SVC_TIMED_OUT, Code(clear) == 0u,
           what);
}

static void SignalReadable(void) {
    static const char *const NAME = "svcSignalEvent of the readable end answers InvalidHandle";
    if (!ReportStart(SUITE, NAME)) {
        return;
    }
    const Result rc = svcSignalEvent(readable[0]);
    ClearBoth();
    Expect(NAME, rc, SVC_INVALID_HANDLE, true, "the readable end of an event");
}

static void CloseTwice(void) {
    static const char *const NAME = "a handle closed once answers InvalidHandle when closed again";
    if (!ReportStart(SUITE, NAME)) {
        return;
    }
    Handle w = INVALID_HANDLE, r = INVALID_HANDLE;
    const Result made = svcCreateEvent(&w, &r);
    if (R_FAILED(made)) {
        ReportCase(SUITE, NAME, CHECK_NONE, false, "svcCreateEvent failed");
        return;
    }
    const Result first = svcCloseHandle(r);
    const Result second = svcCloseHandle(r);
    svcCloseHandle(w);
    char what[64];
    snprintf(what, sizeof(what), "the first close answered 0x%x", (unsigned)Code(first));
    Expect(NAME, second, SVC_INVALID_HANDLE, Code(first) == 0u, what);
}

static void ClosePseudo(void) {
    static const char *const NAME = "the process's pseudo-handle cannot be closed: InvalidHandle";
    if (!ReportStart(SUITE, NAME)) {
        return;
    }
    Expect(NAME, svcCloseHandle(CUR_PROCESS_HANDLE), SVC_INVALID_HANDLE, true,
           "svcCloseHandle(0xffff8001)");
}

// ---- memory

/// What svcQueryMemory says of @p address: its type, its permission and its attribute.
static Result Query(uintptr_t address, MemoryInfo *info) {
    u32 page = 0;
    memset(info, 0, sizeof(*info));
    return svcQueryMemory(info, &page, address);
}

static void QueryCase(const char *name, uintptr_t address, bool check_type, uint32_t type,
                      uint32_t permission, const char *what) {
    if (!ReportStart(SUITE, name)) {
        return;
    }
    MemoryInfo info;
    const Result rc = Query(address, &info);
    char expected[48] = "";
    if (check_type) {
        snprintf(expected, sizeof(expected), "type 0x%x, ", (unsigned)type);
    }
    char line[192];
    snprintf(line, sizeof(line),
             "%s: type 0x%x, permission %u, attribute %u (expected %spermission %u, attribute 0)",
             what, (unsigned)(info.type & 0xFFu), (unsigned)info.perm, (unsigned)info.attr,
             expected, (unsigned)permission);
    Expect(name, rc, SVC_SUCCESS,
           (check_type ? (info.type & 0xFFu) == type : true) && info.perm == permission &&
               info.attr == 0u,
           line);
}

static const char READ_ONLY[] = "the program's read-only data";

static void HeapStep(void) {
    static const char *const NAME =
        "svcSetHeapSize of a size off the 2 MiB step answers InvalidSize";
    if (!ReportStart(SUITE, NAME)) {
        return;
    }
    u64 base = 0;
    svcGetInfo(&base, InfoType_HeapRegionAddress, CUR_PROCESS_HANDLE, 0);
    MemoryInfo info;
    Query((uintptr_t)base, &info);
    // Larger than the heap as it is, by a page: a machine that took it would grow the heap or
    // run out of memory, never take a page from under the program.
    const uint64_t size = (info.type & 0xFFu) == SVC_MEMORY_HEAP ? info.size + 0x1000u : 0x1000u;
    void *address = NULL;
    char what[96];
    snprintf(what, sizeof(what), "a size of 0x%llx, the heap at 0x%llx", (unsigned long long)size,
             (unsigned long long)info.size);
    Expect(NAME, svcSetHeapSize(&address, size), SVC_INVALID_SIZE, true, what);
}

#define MAP_SIZE 0x2000u
#define MAP_WORDS (MAP_SIZE / sizeof(uint32_t))

/// svcMapMemory of two heap pages onto a free place in the stack region, as libnx maps a
/// thread's stack: the source locked with no permission while it is mapped, the view stack
/// memory to read and write, and what the view held in the source once it is unmapped.
static void MapMemory(void) {
    static const char *const LOCKED =
        "svcMapMemory locks its source with no permission and maps a stack view to read and write";
    static const char *const BACK =
        "what is written through svcMapMemory's view is in its source once unmapped";
    if (!ReportStart(SUITE, LOCKED)) {
        return;
    }
    uint32_t *const source = (uint32_t *)aligned_alloc(0x1000u, MAP_SIZE);
    if (source == NULL) {
        ReportCase(SUITE, LOCKED, CHECK_NONE, false, "no heap pages for the source");
        return;
    }
    for (uint32_t i = 0; i < MAP_WORDS; ++i) {
        source[i] = 0x51000000u | i;
    }
    virtmemLock();
    uint32_t *const view = (uint32_t *)virtmemFindStack(MAP_SIZE, 0x1000u);
    const Result mapped = view != NULL ? svcMapMemory(view, source, MAP_SIZE) : 0;
    virtmemUnlock();
    if (view == NULL || R_FAILED(mapped)) {
        if (view == NULL) {
            ReportCase(SUITE, LOCKED, CHECK_NONE, false, "no free place in the stack region");
        } else {
            Expect(LOCKED, mapped, SVC_SUCCESS, false, "svcMapMemory of two heap pages refused");
        }
        free(source);
        ReportSkip(SUITE, BACK, REPORT_REASON_DEPENDS, "svcMapMemory refused the mapping");
        return;
    }
    MemoryInfo from, to;
    Query((uintptr_t)source, &from);
    Query((uintptr_t)view, &to);
    // The source is never touched while it is mapped: on the console it has no permission.
    const bool read_through = view[1] == (0x51000000u | 1u);
    char what[192];
    snprintf(what, sizeof(what),
             "the source type 0x%x, permission %u, attribute %u (expected heap, 0, 1); the view "
             "type 0x%x, permission %u (expected 0x%x, 3); the view reads the source's words %s",
             (unsigned)(from.type & 0xFFu), (unsigned)from.perm, (unsigned)from.attr,
             (unsigned)(to.type & 0xFFu), (unsigned)to.perm, (unsigned)SVC_MEMORY_STACK,
             read_through ? "yes" : "no");
    Expect(LOCKED, mapped, SVC_SUCCESS,
           from.perm == SVC_PERMISSION_NONE && from.attr == SVC_ATTRIBUTE_LOCKED &&
               (from.type & 0xFFu) == SVC_MEMORY_HEAP && (to.type & 0xFFu) == SVC_MEMORY_STACK &&
               to.perm == SVC_PERMISSION_READ_WRITE && read_through,
           what);
    for (uint32_t i = 0; i < MAP_WORDS; ++i) {
        view[i] = 0x7E000000u | i;
    }
    const Result unmapped = svcUnmapMemory(view, source, MAP_SIZE);
    if (!ReportStart(SUITE, BACK)) {
        free(source);
        return;
    }
    uint32_t wrong = 0, first = 0;
    for (uint32_t i = 0; R_SUCCEEDED(unmapped) && i < MAP_WORDS; ++i) {
        if (source[i] != (0x7E000000u | i)) {
            first = wrong == 0u ? i : first;
            ++wrong;
        }
    }
    MemoryInfo after;
    Query((uintptr_t)source, &after);
    snprintf(what, sizeof(what),
             "%u of %u words not the view's, the first %u is 0x%08x; the source then has "
             "permission %u and attribute %u (expected 3 and 0)",
             (unsigned)wrong, (unsigned)MAP_WORDS, (unsigned)first,
             R_SUCCEEDED(unmapped) ? (unsigned)source[first] : 0u, (unsigned)after.perm,
             (unsigned)after.attr);
    Expect(BACK, unmapped, SVC_SUCCESS,
           wrong == 0u && after.perm == SVC_PERMISSION_READ_WRITE && after.attr == 0u, what);
    if (R_SUCCEEDED(unmapped)) {
        free(source);
    }
}

void RunSvc(void) {
    if (!ReportStart(SUITE, OPEN)) {
        return;
    }
    Handle w = INVALID_HANDLE, r = INVALID_HANDLE;
    const Result made = svcCreateEvent(&writable[0], &readable[0]);
    const Result made2 = R_SUCCEEDED(made) ? svcCreateEvent(&writable[1], &readable[1]) : made;
    const Result made3 = R_SUCCEEDED(made2) ? svcCreateEvent(&w, &r) : made2;
    const Result closed = R_SUCCEEDED(made3) ? svcCloseHandle(r) : made3;
    closed_handle = r;
    if (R_SUCCEEDED(made3)) {
        svcCloseHandle(w);
    }
    events_open = R_SUCCEEDED(closed);
    CHECK_TRUE(SUITE, OPEN, CHECK_NONE, events_open,
               "svcCreateEvent answered 0x%x, 0x%x and 0x%x, svcCloseHandle 0x%x",
               (unsigned)Code(made), (unsigned)Code(made2), (unsigned)Code(made3),
               (unsigned)Code(closed));
    if (!events_open) {
        return;
    }
    NoHandlesSleep();
    NoHandlesNow();
    TooMany();
    BadHandle("a closed handle among two fails svcWaitSynchronization with InvalidHandle",
              closed_handle, "a readable event and a handle closed before the wait");
    BadHandle("the writable end of an event is no handle to wait on: InvalidHandle", writable[1],
              "a readable event and the writable end of another");
    BadHandle("the current thread's pseudo-handle is no handle to wait on: InvalidHandle",
              CUR_THREAD_HANDLE, "a readable event and 0xffff8000");
    LowestIndex();
    SecondIndex();
    Cancels();
    ResetSignal();
    ClearEvent();
    SignalReadable();
    CloseTwice();
    ClosePseudo();
    u64 heap = 0;
    svcGetInfo(&heap, InfoType_HeapRegionAddress, CUR_PROCESS_HANDLE, 0);
    QueryCase("svcQueryMemory says the heap is heap memory to read and write", (uintptr_t)heap,
              true, SVC_MEMORY_HEAP, SVC_PERMISSION_READ_WRITE, "the heap region's start");
    QueryCase("svcQueryMemory says the program's code is to read and execute", (uintptr_t)&RunSvc,
              false, 0u, SVC_PERMISSION_READ_EXECUTE, "this function");
    QueryCase("svcQueryMemory says the program's read-only data is to read only",
              (uintptr_t)READ_ONLY, false, 0u, SVC_PERMISSION_READ, READ_ONLY);
    HeapStep();
    MapMemory();
    for (uint32_t i = 0; i < 2u; ++i) {
        svcCloseHandle(writable[i]);
        svcCloseHandle(readable[i]);
    }
}
