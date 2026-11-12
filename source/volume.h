// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <switch.h>

#include "sdk.h"

#ifdef __cplusplus
extern "C" {
#endif

/// A file system as a case reaches it, the memory card or the temporary storage, in the mode the
/// run is in (hooks.h): libnx's file system object in IPC mode, each call the command libnx sends;
/// a mount of nn::fs's in hooks mode, each call the SDK's function. Paths are the file system's
/// own, "/edenprobe/...", in both; results are the service's or the SDK's, and the open modes,
/// write options, directory modes and entries are libnx's, which have the SDK's values.
typedef struct {
    bool hooks;
    /// IPC: the object the calls go to, and whether the volume opened it (and closes it).
    FsFileSystem *fs;
    FsFileSystem own;
    bool opened;
    /// hooks: the mount name, and whether the volume mounted it (and unmounts it).
    char mount[NN_FS_MOUNT_NAME_MAX + 1u];
} Volume;

typedef struct {
    bool hooks;
    /// Opened to write: in hooks mode its close flushes it first, as the SDK asks of a writer.
    bool writes;
    FsFile file;
    NnFsFileHandle handle;
} VolumeFile;

typedef struct {
    bool hooks;
    FsDir dir;
    NnFsDirectoryHandle handle;
} VolumeDir;

/// The memory card opened for a case of its own: fsOpenSdCardFileSystem in IPC mode,
/// nn::fs::MountSdCard as @p mount in hooks mode.
Result VolumeOpenCard(Volume *v, const char *mount);

/// The temporary storage: fsOpen_TemporaryStorage, or nn::fs::MountTemporaryStorage as @p mount.
Result VolumeOpenTemporary(Volume *v, const char *mount);

/// The memory card the program's start mounted, the C library's sdmc: device: fsdev's object in
/// IPC mode, the start's mount in hooks mode. False when there is none; closing it closes nothing.
bool VolumeOfSdmc(Volume *v);

/// Closes what the volume opened: the object, or the mount, whose files must be closed first.
void VolumeClose(Volume *v);

Result VolumeCommit(Volume *v);
Result VolumeCreateFile(Volume *v, const char *path, s64 size);
Result VolumeDeleteFile(Volume *v, const char *path);
Result VolumeCreateDirectory(Volume *v, const char *path);
Result VolumeDeleteDirectory(Volume *v, const char *path);
Result VolumeDeleteDirectoryRecursively(Volume *v, const char *path);
Result VolumeCleanDirectoryRecursively(Volume *v, const char *path);
Result VolumeRenameFile(Volume *v, const char *from, const char *to);
Result VolumeRenameDirectory(Volume *v, const char *from, const char *to);
Result VolumeGetEntryType(Volume *v, const char *path, FsDirEntryType *type);
Result VolumeGetFreeSpace(Volume *v, const char *path, s64 *size);
Result VolumeGetTotalSpace(Volume *v, const char *path, s64 *size);
Result VolumeGetTimeStamp(Volume *v, const char *path, FsTimeStampRaw *stamp);

Result VolumeOpenFile(Volume *v, const char *path, u32 mode, VolumeFile *file);
Result VolumeFileRead(VolumeFile *file, s64 offset, void *buffer, u64 size, u64 *read);
Result VolumeFileWrite(VolumeFile *file, s64 offset, const void *buffer, u64 size,
                       u32 option);
Result VolumeFileFlush(VolumeFile *file);
Result VolumeFileSetSize(VolumeFile *file, s64 size);
Result VolumeFileGetSize(VolumeFile *file, s64 *size);
void VolumeFileClose(VolumeFile *file);

Result VolumeOpenDirectory(Volume *v, const char *path, u32 mode, VolumeDir *dir);
Result VolumeDirRead(VolumeDir *dir, s64 *total, size_t max, FsDirectoryEntry *entries);
Result VolumeDirGetEntryCount(VolumeDir *dir, s64 *count);
void VolumeDirClose(VolumeDir *dir);

/// How the volume's calls go, for a case's detail: "fsp-srv" or "nn::fs".
const char *VolumeWay(const Volume *v);

#ifdef __cplusplus
}
#endif
