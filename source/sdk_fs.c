// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <string.h>
#include <switch.h>

#include "sdk.h"

// nn::fs's exported functions (sdk.h) and their own code: what runs where no machine takes the
// function. The own code keeps a mount table and handles of its own over libnx's file system
// objects; a mount the machine made is not in it, and a call with its name answers
// SDK_OWN_UNSUPPORTED, as the SDK's own code would not know it either.

static uint32_t own_calls[SDK_FUNCTION_COUNT];

void SdkOwnCall(SdkFunction function) {
    __atomic_add_fetch(&own_calls[function], 1u, __ATOMIC_RELAXED);
}

uint32_t SdkOwnCalls(SdkFunction function) {
    return __atomic_load_n(&own_calls[function], __ATOMIC_RELAXED);
}

size_t SdkOwnDescribeSince(char *out, size_t out_size, uint32_t noted[SDK_FUNCTION_COUNT]) {
    static const char *const NAMES[SDK_FUNCTION_COUNT] = {
#define SDK_NAME(id, name) name,
        SDK_FUNCTIONS(SDK_NAME)
#undef SDK_NAME
    };
    if (out == NULL || out_size == 0u) {
        return 0;
    }
    out[0] = '\0';
    size_t used = 0;
    for (uint32_t i = 0; i < SDK_FUNCTION_COUNT; ++i) {
        const uint32_t now = SdkOwnCalls((SdkFunction)i);
        const uint32_t calls = now - noted[i];
        noted[i] = now;
        if (calls == 0u || used >= out_size) {
            continue;
        }
        const int wrote = snprintf(out + used, out_size - used, "%s%s x%u", used > 0u ? ", " : "",
                                   NAMES[i], (unsigned)calls);
        used = wrote < 0 ? out_size : used + (size_t)wrote;
    }
    return strlen(out);
}

#define OWN_MOUNTS 8u
#define OWN_FILES 16u
#define OWN_DIRECTORIES 4u

typedef struct {
    bool used;
    char name[NN_FS_MOUNT_NAME_MAX + 1u];
    FsFileSystem fs;
} OwnMount;

typedef struct {
    bool used;
    FsFile file;
} OwnFile;

typedef struct {
    bool used;
    FsDir directory;
} OwnDirectory;

static OwnMount mounts[OWN_MOUNTS];
static OwnFile files[OWN_FILES];
static OwnDirectory directories[OWN_DIRECTORIES];
/// The game scene's file thread reads while the main thread runs cases.
static Mutex own_lock;

/// fs's result for a mount name already in use (2-0060).
#define OWN_MOUNT_EXISTS MAKERESULT(NN_FS_MODULE, 60)

/// The own mount a path names ("name:/sub"), with @p sub pointing at its "/sub"; NULL when the path
/// has no mount name the own code made. Lock held.
static OwnMount *MountOf(const char *path, const char **sub) {
    if (path == NULL) {
        return NULL;
    }
    const char *const colon = strchr(path, ':');
    if (colon == NULL || colon == path || (size_t)(colon - path) > NN_FS_MOUNT_NAME_MAX) {
        return NULL;
    }
    const size_t length = (size_t)(colon - path);
    for (uint32_t i = 0; i < OWN_MOUNTS; ++i) {
        if (mounts[i].used && strlen(mounts[i].name) == length &&
            memcmp(mounts[i].name, path, length) == 0) {
            *sub = colon + 1;
            return &mounts[i];
        }
    }
    return NULL;
}

bool SdkFsOwnMount(const char *name) {
    mutexLock(&own_lock);
    bool found = false;
    for (uint32_t i = 0; i < OWN_MOUNTS && !found; ++i) {
        found = mounts[i].used && strcmp(mounts[i].name, name) == 0;
    }
    mutexUnlock(&own_lock);
    return found;
}

/// A new own mount of @p name over what @p open opens; fsp-srv is opened with the first and closed
/// with the last (NnFsUnmount).
static uint32_t Mount(const char *name, Result (*open)(FsFileSystem *)) {
    if (name == NULL || name[0] == '\0' || strlen(name) > NN_FS_MOUNT_NAME_MAX) {
        return SDK_OWN_UNSUPPORTED;
    }
    mutexLock(&own_lock);
    OwnMount *free_slot = NULL;
    bool any = false;
    for (uint32_t i = 0; i < OWN_MOUNTS; ++i) {
        if (mounts[i].used && strcmp(mounts[i].name, name) == 0) {
            mutexUnlock(&own_lock);
            return OWN_MOUNT_EXISTS;
        }
        any = any || mounts[i].used;
        if (!mounts[i].used && free_slot == NULL) {
            free_slot = &mounts[i];
        }
    }
    Result rc = free_slot != NULL ? 0 : SDK_OWN_UNSUPPORTED;
    if (R_SUCCEEDED(rc) && !any) {
        rc = fsInitialize();
    }
    if (R_SUCCEEDED(rc)) {
        rc = open(&free_slot->fs);
        if (R_FAILED(rc) && !any) {
            fsExit();
        }
    }
    if (R_SUCCEEDED(rc)) {
        free_slot->used = true;
        snprintf(free_slot->name, sizeof(free_slot->name), "%s", name);
    }
    mutexUnlock(&own_lock);
    return rc;
}

SDK_FUNCTION uint32_t NnFsMountSdCard(const char *name) {
    SdkOwnCall(SDK_FS_MOUNT_SD_CARD);
    return Mount(name, fsOpenSdCardFileSystem);
}

SDK_FUNCTION uint32_t NnFsMountTemporaryStorage(const char *name) {
    SdkOwnCall(SDK_FS_MOUNT_TEMPORARY);
    return Mount(name, fsOpen_TemporaryStorage);
}

SDK_FUNCTION void NnFsUnmount(const char *name) {
    SdkOwnCall(SDK_FS_UNMOUNT);
    mutexLock(&own_lock);
    bool left = false;
    bool closed = false;
    for (uint32_t i = 0; i < OWN_MOUNTS; ++i) {
        if (mounts[i].used && name != NULL && strcmp(mounts[i].name, name) == 0) {
            fsFsClose(&mounts[i].fs);
            memset(&mounts[i], 0, sizeof(mounts[i]));
            closed = true;
        }
        left = left || mounts[i].used;
    }
    if (closed && !left) {
        fsExit();
    }
    mutexUnlock(&own_lock);
}

/// The own code of a function that takes one path: @p call on the mount's file system and the
/// path inside it.
#define ON_PATH(path, call)                                                                       \
    do {                                                                                          \
        mutexLock(&own_lock);                                                                     \
        const char *sub = NULL;                                                                   \
        OwnMount *const mount = MountOf(path, &sub);                                              \
        const Result rc_ = mount != NULL ? (call) : SDK_OWN_UNSUPPORTED;                         \
        mutexUnlock(&own_lock);                                                                   \
        return rc_;                                                                               \
    } while (0)

SDK_FUNCTION uint32_t NnFsCommit(const char *name) {
    SdkOwnCall(SDK_FS_COMMIT);
    char path[NN_FS_MOUNT_NAME_MAX + 3u];
    snprintf(path, sizeof(path), "%s:/", name != NULL ? name : "");
    ON_PATH(path, fsFsCommit(&mount->fs));
}

SDK_FUNCTION uint32_t NnFsCreateFile(const char *path, int64_t size) {
    SdkOwnCall(SDK_FS_CREATE_FILE);
    ON_PATH(path, fsFsCreateFile(&mount->fs, sub, size, 0));
}

SDK_FUNCTION uint32_t NnFsDeleteFile(const char *path) {
    SdkOwnCall(SDK_FS_DELETE_FILE);
    ON_PATH(path, fsFsDeleteFile(&mount->fs, sub));
}

SDK_FUNCTION uint32_t NnFsCreateDirectory(const char *path) {
    SdkOwnCall(SDK_FS_CREATE_DIRECTORY);
    ON_PATH(path, fsFsCreateDirectory(&mount->fs, sub));
}

SDK_FUNCTION uint32_t NnFsDeleteDirectory(const char *path) {
    SdkOwnCall(SDK_FS_DELETE_DIRECTORY);
    ON_PATH(path, fsFsDeleteDirectory(&mount->fs, sub));
}

SDK_FUNCTION uint32_t NnFsDeleteDirectoryRecursively(const char *path) {
    SdkOwnCall(SDK_FS_DELETE_DIRECTORY_RECURSIVELY);
    ON_PATH(path, fsFsDeleteDirectoryRecursively(&mount->fs, sub));
}

SDK_FUNCTION uint32_t NnFsCleanDirectoryRecursively(const char *path) {
    SdkOwnCall(SDK_FS_CLEAN_DIRECTORY_RECURSIVELY);
    ON_PATH(path, fsFsCleanDirectoryRecursively(&mount->fs, sub));
}

/// A rename's own code: both paths on one own mount, as fsp-srv renames within one file system.
static uint32_t Rename(const char *from, const char *to, bool directory) {
    mutexLock(&own_lock);
    const char *from_sub = NULL;
    const char *to_sub = NULL;
    OwnMount *const a = MountOf(from, &from_sub);
    OwnMount *const b = MountOf(to, &to_sub);
    Result rc = SDK_OWN_UNSUPPORTED;
    if (a != NULL && a == b) {
        rc = directory ? fsFsRenameDirectory(&a->fs, from_sub, to_sub)
                       : fsFsRenameFile(&a->fs, from_sub, to_sub);
    }
    mutexUnlock(&own_lock);
    return rc;
}

SDK_FUNCTION uint32_t NnFsRenameFile(const char *from, const char *to) {
    SdkOwnCall(SDK_FS_RENAME_FILE);
    return Rename(from, to, false);
}

SDK_FUNCTION uint32_t NnFsRenameDirectory(const char *from, const char *to) {
    SdkOwnCall(SDK_FS_RENAME_DIRECTORY);
    return Rename(from, to, true);
}

SDK_FUNCTION uint32_t NnFsGetEntryType(int32_t *type, const char *path) {
    SdkOwnCall(SDK_FS_GET_ENTRY_TYPE);
    mutexLock(&own_lock);
    const char *sub = NULL;
    OwnMount *const mount = MountOf(path, &sub);
    FsDirEntryType got = (FsDirEntryType)0;
    const Result rc = mount != NULL ? fsFsGetEntryType(&mount->fs, sub, &got) : SDK_OWN_UNSUPPORTED;
    mutexUnlock(&own_lock);
    if (R_SUCCEEDED(rc) && type != NULL) {
        *type = (int32_t)got;
    }
    return rc;
}

SDK_FUNCTION uint32_t NnFsGetFreeSpaceSize(int64_t *size, const char *path) {
    SdkOwnCall(SDK_FS_GET_FREE_SPACE);
    ON_PATH(path, fsFsGetFreeSpace(&mount->fs, sub, size));
}

SDK_FUNCTION uint32_t NnFsGetTotalSpaceSize(int64_t *size, const char *path) {
    SdkOwnCall(SDK_FS_GET_TOTAL_SPACE);
    ON_PATH(path, fsFsGetTotalSpace(&mount->fs, sub, size));
}

SDK_FUNCTION uint32_t NnFsGetFileTimeStampForDebug(NnFsFileTimeStamp *stamp, const char *path) {
    SdkOwnCall(SDK_FS_GET_TIME_STAMP);
    ON_PATH(path, fsFsGetFileTimeStampRaw(&mount->fs, sub, stamp));
}

// A handle the own code made: its slot plus one, so nought is no handle, with the tag below to
// tell it from another's.
#define OWN_HANDLE_TAG 0x0EE0000000000000ull

static OwnFile *FileOf(NnFsFileHandle file) {
    const uint64_t slot = (file.handle & ~OWN_HANDLE_TAG) - 1u;
    return (file.handle & OWN_HANDLE_TAG) == OWN_HANDLE_TAG && slot < OWN_FILES &&
                   files[slot].used
               ? &files[slot]
               : NULL;
}

static OwnDirectory *DirectoryOf(NnFsDirectoryHandle directory) {
    const uint64_t slot = (directory.handle & ~OWN_HANDLE_TAG) - 1u;
    return (directory.handle & OWN_HANDLE_TAG) == OWN_HANDLE_TAG && slot < OWN_DIRECTORIES &&
                   directories[slot].used
               ? &directories[slot]
               : NULL;
}

SDK_FUNCTION uint32_t NnFsOpenFile(NnFsFileHandle *out, const char *path, int mode) {
    SdkOwnCall(SDK_FS_OPEN_FILE);
    mutexLock(&own_lock);
    const char *sub = NULL;
    OwnMount *const mount = MountOf(path, &sub);
    uint32_t slot = OWN_FILES;
    for (uint32_t i = 0; i < OWN_FILES && slot == OWN_FILES; ++i) {
        slot = files[i].used ? OWN_FILES : i;
    }
    Result rc = mount != NULL && slot < OWN_FILES && out != NULL ? 0 : SDK_OWN_UNSUPPORTED;
    if (R_SUCCEEDED(rc)) {
        rc = fsFsOpenFile(&mount->fs, sub, (u32)mode, &files[slot].file);
    }
    if (R_SUCCEEDED(rc)) {
        files[slot].used = true;
        out->handle = OWN_HANDLE_TAG | (slot + 1u);
    }
    mutexUnlock(&own_lock);
    return rc;
}

SDK_FUNCTION void NnFsCloseFile(NnFsFileHandle file) {
    SdkOwnCall(SDK_FS_CLOSE_FILE);
    mutexLock(&own_lock);
    OwnFile *const f = FileOf(file);
    if (f != NULL) {
        fsFileClose(&f->file);
        memset(f, 0, sizeof(*f));
    }
    mutexUnlock(&own_lock);
}

// A file's reads and writes run outside the lock: each handle belongs to one thread at a time.

SDK_FUNCTION uint32_t NnFsReadFile(size_t *read, NnFsFileHandle file, int64_t offset,
                                   void *buffer, size_t size) {
    SdkOwnCall(SDK_FS_READ_FILE);
    OwnFile *const f = FileOf(file);
    u64 got = 0;
    const Result rc = f != NULL
                          ? fsFileRead(&f->file, offset, buffer, size, FsReadOption_None, &got)
                          : SDK_OWN_UNSUPPORTED;
    if (read != NULL) {
        *read = (size_t)got;
    }
    return rc;
}

SDK_FUNCTION uint32_t NnFsWriteFile(NnFsFileHandle file, int64_t offset, const void *buffer,
                                    size_t size, const NnFsWriteOption *option) {
    SdkOwnCall(SDK_FS_WRITE_FILE);
    OwnFile *const f = FileOf(file);
    const bool flush = option != NULL && (option->flags & NN_FS_WRITE_FLUSH) != 0;
    return f != NULL ? fsFileWrite(&f->file, offset, buffer, size,
                                   flush ? FsWriteOption_Flush : FsWriteOption_None)
                     : SDK_OWN_UNSUPPORTED;
}

SDK_FUNCTION uint32_t NnFsFlushFile(NnFsFileHandle file) {
    SdkOwnCall(SDK_FS_FLUSH_FILE);
    OwnFile *const f = FileOf(file);
    return f != NULL ? fsFileFlush(&f->file) : SDK_OWN_UNSUPPORTED;
}

SDK_FUNCTION uint32_t NnFsSetFileSize(NnFsFileHandle file, int64_t size) {
    SdkOwnCall(SDK_FS_SET_FILE_SIZE);
    OwnFile *const f = FileOf(file);
    return f != NULL ? fsFileSetSize(&f->file, size) : SDK_OWN_UNSUPPORTED;
}

SDK_FUNCTION uint32_t NnFsGetFileSize(int64_t *size, NnFsFileHandle file) {
    SdkOwnCall(SDK_FS_GET_FILE_SIZE);
    OwnFile *const f = FileOf(file);
    return f != NULL ? fsFileGetSize(&f->file, size) : SDK_OWN_UNSUPPORTED;
}

SDK_FUNCTION uint32_t NnFsOpenDirectory(NnFsDirectoryHandle *out, const char *path, int mode) {
    SdkOwnCall(SDK_FS_OPEN_DIRECTORY);
    mutexLock(&own_lock);
    const char *sub = NULL;
    OwnMount *const mount = MountOf(path, &sub);
    uint32_t slot = OWN_DIRECTORIES;
    for (uint32_t i = 0; i < OWN_DIRECTORIES && slot == OWN_DIRECTORIES; ++i) {
        slot = directories[i].used ? OWN_DIRECTORIES : i;
    }
    Result rc = mount != NULL && slot < OWN_DIRECTORIES && out != NULL ? 0 : SDK_OWN_UNSUPPORTED;
    if (R_SUCCEEDED(rc)) {
        rc = fsFsOpenDirectory(&mount->fs, sub, (u32)mode, &directories[slot].directory);
    }
    if (R_SUCCEEDED(rc)) {
        directories[slot].used = true;
        out->handle = OWN_HANDLE_TAG | (slot + 1u);
    }
    mutexUnlock(&own_lock);
    return rc;
}

SDK_FUNCTION uint32_t NnFsReadDirectory(int64_t *count, NnFsDirectoryEntry *entries,
                                        NnFsDirectoryHandle directory, int64_t max) {
    SdkOwnCall(SDK_FS_READ_DIRECTORY);
    OwnDirectory *const d = DirectoryOf(directory);
    s64 total = 0;
    const Result rc = d != NULL && max >= 0
                          ? fsDirRead(&d->directory, &total, (size_t)max, entries)
                          : SDK_OWN_UNSUPPORTED;
    if (count != NULL) {
        *count = total;
    }
    return rc;
}

SDK_FUNCTION uint32_t NnFsGetDirectoryEntryCount(int64_t *count, NnFsDirectoryHandle directory) {
    SdkOwnCall(SDK_FS_GET_DIRECTORY_ENTRY_COUNT);
    OwnDirectory *const d = DirectoryOf(directory);
    return d != NULL ? fsDirGetEntryCount(&d->directory, count) : SDK_OWN_UNSUPPORTED;
}

SDK_FUNCTION void NnFsCloseDirectory(NnFsDirectoryHandle directory) {
    SdkOwnCall(SDK_FS_CLOSE_DIRECTORY);
    mutexLock(&own_lock);
    OwnDirectory *const d = DirectoryOf(directory);
    if (d != NULL) {
        fsDirClose(&d->directory);
        memset(d, 0, sizeof(*d));
    }
    mutexUnlock(&own_lock);
}
