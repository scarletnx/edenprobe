// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// The way to the video decode engine: the /dev/nvhost-nvdec channel and one nvmap buffer,
/// uncached for the processor, laid out as vp9ref.h says and mapped into the engine once.
/// Console build only.
///
/// The path is the one the homebrew FFmpeg backend (nvtegra) takes on the console: nvdrv, nvmap
/// and the fence service, the channel with its syncpoint and a submit timeout of a second, the
/// engine's clock asked of the multimedia service at its highest, MapCommandBuffer for the I/O
/// virtual address, and per job a Submit with one syncpoint increment and a fence wait with a
/// timeout, so a job that never finishes is a failed case and not a hung run.

/// Opens everything, or says in @p why which step failed and leaves nothing open. A refused
/// submit timeout or clock request does not stop it; @p why says so.
bool NvdecDeviceOpen(char *why, size_t why_size);
void NvdecDeviceClose(void);

/// The buffer, VP9_MAP_SIZE bytes, as the processor sees it.
uint8_t *NvdecDeviceMemory(void);
/// Where the engine sees it.
uint32_t NvdecDeviceIova(void);
uint32_t NvdecDeviceSyncpt(void);

/// Submits the @p words command words at VP9_AT_COMMANDS and waits up to @p timeout_us for the
/// syncpoint they increment. False with the reason when the submit fails or the job does not
/// finish in time.
bool NvdecDeviceRun(uint32_t words, int32_t timeout_us, char *why, size_t why_size);

#ifdef __cplusplus
}
#endif
