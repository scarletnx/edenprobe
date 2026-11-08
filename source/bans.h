// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// The ban list, source/bans.txt: the cases that end or hang the program every time on one machine. A case a line
/// names is skipped as `banned` on the machine the mode file's machine=<name>[@<version>] says, unless the word
/// `unban` asks for it; a run with no machine holds no line. tools/bans.py writes the table, source/banlist.c.
typedef struct {
    const char *suite;
    /// The case's name, as its line in the report has it.
    const char *name;
    /// One of BAN_MACHINES.
    const char *machine;
    /// `*`, a list of versions comma separated, or bounds on a version of dotted numbers (BanVersionsHold).
    const char *versions;
    /// What goes wrong, as it reads after the case's name.
    const char *breaks;
    /// The run that showed it.
    const char *source;
    /// The mode words that run it again on that machine.
    const char *retest;
    /// When it was last seen, and what was seen.
    const char *checked;
    /// Its line in source/bans.txt.
    uint32_t line;
} BanLine;

/// The table tools/bans.py writes.
extern const char *const BAN_MACHINES[];
extern const size_t BAN_MACHINE_COUNT;
extern const BanLine BAN_LINES[];
extern const size_t BAN_LINE_COUNT;

/// The longest machine name and version a machine= word takes.
#define BAN_MACHINE_MAX 24u
#define BAN_VERSION_MAX 32u

/// Whether @p name is one of the machines source/bans.txt names.
bool BanMachineKnown(const char *name);

/// The machines, space separated, for a message that names them.
const char *BanMachineList(void);

/// Whether @p version is one a machine= word may carry: 1 to BAN_VERSION_MAX letters, digits, dots, dashes and
/// underscores.
bool BanVersionValid(const char *version);

/// Whether a line's versions field is one the program reads: `*`; versions comma separated, each BanVersionValid;
/// or bounds, `< V`, `<= V`, `>= V`, `> V`, or a lower one then an upper one, on dotted numbers (1.2.3, v1.2).
bool BanVersionsValid(const char *versions);

/// Whether @p versions holds for @p version, "" or NULL when the run gave none: `*` holds for every version and for
/// none; a list holds for a version it names; bounds hold for a version of dotted numbers inside them.
bool BanVersionsHold(const char *versions, const char *version);

/// The lines the program holds runs to: source/bans.txt's, or the ones a judge's check put in their place.
size_t BanCount(void);
const BanLine *BanAt(size_t index);

/// The line that bans @p suite's case @p key (its name as the journal keeps it) on @p machine at @p version, or NULL.
/// No machine, NULL or "", holds no line.
const BanLine *BanFind(const char *suite, const char *key, const char *machine, const char *version);

/// How many lines name @p machine, and of those how many hold at @p version.
size_t BanCountFor(const char *machine, const char *version, size_t *holding);

/// The note a new run's report carries about the ban list, without `bans: ` in front: how many lines hold for
/// @p machine at @p version, "" for none given, and what @p unban does to their cases. Nothing, and 0, for a run
/// with no machine and no unban, which reports what it always did. Returns the length; always terminated.
size_t BanDescribe(char *out, size_t out_size, const char *machine, const char *version, bool unban);

/// For the judge's checks: the lines to hold runs to in place of source/bans.txt's; NULL puts those back.
void BanUseTable(const BanLine *lines, size_t count);

#ifdef __cplusplus
}
#endif
