// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/iosupport.h>
#include <sys/syslimits.h>
#include <switch.h>

#include "check.h"
#include "hooks.h"
#include "mode.h"
#include "platform.h"
#include "report.h"
#include "sdk.h"

// The program's start in either mode (hooks.h), and in hooks mode the C library's sdmc: device
// over nn::fs.

// libnx's start, which the program's __appInit below takes the place of: its own functions for the
// time zone and the working directory, the window's and the application's hooks when linked, and
// the applet type its start reads.
void __libnx_init_time(void);
void __libnx_init_cwd(void);
void __nx_win_init(void) __attribute__((weak));
void __nx_win_exit(void) __attribute__((weak));
void userAppInit(void) __attribute__((weak));
void userAppExit(void) __attribute__((weak));
extern AppletType __nx_applet_type;

/// The mode file's path, and the nn::fs mount name the memory card has in hooks mode: the C
/// library's device name, so a path the program writes is the SDK's path unchanged.
#define MODE_PATH PROBE_DIRECTORY "/mode"
#define CARD "sdmc"

static bool hooks_on;
/// What the start saw: whether the program's own code made the card's mount when the mode file
/// was read, and in hooks mode whether it answered nn::time::Initialize.
static bool card_own;
static bool time_own;
static uint32_t card_rc;
static uint32_t time_rc;

bool HooksOn(void) {
    return hooks_on;
}

// ------------------------------------------------------------------------- the mode file first

/// Whether the mode file, read through nn::fs from the card mounted as CARD, has the word `hooks`,
/// as RunnerModeFromFile reads it: a folder is no file, and past MODE_FILE_MAX nothing is read.
static bool ModeAsksHooks(void) {
    static char text[MODE_FILE_MAX + 2u];
    int32_t type = -1;
    if (R_SUCCEEDED(NnFsGetEntryType(&type, MODE_PATH)) && type == NN_FS_ENTRY_DIRECTORY) {
        return false;
    }
    NnFsFileHandle file = { 0 };
    if (R_FAILED(NnFsOpenFile(&file, MODE_PATH, NN_FS_OPEN_READ))) {
        return false;
    }
    int64_t size = 0;
    size_t read = 0;
    if (R_SUCCEEDED(NnFsGetFileSize(&size, file)) && size > 0) {
        const size_t want = (size_t)size < MODE_FILE_MAX + 1u ? (size_t)size : MODE_FILE_MAX + 1u;
        if (R_FAILED(NnFsReadFile(&read, file, 0, text, want))) {
            read = 0;
        }
    }
    NnFsCloseFile(file);
    text[read] = '\0';
    const bool longer = read > MODE_FILE_MAX;
    return ModeFromFile(MODE_SOURCE_FILE, text, longer ? MODE_FILE_MAX : read, longer).hooks;
}

// ------------------------------------------------------------------ the sdmc: device on nn::fs

/// The longest path nn::fs takes, with the mount name before it.
#define CARD_PATH_MAX (sizeof(CARD) + 0x301u)

typedef struct {
    NnFsFileHandle handle;
    int64_t position;
    bool append;
    bool writes;
} CardFile;

typedef struct {
    NnFsDirectoryHandle handle;
    char path[CARD_PATH_MAX];
} CardDirectory;

/// @p path as nn::fs takes it: CARD's own paths as they are, one with no device under CARD.
static bool FullPath(char out[CARD_PATH_MAX], const char *path) {
    const char *const colon = strchr(path, ':');
    const int wrote = colon != NULL ? snprintf(out, CARD_PATH_MAX, "%s", path)
                                    : snprintf(out, CARD_PATH_MAX, CARD ":%s%s",
                                               path[0] == '/' ? "" : "/", path);
    return wrote > 0 && (size_t)wrote < CARD_PATH_MAX;
}

/// The errno an fs result stands for.
static int ErrnoOf(uint32_t rc) {
    if (R_MODULE(rc) != NN_FS_MODULE) {
        return EIO;
    }
    const uint32_t description = R_DESCRIPTION(rc);
    return description == 1u                         ? ENOENT
           : description == 2u                       ? EEXIST
           : description == 7u                       ? EBUSY
           : description == 8u                       ? ENOTEMPTY
           : description >= 30u && description <= 45u ? ENOSPC
                                                      : EIO;
}

/// Sets errno from @p rc and gives what the device call returns on a failure.
static int Fail(struct _reent *r, uint32_t rc) {
    r->_errno = ErrnoOf(rc);
    return -1;
}

static int CardOpen(struct _reent *r, void *file_struct, const char *path, int flags, int mode) {
    (void)mode;
    CardFile *const f = (CardFile *)file_struct;
    memset(f, 0, sizeof(*f));
    char full[CARD_PATH_MAX];
    if (!FullPath(full, path)) {
        r->_errno = ENAMETOOLONG;
        return -1;
    }
    const int access = flags & O_ACCMODE;
    f->writes = access != O_RDONLY;
    f->append = (flags & O_APPEND) != 0;
    // A write past the end needs a file opened to append, so every writer is.
    const int open_mode = access == O_RDONLY   ? NN_FS_OPEN_READ
                          : access == O_WRONLY ? NN_FS_OPEN_WRITE | NN_FS_OPEN_APPEND
                                               : NN_FS_OPEN_READ | NN_FS_OPEN_WRITE |
                                                     NN_FS_OPEN_APPEND;
    if ((flags & O_CREAT) != 0) {
        int32_t type = -1;
        const uint32_t found = NnFsGetEntryType(&type, full);
        if (R_SUCCEEDED(found) && (flags & O_EXCL) != 0) {
            r->_errno = EEXIST;
            return -1;
        }
        if (R_FAILED(found)) {
            const uint32_t made = NnFsCreateFile(full, 0);
            if (R_FAILED(made)) {
                return Fail(r, made);
            }
        }
    }
    uint32_t rc = NnFsOpenFile(&f->handle, full, open_mode);
    if (R_FAILED(rc)) {
        return Fail(r, rc);
    }
    if ((flags & O_TRUNC) != 0 && f->writes) {
        rc = NnFsSetFileSize(f->handle, 0);
        if (R_FAILED(rc)) {
            NnFsCloseFile(f->handle);
            return Fail(r, rc);
        }
    }
    return 0;
}

static int CardClose(struct _reent *r, void *fd) {
    CardFile *const f = (CardFile *)fd;
    // A file written to is flushed before it is closed, which the SDK requires of a writer.
    const uint32_t rc = f->writes ? NnFsFlushFile(f->handle) : 0u;
    NnFsCloseFile(f->handle);
    return R_FAILED(rc) ? Fail(r, rc) : 0;
}

static ssize_t CardRead(struct _reent *r, void *fd, char *ptr, size_t len) {
    CardFile *const f = (CardFile *)fd;
    int64_t size = 0;
    uint32_t rc = NnFsGetFileSize(&size, f->handle);
    if (R_FAILED(rc)) {
        return Fail(r, rc);
    }
    // A read that starts past the end is refused by the SDK; one at the end reads nothing.
    if (f->position >= size) {
        return 0;
    }
    size_t read = 0;
    rc = NnFsReadFile(&read, f->handle, f->position, ptr, len);
    if (R_FAILED(rc)) {
        return Fail(r, rc);
    }
    f->position += (int64_t)read;
    return (ssize_t)read;
}

static ssize_t CardWrite(struct _reent *r, void *fd, const char *ptr, size_t len) {
    CardFile *const f = (CardFile *)fd;
    int64_t size = 0;
    uint32_t rc = NnFsGetFileSize(&size, f->handle);
    if (R_FAILED(rc)) {
        return Fail(r, rc);
    }
    if (f->append) {
        f->position = size;
    } else if (f->position > size) {
        // The gap a seek past the end leaves reads as noughts, as POSIX has it.
        rc = NnFsSetFileSize(f->handle, f->position);
        if (R_FAILED(rc)) {
            return Fail(r, rc);
        }
    }
    // Each write flushed: a journal line has to be on the card before the next case can end the
    // program.
    const NnFsWriteOption flush = { NN_FS_WRITE_FLUSH };
    rc = NnFsWriteFile(f->handle, f->position, ptr, len, &flush);
    if (R_FAILED(rc)) {
        return Fail(r, rc);
    }
    f->position += (int64_t)len;
    return (ssize_t)len;
}

static off_t CardSeek(struct _reent *r, void *fd, off_t pos, int dir) {
    CardFile *const f = (CardFile *)fd;
    int64_t base = 0;
    if (dir == SEEK_CUR) {
        base = f->position;
    } else if (dir == SEEK_END) {
        const uint32_t rc = NnFsGetFileSize(&base, f->handle);
        if (R_FAILED(rc)) {
            return Fail(r, rc);
        }
    } else if (dir != SEEK_SET) {
        r->_errno = EINVAL;
        return -1;
    }
    if (base + (int64_t)pos < 0) {
        r->_errno = EINVAL;
        return -1;
    }
    f->position = base + (int64_t)pos;
    return (off_t)f->position;
}

static int CardFstat(struct _reent *r, void *fd, struct stat *st) {
    CardFile *const f = (CardFile *)fd;
    int64_t size = 0;
    const uint32_t rc = NnFsGetFileSize(&size, f->handle);
    if (R_FAILED(rc)) {
        return Fail(r, rc);
    }
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFREG | 0666;
    st->st_size = (off_t)size;
    st->st_nlink = 1;
    return 0;
}

static int CardStat(struct _reent *r, const char *path, struct stat *st) {
    char full[CARD_PATH_MAX];
    if (!FullPath(full, path)) {
        r->_errno = ENAMETOOLONG;
        return -1;
    }
    int32_t type = -1;
    uint32_t rc = NnFsGetEntryType(&type, full);
    if (R_FAILED(rc)) {
        return Fail(r, rc);
    }
    memset(st, 0, sizeof(*st));
    st->st_nlink = 1;
    if (type == NN_FS_ENTRY_DIRECTORY) {
        st->st_mode = S_IFDIR | 0777;
        return 0;
    }
    NnFsFileHandle file = { 0 };
    rc = NnFsOpenFile(&file, full, NN_FS_OPEN_READ);
    if (R_FAILED(rc)) {
        return Fail(r, rc);
    }
    int64_t size = 0;
    rc = NnFsGetFileSize(&size, file);
    NnFsCloseFile(file);
    if (R_FAILED(rc)) {
        return Fail(r, rc);
    }
    st->st_mode = S_IFREG | 0666;
    st->st_size = (off_t)size;
    return 0;
}

/// A call of @p call on @p path made full: 0, or -1 with errno.
static int OnPath(struct _reent *r, const char *path, uint32_t (*call)(const char *)) {
    char full[CARD_PATH_MAX];
    if (!FullPath(full, path)) {
        r->_errno = ENAMETOOLONG;
        return -1;
    }
    const uint32_t rc = call(full);
    return R_FAILED(rc) ? Fail(r, rc) : 0;
}

static int CardUnlink(struct _reent *r, const char *name) {
    return OnPath(r, name, NnFsDeleteFile);
}

static int CardRmdir(struct _reent *r, const char *name) {
    return OnPath(r, name, NnFsDeleteDirectory);
}

static int CardMkdir(struct _reent *r, const char *path, int mode) {
    (void)mode;
    return OnPath(r, path, NnFsCreateDirectory);
}

static int CardRename(struct _reent *r, const char *old_name, const char *new_name) {
    char from[CARD_PATH_MAX];
    char to[CARD_PATH_MAX];
    if (!FullPath(from, old_name) || !FullPath(to, new_name)) {
        r->_errno = ENAMETOOLONG;
        return -1;
    }
    int32_t type = -1;
    uint32_t rc = NnFsGetEntryType(&type, from);
    if (R_SUCCEEDED(rc)) {
        rc = type == NN_FS_ENTRY_DIRECTORY ? NnFsRenameDirectory(from, to)
                                           : NnFsRenameFile(from, to);
    }
    return R_FAILED(rc) ? Fail(r, rc) : 0;
}

static int CardFtruncate(struct _reent *r, void *fd, off_t len) {
    CardFile *const f = (CardFile *)fd;
    const uint32_t rc = NnFsSetFileSize(f->handle, (int64_t)len);
    return R_FAILED(rc) ? Fail(r, rc) : 0;
}

static int CardFsync(struct _reent *r, void *fd) {
    CardFile *const f = (CardFile *)fd;
    const uint32_t rc = NnFsFlushFile(f->handle);
    return R_FAILED(rc) ? Fail(r, rc) : 0;
}

static DIR_ITER *CardDiropen(struct _reent *r, DIR_ITER *dir_state, const char *path) {
    CardDirectory *const d = (CardDirectory *)dir_state->dirStruct;
    memset(d, 0, sizeof(*d));
    if (!FullPath(d->path, path)) {
        r->_errno = ENAMETOOLONG;
        return NULL;
    }
    const uint32_t rc = NnFsOpenDirectory(&d->handle, d->path,
                                          NN_FS_DIRECTORY_DIRECTORIES | NN_FS_DIRECTORY_FILES);
    if (R_FAILED(rc)) {
        Fail(r, rc);
        return NULL;
    }
    return dir_state;
}

static int CardDirreset(struct _reent *r, DIR_ITER *dir_state) {
    CardDirectory *const d = (CardDirectory *)dir_state->dirStruct;
    NnFsCloseDirectory(d->handle);
    const uint32_t rc = NnFsOpenDirectory(&d->handle, d->path,
                                          NN_FS_DIRECTORY_DIRECTORIES | NN_FS_DIRECTORY_FILES);
    return R_FAILED(rc) ? Fail(r, rc) : 0;
}

static int CardDirnext(struct _reent *r, DIR_ITER *dir_state, char *filename,
                       struct stat *filestat) {
    CardDirectory *const d = (CardDirectory *)dir_state->dirStruct;
    static NnFsDirectoryEntry entry;
    int64_t count = 0;
    const uint32_t rc = NnFsReadDirectory(&count, &entry, d->handle, 1);
    if (R_FAILED(rc)) {
        return Fail(r, rc);
    }
    if (count == 0) {
        r->_errno = ENOENT;
        return -1;
    }
    const size_t length = strnlen(entry.name, sizeof(entry.name));
    if (length > NAME_MAX) {
        r->_errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(filename, entry.name, length);
    filename[length] = '\0';
    if (filestat != NULL) {
        memset(filestat, 0, sizeof(*filestat));
        filestat->st_mode =
            entry.type == FsDirEntryType_Dir ? (S_IFDIR | 0777) : (S_IFREG | 0666);
        filestat->st_size = (off_t)entry.file_size;
        filestat->st_nlink = 1;
    }
    return 0;
}

static int CardDirclose(struct _reent *r, DIR_ITER *dir_state) {
    (void)r;
    CardDirectory *const d = (CardDirectory *)dir_state->dirStruct;
    NnFsCloseDirectory(d->handle);
    return 0;
}

static const devoptab_t CARD_DEVICE = {
    .name = CARD,
    .structSize = sizeof(CardFile),
    .open_r = CardOpen,
    .close_r = CardClose,
    .write_r = CardWrite,
    .read_r = CardRead,
    .seek_r = CardSeek,
    .fstat_r = CardFstat,
    .stat_r = CardStat,
    .unlink_r = CardUnlink,
    .rename_r = CardRename,
    .mkdir_r = CardMkdir,
    .dirStateSize = sizeof(CardDirectory),
    .diropen_r = CardDiropen,
    .dirreset_r = CardDirreset,
    .dirnext_r = CardDirnext,
    .dirclose_r = CardDirclose,
    .ftruncate_r = CardFtruncate,
    .fsync_r = CardFsync,
    .rmdir_r = CardRmdir,
    .lstat_r = CardStat,
};

// ---------------------------------------------------------------------------------- the start

/// Says on the machine's log why the start is going the way it is, for a machine that ends the
/// program before the report opens.
static void Say(const char *text) {
    PlatformLog(text, strlen(text));
}

/// libnx's own start for the card and the clocks, what the IPC mode keeps: time:u, the C library's
/// time zone, fsp-srv and its sdmc: device, the working directory. Each failure ends the program
/// with libnx's result for it, as libnx's start does.
static void StartIpc(void) {
    Result rc = timeInitialize();
    if (R_FAILED(rc)) {
        diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_Time));
    }
    __libnx_init_time();
    rc = fsInitialize();
    if (R_FAILED(rc)) {
        diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_FS));
    }
    fsdevMountSdmc();
    __libnx_init_cwd();
}

/// The card and the clocks through the SDK's functions: the card stays mounted as CARD and is the
/// C library's sdmc: device, and nn::time is initialized as a game's start initializes it.
static void StartHooks(void) {
    const int device = AddDevice(&CARD_DEVICE);
    if (device < 0) {
        Say("edenprobe: hooks mode: the C library has no room for the sdmc device");
        diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_TooManyDevOpTabs));
    }
    setDefaultDevice(device);
    time_rc = NnTimeInitialize();
    time_own = SdkOwnCalls(SDK_TIME_INITIALIZE) != 0u;
}

/// The mode file read through nn::fs, then the start that mode asks for.
static void StartFiles(void) {
    card_rc = NnFsMountSdCard(CARD);
    card_own = SdkFsOwnMount(CARD);
    if (R_FAILED(card_rc)) {
        char said[160];
        snprintf(said, sizeof(said),
                 "edenprobe: nn::fs::MountSdCard(\"" CARD "\") returned 0x%x, so the mode file is "
                 "read as no file: the IPC mode",
                 (unsigned)card_rc);
        Say(said);
    }
    hooks_on = R_SUCCEEDED(card_rc) && ModeAsksHooks();
    if (hooks_on) {
        StartHooks();
        return;
    }
    if (R_SUCCEEDED(card_rc) && !card_own) {
        Say("edenprobe: the machine answered nn::fs::MountSdCard itself and the mode file has no "
            "hooks: the IPC mode opens time:u and fsp-srv now, which such a machine may refuse; "
            "the word hooks in " MODE_PATH " asks through the SDK's functions instead");
    }
    if (R_SUCCEEDED(card_rc)) {
        NnFsUnmount(CARD);
    }
    StartIpc();
}

/// libnx's __appInit, the card and the clocks as the mode file asks.
void __appInit(void) {
    Result rc = smInitialize();
    if (R_FAILED(rc)) {
        diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_SM));
    }
    if (hosversionGet() == 0u && R_SUCCEEDED(setsysInitialize())) {
        SetSysFirmwareVersion firmware;
        if (R_SUCCEEDED(setsysGetFirmwareVersion(&firmware))) {
            hosversionSet(MAKEHOSVERSION(firmware.major, firmware.minor, firmware.micro));
        }
        setsysExit();
    }
    rc = appletInitialize();
    if (R_FAILED(rc)) {
        diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_AM));
    }
    if (__nx_applet_type != AppletType_None) {
        rc = hidInitialize();
        if (R_FAILED(rc)) {
            diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_HID));
        }
    }
    StartFiles();
    if (__nx_win_init != NULL) {
        __nx_win_init();
    }
    if (userAppInit != NULL) {
        userAppInit();
    }
}

/// libnx's __appExit, with the card and the clocks of the mode the program started in.
void __appExit(void) {
    if (userAppExit != NULL) {
        userAppExit();
    }
    if (__nx_win_exit != NULL) {
        __nx_win_exit();
    }
    if (hooks_on) {
        RemoveDevice(CARD ":");
        NnFsUnmount(CARD);
    } else {
        fsdevUnmountAll();
        fsExit();
        timeExit();
    }
    hidExit();
    appletExit();
    smExit();
}

// --------------------------------------------------------------------------- what the run says

void RunSupportMode(void) {
    static const char *const SUITE = "support";
    static const char *const NAME =
        "the services a machine may answer in a game's code are asked the way the mode file says";
    if (!ReportStart(SUITE, NAME)) {
        return;
    }
    const char *const card = card_own ? "the program's own code (fsp-srv)" : "the machine";
    if (hooks_on) {
        CHECK_TRUE(SUITE, NAME, CHECK_NONE, R_SUCCEEDED(time_rc),
                   "hooks: the mode file has the word; nn::fs::MountSdCard(\"" CARD "\") answered "
                   "by %s, nn::time::Initialize by %s with 0x%x",
                   card, time_own ? "the program's own code (time:u)" : "the machine",
                   (unsigned)time_rc);
        return;
    }
    const bool machine_took = R_SUCCEEDED(card_rc) && !card_own;
    CHECK_TRUE(SUITE, NAME, CHECK_NONE, !machine_took,
               "IPC: the mode file has no hooks, so the program opened time:u and fsp-srv at its "
               "start, as libnx does%s; nn::fs::MountSdCard(\"" CARD "\") for the mode file "
               "returned 0x%x, answered by %s",
               machine_took ? ", on a machine that answered nn::fs itself: the word hooks asks it "
                              "through the SDK's functions"
                            : "",
               (unsigned)card_rc, R_SUCCEEDED(card_rc) ? card : "nobody");
}

void HooksReportOwnCode(const char *step) {
    static uint32_t noted[SDK_FUNCTION_COUNT];
    if (!hooks_on) {
        return;
    }
    char said[REPORT_NOTE_MAX - 96];
    if (SdkOwnDescribeSince(said, sizeof(said), noted) > 0u) {
        ReportNote("hooks: %s%s, the program's own code answered %s", step != NULL ? "in step " : "",
                   step != NULL ? step : "at the start", said);
    }
}
