// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <string.h>
#include <switch.h>

#include "hooks.h"
#include "volume.h"

// The volume.h calls: in IPC mode each one libnx's call as the cases made it before there were two
// modes, so that mode sends the same commands; in hooks mode the SDK's function on the volume's
// mount, the path after its name.

/// The longest path nn::fs takes, with a mount name before it.
#define FULL_MAX (NN_FS_MOUNT_NAME_MAX + 2u + FS_MAX_PATH)

/// @p path on the volume's mount, "name:/path"; an empty string when it does not fit, which the
/// SDK refuses as a path.
static const char *Full(const Volume *v, const char *path, char out[FULL_MAX]) {
    const int wrote = snprintf(out, FULL_MAX, "%s:%s", v->mount, path);
    if (wrote < 0 || (size_t)wrote >= FULL_MAX) {
        out[0] = '\0';
    }
    return out;
}

static Result OpenIpc(Volume *v, Result (*open)(FsFileSystem *)) {
    const Result rc = open(&v->own);
    if (R_SUCCEEDED(rc)) {
        v->fs = &v->own;
        v->opened = true;
    }
    return rc;
}

static Result OpenHooks(Volume *v, const char *mount, uint32_t (*mount_as)(const char *)) {
    snprintf(v->mount, sizeof(v->mount), "%s", mount);
    const Result rc = mount_as(v->mount);
    v->opened = R_SUCCEEDED(rc);
    return rc;
}

Result VolumeOpenCard(Volume *v, const char *mount) {
    memset(v, 0, sizeof(*v));
    v->hooks = HooksOn();
    return v->hooks ? OpenHooks(v, mount, NnFsMountSdCard) : OpenIpc(v, fsOpenSdCardFileSystem);
}

Result VolumeOpenTemporary(Volume *v, const char *mount) {
    memset(v, 0, sizeof(*v));
    v->hooks = HooksOn();
    return v->hooks ? OpenHooks(v, mount, NnFsMountTemporaryStorage)
                    : OpenIpc(v, fsOpen_TemporaryStorage);
}

bool VolumeOfSdmc(Volume *v) {
    memset(v, 0, sizeof(*v));
    v->hooks = HooksOn();
    if (v->hooks) {
        snprintf(v->mount, sizeof(v->mount), "sdmc");
        return true;
    }
    v->fs = fsdevGetDeviceFileSystem("sdmc");
    return v->fs != NULL;
}

void VolumeClose(Volume *v) {
    if (v->opened) {
        if (v->hooks) {
            NnFsUnmount(v->mount);
        } else {
            fsFsClose(v->fs);
        }
    }
    v->opened = false;
}

const char *VolumeWay(const Volume *v) {
    return v->hooks ? "nn::fs" : "fsp-srv";
}

Result VolumeCommit(Volume *v) {
    return v->hooks ? NnFsCommit(v->mount) : fsFsCommit(v->fs);
}

/// A call that takes one path: libnx's on the object, or the SDK's on the full path.
#define ON_PATH(v, path, ipc, sdk)                                                                \
    do {                                                                                          \
        if (!(v)->hooks) {                                                                        \
            return ipc((v)->fs, path);                                                            \
        }                                                                                         \
        char full[FULL_MAX];                                                                      \
        return sdk(Full(v, path, full));                                                          \
    } while (0)

Result VolumeCreateFile(Volume *v, const char *path, s64 size) {
    if (!v->hooks) {
        return fsFsCreateFile(v->fs, path, size, 0);
    }
    char full[FULL_MAX];
    return NnFsCreateFile(Full(v, path, full), size);
}

Result VolumeDeleteFile(Volume *v, const char *path) {
    ON_PATH(v, path, fsFsDeleteFile, NnFsDeleteFile);
}

Result VolumeCreateDirectory(Volume *v, const char *path) {
    ON_PATH(v, path, fsFsCreateDirectory, NnFsCreateDirectory);
}

Result VolumeDeleteDirectory(Volume *v, const char *path) {
    ON_PATH(v, path, fsFsDeleteDirectory, NnFsDeleteDirectory);
}

Result VolumeDeleteDirectoryRecursively(Volume *v, const char *path) {
    ON_PATH(v, path, fsFsDeleteDirectoryRecursively, NnFsDeleteDirectoryRecursively);
}

Result VolumeCleanDirectoryRecursively(Volume *v, const char *path) {
    ON_PATH(v, path, fsFsCleanDirectoryRecursively, NnFsCleanDirectoryRecursively);
}

Result VolumeRenameFile(Volume *v, const char *from, const char *to) {
    if (!v->hooks) {
        return fsFsRenameFile(v->fs, from, to);
    }
    char a[FULL_MAX];
    char b[FULL_MAX];
    return NnFsRenameFile(Full(v, from, a), Full(v, to, b));
}

Result VolumeRenameDirectory(Volume *v, const char *from, const char *to) {
    if (!v->hooks) {
        return fsFsRenameDirectory(v->fs, from, to);
    }
    char a[FULL_MAX];
    char b[FULL_MAX];
    return NnFsRenameDirectory(Full(v, from, a), Full(v, to, b));
}

Result VolumeGetEntryType(Volume *v, const char *path, FsDirEntryType *type) {
    if (!v->hooks) {
        return fsFsGetEntryType(v->fs, path, type);
    }
    char full[FULL_MAX];
    int32_t got = (int32_t)*type;
    const Result rc = NnFsGetEntryType(&got, Full(v, path, full));
    *type = (FsDirEntryType)got;
    return rc;
}

Result VolumeGetFreeSpace(Volume *v, const char *path, s64 *size) {
    if (!v->hooks) {
        return fsFsGetFreeSpace(v->fs, path, size);
    }
    char full[FULL_MAX];
    return NnFsGetFreeSpaceSize(size, Full(v, path, full));
}

Result VolumeGetTotalSpace(Volume *v, const char *path, s64 *size) {
    if (!v->hooks) {
        return fsFsGetTotalSpace(v->fs, path, size);
    }
    char full[FULL_MAX];
    return NnFsGetTotalSpaceSize(size, Full(v, path, full));
}

Result VolumeGetTimeStamp(Volume *v, const char *path, FsTimeStampRaw *stamp) {
    if (!v->hooks) {
        return fsFsGetFileTimeStampRaw(v->fs, path, stamp);
    }
    char full[FULL_MAX];
    return NnFsGetFileTimeStampForDebug(stamp, Full(v, path, full));
}

Result VolumeOpenFile(Volume *v, const char *path, u32 mode, VolumeFile *file) {
    file->hooks = v->hooks;
    file->writes = (mode & FsOpenMode_Write) != 0u;
    if (!v->hooks) {
        return fsFsOpenFile(v->fs, path, mode, &file->file);
    }
    char full[FULL_MAX];
    return NnFsOpenFile(&file->handle, Full(v, path, full), (int)mode);
}

Result VolumeFileRead(VolumeFile *file, s64 offset, void *buffer, u64 size, u64 *read) {
    if (!file->hooks) {
        return fsFileRead(&file->file, offset, buffer, size, FsReadOption_None, read);
    }
    size_t got = 0;
    const Result rc = NnFsReadFile(&got, file->handle, offset, buffer, (size_t)size);
    *read = got;
    return rc;
}

Result VolumeFileWrite(VolumeFile *file, s64 offset, const void *buffer, u64 size,
                       u32 option) {
    if (!file->hooks) {
        return fsFileWrite(&file->file, offset, buffer, size, option);
    }
    const NnFsWriteOption write = { (int32_t)option };
    return NnFsWriteFile(file->handle, offset, buffer, (size_t)size, &write);
}

Result VolumeFileFlush(VolumeFile *file) {
    return file->hooks ? NnFsFlushFile(file->handle) : fsFileFlush(&file->file);
}

Result VolumeFileSetSize(VolumeFile *file, s64 size) {
    return file->hooks ? NnFsSetFileSize(file->handle, size) : fsFileSetSize(&file->file, size);
}

Result VolumeFileGetSize(VolumeFile *file, s64 *size) {
    if (!file->hooks) {
        return fsFileGetSize(&file->file, size);
    }
    int64_t got = *size;
    const Result rc = NnFsGetFileSize(&got, file->handle);
    *size = got;
    return rc;
}

void VolumeFileClose(VolumeFile *file) {
    if (file->hooks) {
        // fsp-srv flushes a file it closes; the SDK stops a program that closes one it wrote to
        // and did not flush, so a writer flushes first, as a game does.
        if (file->writes) {
            (void)NnFsFlushFile(file->handle);
        }
        NnFsCloseFile(file->handle);
    } else {
        fsFileClose(&file->file);
    }
}

Result VolumeOpenDirectory(Volume *v, const char *path, u32 mode, VolumeDir *dir) {
    dir->hooks = v->hooks;
    if (!v->hooks) {
        return fsFsOpenDirectory(v->fs, path, mode, &dir->dir);
    }
    char full[FULL_MAX];
    return NnFsOpenDirectory(&dir->handle, Full(v, path, full), (int)mode);
}

Result VolumeDirRead(VolumeDir *dir, s64 *total, size_t max, FsDirectoryEntry *entries) {
    if (!dir->hooks) {
        return fsDirRead(&dir->dir, total, max, entries);
    }
    int64_t got = *total;
    const Result rc = NnFsReadDirectory(&got, entries, dir->handle, (int64_t)max);
    *total = got;
    return rc;
}

Result VolumeDirGetEntryCount(VolumeDir *dir, s64 *count) {
    if (!dir->hooks) {
        return fsDirGetEntryCount(&dir->dir, count);
    }
    int64_t got = *count;
    const Result rc = NnFsGetDirectoryEntryCount(&got, dir->handle);
    *count = got;
    return rc;
}

void VolumeDirClose(VolumeDir *dir) {
    if (dir->hooks) {
        NnFsCloseDirectory(dir->handle);
    } else {
        fsDirClose(&dir->dir);
    }
}
