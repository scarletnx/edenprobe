// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// The way to the video image compositor: the /dev/nvhost-vic channel and one nvmap buffer,
/// uncached for the processor, that holds the configuration structure, the command words, two
/// slots' planes and the output, mapped into the engine once. Console build only.
///
/// The path is the one the homebrew FFmpeg backend (nvtegra) takes on the console: nvdrv, nvmap
/// and the fence service, the channel with its syncpoint, MapCommandBuffer for the I/O virtual
/// address, Submit with one syncpoint increment, and a fence wait with a timeout, so a job that
/// never finishes is a failed case and not a hung run.

/// Opens everything, or says in @p why which step failed and leaves nothing open.
bool VicDeviceOpen(char *why, size_t why_size);
void VicDeviceClose(void);

uint8_t *VicDeviceConfig(void);
uint8_t *VicDeviceLuma(uint32_t slot);
uint8_t *VicDeviceChroma(uint32_t slot);
/// VIC_OUTPUT_MAX bytes; an NV12 output's chroma starts VIC_LUMA_STRIDE * VIC_SIZE in.
uint8_t *VicDeviceOutput(void);
/// The bytes right after the output, which no job is told about.
uint8_t *VicDeviceGuard(void);
#define VIC_GUARD_SIZE 4096u

/// Submits the job the structure and the planes hold, with @p slots slots' surfaces and the
/// output's chroma when @p nv12_out, and waits up to @p timeout_us for its syncpoint. False with
/// the reason when the submit fails or the job does not finish in time.
bool VicDeviceRun(uint32_t slots, bool nv12_out, int32_t timeout_us, char *why, size_t why_size);

#ifdef __cplusplus
}
#endif
