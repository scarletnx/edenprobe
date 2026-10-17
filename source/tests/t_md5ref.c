// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../check.h"
#include "../md5.h"
#include "../report.h"
#include "tests.h"

// The vendored MD5 against RFC 1321's own test suite (appendix A.5), whole and fed in pieces
// that straddle its 64-byte blocks: the nvdec suite's verdicts are MD5s compared with lists made
// by other implementations, so this one has to give theirs.

static const char *const SUITE = "reference";

static const struct {
    const char *message;
    const char *digest;
} RFC1321[] = {
    {"", "d41d8cd98f00b204e9800998ecf8427e"},
    {"a", "0cc175b9c0f1b6a831c399e269772661"},
    {"abc", "900150983cd24fb0d6963f7d28e17f72"},
    {"message digest", "f96b697d7cb7938d525a2f31aaf161d0"},
    {"abcdefghijklmnopqrstuvwxyz", "c3fcd3d76192e4007dfb496cca67e13b"},
    {"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789",
     "d174ab98d277d9f5a5611c2c9f419d9f"},
    {"12345678901234567890123456789012345678901234567890123456789012345678901234567890",
     "57edf4a22be3c955ac49da2e2107b67a"},
};

static void Hex(const unsigned char digest[16], char out[33]) {
    for (int i = 0; i < 16; ++i) {
        snprintf(out + 2 * i, 3, "%02x", digest[i]);
    }
}

/// The message's digest, fed @p piece bytes at a time (all at once when nought).
static void Digest(const char *message, size_t piece, char out[33]) {
    MD5_CTX ctx;
    unsigned char digest[16];
    const size_t size = strlen(message);
    MD5_Init(&ctx);
    for (size_t at = 0; at < size;) {
        const size_t n = piece == 0u || piece > size - at ? size - at : piece;
        MD5_Update(&ctx, message + at, (unsigned long)n);
        at += n;
    }
    MD5_Final(digest, &ctx);
    Hex(digest, out);
}

void RunReferenceMd5(void) {
    static const size_t PIECES[] = {0u, 1u, 7u, 63u, 64u, 65u};
    char name[128];
    for (size_t i = 0; i < sizeof(RFC1321) / sizeof(RFC1321[0]); ++i) {
        uint32_t wrong = 0;
        char got[33] = "", first[33] = "";
        for (size_t p = 0; p < sizeof(PIECES) / sizeof(PIECES[0]); ++p) {
            Digest(RFC1321[i].message, PIECES[p], got);
            if (strcmp(got, RFC1321[i].digest) != 0 && wrong++ == 0u) {
                memcpy(first, got, sizeof(first));
            }
        }
        snprintf(name, sizeof(name), "md5: RFC 1321's \"%.24s%s\" whole and in pieces",
                 RFC1321[i].message, strlen(RFC1321[i].message) > 24u ? "..." : "");
        CHECK_TRUE(SUITE, name, CHECK_NONE, wrong == 0u, "%u of 6 feedings differ; first %s",
                   (unsigned)wrong, first);
    }
    // A million a's, the long message suites add to the RFC's, fed in pieces of 1000 bytes:
    // 15625 blocks, most of them straddling two pieces.
    static char block[1024];
    memset(block, 'a', sizeof(block));
    MD5_CTX ctx;
    unsigned char digest[16];
    char got[33];
    MD5_Init(&ctx);
    for (int i = 0; i < 1000; ++i) {
        MD5_Update(&ctx, block, 1000u);
    }
    MD5_Final(digest, &ctx);
    Hex(digest, got);
    CHECK_TRUE(SUITE, "md5: a million a's", CHECK_NONE,
               strcmp(got, "7707d6ae4e027c70eea2a935c2296f21") == 0, "%s", got);
}
