// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// The half of the net family that touches no service: the file that says what the machine's
/// backend promises, and every format the services answer in, read back here rather than in the
/// suites. The `reference` suite pins all of it on the host, so a case that fails under an
/// emulator is a fact about the emulator and not about a parser nobody checked.

/// Where the net family reads what the backend promises, beside the mode file.
#define NET_FILE_PATH "sdmc:/edenprobe/net"

#define NET_NAME_MAX 128u
#define NET_REWRITE_MAX 4u
#define NET_BLOCKED_MAX 16u
#define NET_PROFILE_MAX 4u
/// The two text fields of a friend profile as the guest reads them: 33 bytes of name and 160 of
/// image url, each with room for its terminator.
#define NET_PROFILE_NAME_MAX 33u
#define NET_PROFILE_URL_MAX 160u
#define NET_PROBLEMS_TEXT_MAX 160u

/// One published host rewrite and the address its destination resolves to on the machine that
/// published it.
typedef struct {
    char source[NET_NAME_MAX];
    char destination[NET_NAME_MAX];
    uint8_t address[4];
} NetRewrite;

typedef struct {
    uint64_t account;
    char name[NET_PROFILE_NAME_MAX];
    char url[NET_PROFILE_URL_MAX];
} NetProfile;

/// What `sdmc:/edenprobe/net` says. One `key value...` per line, `#` to the end of a line is a
/// comment, and anything this file does not know is counted and quoted, never fatal:
///
///   session <origin>             an online session in the sense of reden's docs/OnlineService.md
///                                is configured against this backend, so the cases that ask one
///                                run. Without it they are skipped: on a console they would ask
///                                the platform's own servers.
///   account <16 hex digits>      the account id that backend's /api/v1/config publishes.
///   rewrite <source> <dest> <a.b.c.d>
///                                a rewrite it publishes, and the address the destination has.
///   blocked <16 hex digits>      one entry of its /api/v1/blocked, once per entry.
///   profile <16 hex> <url|-> <name...>
///                                one entry of its /api/v1/profiles; the name runs to the end.
///   server <a.b.c.d> <port>      a game server reachable over TCP and TLS.
///   server-name <name>           the name to present to it, and the one its certificate carries.
///   server-cert <path>           the certificate it presents, PEM, relative to sdmc:/edenprobe.
typedef struct {
    bool file_found;
    bool has_session;
    char session[NET_NAME_MAX];
    bool has_account;
    uint64_t account;
    bool has_server;
    uint8_t server_ip[4];
    uint16_t server_port;
    char server_name[NET_NAME_MAX];
    char server_cert[NET_NAME_MAX];
    uint32_t rewrite_count;
    NetRewrite rewrites[NET_REWRITE_MAX];
    uint32_t blocked_count;
    uint64_t blocked[NET_BLOCKED_MAX];
    uint32_t profile_count;
    NetProfile profiles[NET_PROFILE_MAX];
    /// Lines that were not understood, the first few quoted as "line N".
    uint32_t problem_count;
    char problems[NET_PROBLEMS_TEXT_MAX];
} NetConfig;

/// Reads @p text, NULL meaning there was no file. Never fails: what it cannot read it counts.
void NetConfigParse(const char *text, NetConfig *out);

/// The report's one line about it. Returns the length; the text is always terminated.
size_t NetConfigDescribe(char *out, size_t out_size, const NetConfig *config);

/// Whether the file says this machine should have a network at all: it names a backend or a
/// server. Without that, nothing says a disconnected machine is wrong.
bool NetConfigDeclaresOnline(const NetConfig *config);

/// An id that neither the account line nor any profile line names, for asking about somebody the
/// backend does not know. Deterministic: the smallest such id above the account.
uint64_t NetUnknownAccount(const NetConfig *config);

/// Sixteen hex digits, either case, nothing else, not zero: the contract's account id.
bool NetParseAccountId(const char *text, size_t length, uint64_t *out);

/// A dotted quad, each part 0 to 255 with no leading sign and no more than three digits.
bool NetParseIpv4(const char *text, size_t length, uint8_t out[4]);

/// Replaces every `%` in @p name with @p environment, which is what nsd does with the placeholder
/// a name carries. False when the result does not fit.
bool NetExpandPlaceholder(char *out, size_t out_size, const char *name, const char *environment);

/// Whether @p token can travel in an Authorization header as it is: RFC 6750 section 2.1,
/// b64token = 1*( ALPHA / DIGIT / "-" / "." / "_" / "~" / "+" / "/" ) *"=".
bool NetIsB64Token(const uint8_t *token, size_t length);

/// Whether two lists hold the same ids, each as often, in any order.
bool NetSameIds(const uint64_t *a, size_t a_count, const uint64_t *b, size_t b_count);

// Whether an address length recvfrom handed back names a whole sockaddr_in that fits the
// buffer it was given. An emulator that leaves it unset (0) must not be answered with it: one
// crashed on the echo (Ryujinx 1.3.351) instead of refusing it.
bool NetAddressLengthUsable(size_t length, size_t buffer_size);

#define NET_ADDRESSES_MAX 8u

/// What sfdnsres's GetHostByNameRequest serialises: the name, the aliases, and the addresses.
typedef struct {
    char name[NET_NAME_MAX];
    uint32_t alias_count;
    uint16_t address_type;
    uint16_t address_length;
    uint32_t address_count;
    uint8_t addresses[NET_ADDRESSES_MAX][4];
} NetHostEnt;

/// Reads a serialised hostent: the name NUL terminated, a big endian alias count and the aliases,
/// the address type and length as big endian halves, a big endian address count, then each
/// address as a little endian word of the dotted value, first octet in the high byte (the console
/// runs an already big endian value through htonl, and libnx's resolver undoes exactly that).
bool NetParseHostEnt(const uint8_t *data, size_t size, NetHostEnt *out);

typedef struct {
    uint32_t count;
    uint32_t family[NET_ADDRESSES_MAX];
    uint16_t port[NET_ADDRESSES_MAX];
    uint8_t addresses[NET_ADDRESSES_MAX][4];
} NetAddrList;

/// Reads a serialised addrinfo list: entries opening with the big endian magic 0xBEEFCAFE, five
/// big endian words (flags, family, socket type, protocol, address length), the address (family
/// big endian, port and address in the same little endian form as the hostent), a NUL terminated
/// canonical name, and a zero word after the last.
bool NetParseAddrInfo(const uint8_t *data, size_t size, NetAddrList *out);

/// The first CERTIFICATE block of @p pem as DER. Returns its length, or 0.
size_t NetPemToDer(const char *pem, uint8_t *out, size_t out_size);

/// Whether a DER certificate names itself as its issuer, which is what a self-signed one does:
/// no store of authorities the platform ships can hold it, so a verifying handshake has to refuse
/// it. False with @p parsed false when the bytes are not a certificate.
bool NetCertSelfIssued(const uint8_t *der, size_t length, bool *parsed);

/// The first certificate in what a handshake handed back: the chain form (a header with the magic
/// "CertChMN" as a little endian word, a count, then offsets and sizes) or one bare DER
/// certificate, which is what comes back when the chain option was not set.
bool NetFirstServerCert(const uint8_t *data, size_t size, const uint8_t **cert, size_t *length);

typedef struct {
    bool complete;
    int status;
    long content_length; // -1 when the response names none
    size_t body_offset;
    size_t body_length;
} NetHttpResponse;

/// Reads an HTTP/1.x response head and says where its body is. Complete once the head has ended
/// and, when there is a Content-Length, that many bytes have arrived. A response with neither
/// ends when the connection does, which is the caller's to know.
bool NetHttpParse(const char *data, size_t size, NetHttpResponse *out);

/// The request the status document is fetched with.
size_t NetStatusRequest(char *out, size_t out_size, const char *host);

/// Whether @p body is one JSON object whose `maintenances` and `incidents` are both empty
/// arrays, which is a service status document saying nothing is wrong. Anything that is not JSON
/// is not that.
bool NetStatusAllClear(const char *body, size_t length);

/// nn::friends profile as the guest reads it: 256 bytes.
typedef struct {
    uint64_t account;
    char name[NET_PROFILE_NAME_MAX];
    char url[NET_PROFILE_URL_MAX];
    uint8_t valid;
} NetFriendProfile;

#define NET_FRIEND_PROFILE_SIZE 256u

/// Reads one profile: the account id as a little endian word at 0, the name at 8 (33 bytes), the
/// image url at 48 (160 bytes), the validity byte at 208.
void NetFriendProfileDecode(const uint8_t raw[NET_FRIEND_PROFILE_SIZE], NetFriendProfile *out);

/// What nsd should answer for @p name: a destination the backend publishes for it, otherwise the
/// name with every `%` replaced by @p environment, which leaves a plain name as it is (the
/// contract's host rewriting rules 1 and 3). False when the answer does not fit.
bool NetExpectNsd(const NetConfig *config, const char *name, const char *environment, char *out,
                  size_t out_size);

/// The address sfdnsres should answer for @p name, when this program can know one: a dotted
/// address is itself; a published name is its destination's address; signed in, a name carrying
/// the placeholder, or one nsd handed out (@p handed_out, may be NULL), is loopback (rule 4).
/// False when nothing says what the address should be.
bool NetExpectAddress(const NetConfig *config, const char *name, const char *handed_out,
                      uint8_t out[4]);

/// Whether a session answer has arrived: nsd's answer for @p asked is the one expected of a
/// signed in session and differs from the name itself. What the sign-in wait polls for.
bool NetSessionAnswered(const NetConfig *config, const char *asked, const char *environment,
                        const char *answer);

/// The name the sign-in wait asks nsd about: the first published rewrite's source, or a name
/// carrying the placeholder when the backend publishes none.
const char *NetSessionProbeName(const NetConfig *config);

/// The name every dns case that carries the placeholder asks about.
#define NET_PLACEHOLDER_NAME "probe-%.edenprobe.invalid"

/// A profile image url with every `{size}` replaced by the decimal @p pixels, scanning on past
/// each replacement (the contract: avatar_url may contain {size}). False when it does not fit.
bool NetExpandSize(char *out, size_t out_size, const char *url, uint32_t pixels);

/// Whether a profile slot is the one @p expected describes: that account, valid, and its name and
/// picture as published.
bool NetProfileMatches(const NetProfile *expected, const NetFriendProfile *got);

/// Whether a slot answers for @p account and says nobody is known there: the id asked, and not
/// valid. The id is what keeps it in its own slot.
bool NetProfileIsUnknown(uint64_t account, const NetFriendProfile *got);

/// The ssl service's "would block", 2123-0204: what Read, Write and DoHandshake answer on a
/// connection in non-blocking mode that has nothing to go on yet.
#define NET_SSL_WOULD_BLOCK (123u | (204u << 9))

/// Whether @p result is that answer, so a bounded loop asks again rather than giving up.
bool NetSslWouldBlock(uint32_t result);

/// A result code the way the platform prints one, 2xxx-yyyy, and the raw value beside it.
void NetResultText(char *out, size_t out_size, uint32_t result);

#ifdef __cplusplus
}
#endif
