// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// The svc suite's answers: the kernel's result codes by name, and how a case's line names the
/// one it got against the one it wanted.
///
/// Touches no system call, so the reference suite pins all of it with no kernel present.

/// The kernel's results the suite expects or meets, as the console's kernel encodes them: module
/// 1 in the low nine bits and the description above (svc::Result in Atmosphere's mesosphere,
/// switchbrew's Error codes page).
typedef enum {
    SVC_SUCCESS = 0,
    SVC_INVALID_SIZE = 101,
    SVC_INVALID_ADDRESS = 102,
    SVC_OUT_OF_MEMORY = 104,
    SVC_INVALID_CURRENT_MEMORY = 106,
    SVC_INVALID_HANDLE = 114,
    SVC_INVALID_POINTER = 115,
    SVC_INVALID_COMBINATION = 116,
    SVC_TIMED_OUT = 117,
    SVC_CANCELLED = 118,
    SVC_OUT_OF_RANGE = 119,
    SVC_INVALID_ENUM_VALUE = 120,
    SVC_INVALID_STATE = 125,
    SVC_LIMIT_REACHED = 132,
} SvcResult;

/// The result code the kernel returns for @p result: nought for success.
uint32_t SvcCode(SvcResult result);

/// The name of the kernel result @p code, "Success" for nought, NULL for one this file does not
/// know.
const char *SvcName(uint32_t code);

/// Writes "TimedOut (0xea01)" for @p code into @p out, or "0x<code>" for a code SvcName does not
/// know, so a line names what the machine answered.
void SvcDescribe(uint32_t code, char *out, uint32_t size);

/// The memory types and permissions svcQueryMemory reports, as the console's kernel numbers them
/// (svc::MemoryState and svc::MemoryPermission).
#define SVC_MEMORY_CODE_STATIC 0x03u
#define SVC_MEMORY_HEAP 0x05u
#define SVC_MEMORY_STACK 0x0Bu
#define SVC_PERMISSION_NONE 0u
#define SVC_PERMISSION_READ 1u
#define SVC_PERMISSION_READ_WRITE 3u
#define SVC_PERMISSION_READ_EXECUTE 5u
/// The attribute svcMapMemory sets on its source: locked, which libnx calls borrowed.
#define SVC_ATTRIBUTE_LOCKED 1u

/// The most handles one svcWaitSynchronization takes.
#define SVC_WAIT_HANDLES_MAX 64u
/// The heap's size goes up in steps of this many bytes.
#define SVC_HEAP_ALIGNMENT 0x200000u

#ifdef __cplusplus
}
#endif
