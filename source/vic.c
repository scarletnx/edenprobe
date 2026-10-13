// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "vic.h"

#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <switch.h>

#include "vicref.h"

// Where each thing sits in the one buffer. Every offset is a multiple of 256, since the engine
// is given addresses shifted right by 8.
#define AT_CONFIG 0x0000u
#define AT_COMMANDS 0x0800u
#define AT_LUMA0 0x1000u
#define AT_CHROMA0 0x2000u
#define AT_LUMA1 0x3000u
#define AT_CHROMA1 0x4000u
#define AT_OUTPUT 0x5000u
#define AT_GUARD (AT_OUTPUT + VIC_OUTPUT_MAX)
#define MAP_SIZE 0x10000u
#define COMMAND_WORDS ((AT_LUMA0 - AT_COMMANDS) / 4u)

_Static_assert(AT_COMMANDS >= AT_CONFIG + VIC_CONFIG_SIZE, "the structure fits");
_Static_assert(AT_GUARD + VIC_GUARD_SIZE <= MAP_SIZE, "the guard fits");

static uint8_t *memory;
static NvMap map;
static NvChannel channel;
static uint32_t syncpt;
static uint32_t iova;
static bool nv_up, map_up, fence_up, mapped, channel_up, buffer_up;

static void DeviceSync(void) {
    __asm__ volatile("dsb sy" ::: "memory");
}

void VicDeviceClose(void) {
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

bool VicDeviceOpen(char *why, size_t why_size) {
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
        step = "allocating 64 KiB";
        memory = memalign(0x1000u, MAP_SIZE);
        rc = memory != NULL ? 0 : MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
    }
    if (memory != NULL) {
        memset(memory, 0, MAP_SIZE);
        step = "nvMapCreate";
        // Not cacheable: the processor's writes reach the engine and its writes reach the
        // processor without a flush or an invalidate in between.
        rc = nvMapCreate(&map, memory, MAP_SIZE, 0x1000u, NvKind_Pitch, false);
        buffer_up = R_SUCCEEDED(rc);
    }
    if (buffer_up) {
        step = "nvChannelCreate /dev/nvhost-vic";
        rc = nvChannelCreate(&channel, "/dev/nvhost-vic");
        channel_up = R_SUCCEEDED(rc);
    }
    if (channel_up) {
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
        VicDeviceClose();
        return false;
    }
    if ((iova & 0xFFu) != 0u) {
        snprintf(why, why_size, "the buffer was mapped at 0x%x, not on a 256-byte boundary",
                 (unsigned)iova);
        VicDeviceClose();
        return false;
    }
    snprintf(why, why_size, "syncpoint %u, buffer at 0x%x", (unsigned)syncpt, (unsigned)iova);
    return true;
}

uint8_t *VicDeviceConfig(void) {
    return memory + AT_CONFIG;
}

uint8_t *VicDeviceLuma(uint32_t slot) {
    return memory + (slot == 0u ? AT_LUMA0 : AT_LUMA1);
}

uint8_t *VicDeviceChroma(uint32_t slot) {
    return memory + (slot == 0u ? AT_CHROMA0 : AT_CHROMA1);
}

uint8_t *VicDeviceOutput(void) {
    return memory + AT_OUTPUT;
}

uint8_t *VicDeviceGuard(void) {
    return memory + AT_GUARD;
}

bool VicDeviceRun(uint32_t slots, bool nv12_out, int32_t timeout_us, char *why, size_t why_size) {
    const VicAddresses at = {
        .config = iova + AT_CONFIG,
        .out_luma = iova + AT_OUTPUT,
        .out_chroma = iova + AT_OUTPUT + VIC_LUMA_STRIDE * VIC_SIZE,
        .luma = {iova + AT_LUMA0, iova + AT_LUMA1},
        .chroma = {iova + AT_CHROMA0, iova + AT_CHROMA1},
    };
    uint32_t *const words = (uint32_t *)(memory + AT_COMMANDS);
    const size_t count = VicCommands(words, COMMAND_WORDS, &at, slots, nv12_out, syncpt);
    if (count == 0u) {
        snprintf(why, why_size, "the command words do not fit");
        return false;
    }
    DeviceSync();
    const nvioctl_cmdbuf command = {
        .mem = map.handle, .offset = AT_COMMANDS, .words = (u32)count};
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
                 "the job did not finish within %u ms: syncpoint %u never reached %u (0x%x)",
                 (unsigned)(timeout_us / 1000), (unsigned)fence.id, (unsigned)fence.value,
                 (unsigned)rc);
        return false;
    }
    return true;
}
