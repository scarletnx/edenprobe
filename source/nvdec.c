// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "nvdec.h"

#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <switch.h>

#include "vp9ref.h"

// nvtegra asks the multimedia service for module 5, which libnx's header names Nvenc, and the
// highest clock the way the official driver does: INT_MAX kHz in Hz, wrapped to 32 bits.
#define MM_MODULE_NVDEC ((MmuModuleId)5)
#define MM_CLOCK_MAX 0xFFFFFC18u

static uint8_t *memory;
static NvMap map;
static NvChannel channel;
static MmuRequest clock_request;
static uint32_t syncpt;
static uint32_t iova;
static bool nv_up, map_up, fence_up, mapped, channel_up, buffer_up, mm_up, clock_up;

static void DeviceSync(void) {
    __asm__ volatile("dsb sy" ::: "memory");
}

void NvdecDeviceClose(void) {
    if (clock_up) {
        mmuRequestFinalize(&clock_request);
        clock_up = false;
    }
    if (mm_up) {
        mmuExit();
        mm_up = false;
    }
    if (mapped) {
        nvioctl_command_buffer_map unmap = {.handle = map.handle, .iova = iova};
        nvioctlChannel_UnmapCommandBuffer(channel.fd, &unmap, 1, false);
        mapped = false;
    }
    if (channel_up) {
        nvChannelClose(&channel);
        channel_up = false;
    }
    if (buffer_up) {
        nvMapClose(&map);
        buffer_up = false;
    }
    free(memory);
    memory = NULL;
    if (fence_up) {
        nvFenceExit();
        fence_up = false;
    }
    if (map_up) {
        nvMapExit();
        map_up = false;
    }
    if (nv_up) {
        nvExit();
        nv_up = false;
    }
}

/// The clock, asked for once the channel is up. What it got, or why not, goes into @p said.
static void AskClock(char *said, size_t said_size) {
    Result rc = mmuInitialize();
    mm_up = R_SUCCEEDED(rc);
    const char *step = "mmuInitialize";
    if (mm_up) {
        step = "mmuRequestInitialize";
        rc = mmuRequestInitialize(&clock_request, MM_MODULE_NVDEC, 8, false);
        clock_up = R_SUCCEEDED(rc);
    }
    u32 hz = 0;
    if (clock_up) {
        step = "mmuRequestSetAndWait";
        rc = mmuRequestSetAndWait(&clock_request, MM_CLOCK_MAX, -1);
        if (R_SUCCEEDED(rc)) {
            step = "mmuRequestGet";
            rc = mmuRequestGet(&clock_request, &hz);
        }
    }
    if (R_SUCCEEDED(rc)) {
        snprintf(said, said_size, "clock %u Hz", (unsigned)hz);
    } else {
        snprintf(said, said_size, "no clock (%s 0x%x)", step, (unsigned)rc);
    }
}

bool NvdecDeviceOpen(char *why, size_t why_size) {
    Result rc = nvInitialize();
    nv_up = R_SUCCEEDED(rc);
    const char *step = "nvInitialize";
    if (nv_up) {
        step = "nvMapInit";
        rc = nvMapInit();
        map_up = R_SUCCEEDED(rc);
    }
    if (map_up) {
        step = "nvFenceInit";
        rc = nvFenceInit();
        fence_up = R_SUCCEEDED(rc);
    }
    if (fence_up) {
        step = "allocating the buffer";
        memory = memalign(0x1000u, VP9_MAP_SIZE);
        rc = memory != NULL ? 0 : MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
    }
    if (memory != NULL) {
        memset(memory, 0, VP9_MAP_SIZE);
        step = "nvMapCreate";
        // Not cacheable: the processor's writes reach the engine and its writes reach the
        // processor without a flush or an invalidate in between. Aligned as nvtegra aligns.
        rc = nvMapCreate(&map, memory, VP9_MAP_SIZE, 0x10000u, NvKind_Pitch, false);
        buffer_up = R_SUCCEEDED(rc);
    }
    if (buffer_up) {
        step = "nvChannelCreate /dev/nvhost-nvdec";
        rc = nvChannelCreate(&channel, "/dev/nvhost-nvdec");
        channel_up = R_SUCCEEDED(rc);
    }
    Result timeout_rc = 0;
    if (channel_up) {
        // The official driver sets this on the decoder's channel only; nvtegra does the same.
        timeout_rc = nvioctlChannel_SetSubmitTimeout(channel.fd, 1000);
        step = "GetSyncpt";
        rc = nvioctlChannel_GetSyncpt(channel.fd, 0, &syncpt);
    }
    if (channel_up && R_SUCCEEDED(rc)) {
        step = "MapCommandBuffer";
        nvioctl_command_buffer_map m = {.handle = map.handle};
        rc = nvioctlChannel_MapCommandBuffer(channel.fd, &m, 1, false);
        mapped = R_SUCCEEDED(rc);
        iova = m.iova;
    }
    if (!mapped) {
        snprintf(why, why_size, "%s failed: 0x%x", step, (unsigned)rc);
        NvdecDeviceClose();
        return false;
    }
    if ((iova & 0xFFu) != 0u) {
        snprintf(why, why_size, "the buffer was mapped at 0x%x, not on a 256-byte boundary",
                 (unsigned)iova);
        NvdecDeviceClose();
        return false;
    }
    char clock[64];
    AskClock(clock, sizeof(clock));
    char timeout[48] = "";
    if (R_FAILED(timeout_rc)) {
        snprintf(timeout, sizeof(timeout), ", submit timeout refused 0x%x",
                 (unsigned)timeout_rc);
    }
    snprintf(why, why_size, "syncpoint %u, buffer of %u KiB at 0x%x, %s%s", (unsigned)syncpt,
             (unsigned)(VP9_MAP_SIZE / 1024u), (unsigned)iova, clock, timeout);
    return true;
}

uint8_t *NvdecDeviceMemory(void) {
    return memory;
}

uint32_t NvdecDeviceIova(void) {
    return iova;
}

uint32_t NvdecDeviceSyncpt(void) {
    return syncpt;
}

bool NvdecDeviceRun(uint32_t words, int32_t timeout_us, char *why, size_t why_size) {
    DeviceSync();
    const nvioctl_cmdbuf command = {.mem = map.handle, .offset = VP9_AT_COMMANDS, .words = words};
    // One increment, the one the last command word asks for when the job is done.
    const nvioctl_syncpt_incr increment = {
        .syncpt_id = syncpt, .syncpt_incrs = 1, .waitbase_id = UINT32_MAX,
        .next = UINT32_MAX, .prev = UINT32_MAX};
    nvioctl_fence fence = {0};
    Result rc = nvioctlChannel_Submit(channel.fd, &command, 1, NULL, NULL, 0, &increment, 1,
                                      &fence, 1);
    if (R_FAILED(rc)) {
        snprintf(why, why_size, "Submit failed: 0x%x", (unsigned)rc);
        return false;
    }
    rc = nvFenceWait(&fence, timeout_us);
    DeviceSync();
    if (R_FAILED(rc)) {
        snprintf(why, why_size,
                 "did not finish within %u ms: syncpoint %u never reached %u (0x%x)",
                 (unsigned)(timeout_us / 1000), (unsigned)fence.id, (unsigned)fence.value,
                 (unsigned)rc);
        return false;
    }
    return true;
}
