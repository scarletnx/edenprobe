// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <string.h>

#include "netref.h"

static bool IsSpace(char c) {
    return c == ' ' || c == '\t' || c == '\r';
}

static bool IsDigit(char c) {
    return c >= '0' && c <= '9';
}

static int HexValue(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

bool NetParseAccountId(const char *text, size_t length, uint64_t *out) {
    if (text == NULL || length != 16u) {
        return false;
    }
    uint64_t value = 0;
    for (size_t i = 0; i < length; ++i) {
        const int digit = HexValue(text[i]);
        if (digit < 0) {
            return false;
        }
        value = (value << 4) | (uint64_t)digit;
    }
    // The contract refuses zero: it is what an account that never signed in reads as.
    if (value == 0u) {
        return false;
    }
    if (out != NULL) {
        *out = value;
    }
    return true;
}

bool NetParseIpv4(const char *text, size_t length, uint8_t out[4]) {
    if (text == NULL) {
        return false;
    }
    uint8_t parts[4];
    size_t i = 0;
    for (int part = 0; part < 4; ++part) {
        if (part > 0) {
            if (i >= length || text[i] != '.') {
                return false;
            }
            ++i;
        }
        unsigned value = 0;
        size_t digits = 0;
        while (i < length && IsDigit(text[i]) && digits < 4u) {
            value = value * 10u + (unsigned)(text[i] - '0');
            ++i;
            ++digits;
        }
        if (digits == 0u || digits > 3u || value > 255u) {
            return false;
        }
        parts[part] = (uint8_t)value;
    }
    if (i != length) {
        return false;
    }
    memcpy(out, parts, 4);
    return true;
}

/// The next whitespace separated word of a line, or none.
static bool NextWord(const char **cursor, const char *end, const char **word, size_t *length) {
    const char *p = *cursor;
    while (p < end && IsSpace(*p)) {
        ++p;
    }
    if (p >= end) {
        *cursor = p;
        return false;
    }
    const char *start = p;
    while (p < end && !IsSpace(*p)) {
        ++p;
    }
    *word = start;
    *length = (size_t)(p - start);
    *cursor = p;
    return true;
}

/// Copies a word into a fixed field, refusing one that does not fit whole.
static bool CopyWord(char *out, size_t out_size, const char *word, size_t length) {
    if (length + 1u > out_size) {
        return false;
    }
    memcpy(out, word, length);
    out[length] = '\0';
    return true;
}

static void NoteProblem(NetConfig *config, unsigned line) {
    ++config->problem_count;
    const size_t used = strlen(config->problems);
    if (used >= 3u && strcmp(config->problems + used - 3u, "...") == 0) {
        return;
    }
    char item[24];
    snprintf(item, sizeof(item), "%sline %u", used > 0u ? ", " : "", line);
    if (used + strlen(item) + 5u < sizeof(config->problems)) {
        memcpy(config->problems + used, item, strlen(item) + 1u);
    } else {
        snprintf(config->problems + used, sizeof(config->problems) - used, "%s...",
                 used > 0u ? ", " : "");
    }
}

static bool WordIs(const char *word, size_t length, const char *literal) {
    return length == strlen(literal) && memcmp(word, literal, length) == 0;
}

/// One line, without its comment. False when it was not understood.
static bool TakeLine(NetConfig *config, const char *p, const char *end) {
    const char *key;
    size_t key_length;
    if (!NextWord(&p, end, &key, &key_length)) {
        return true; // blank
    }
    const char *a;
    size_t a_length;
    const char *b;
    size_t b_length;
    const char *c;
    size_t c_length;
    const char *extra;
    size_t extra_length;

    if (WordIs(key, key_length, "session")) {
        if (!NextWord(&p, end, &a, &a_length) || NextWord(&p, end, &extra, &extra_length) ||
            !CopyWord(config->session, sizeof(config->session), a, a_length)) {
            return false;
        }
        config->has_session = true;
        return true;
    }
    if (WordIs(key, key_length, "account")) {
        if (!NextWord(&p, end, &a, &a_length) || NextWord(&p, end, &extra, &extra_length) ||
            !NetParseAccountId(a, a_length, &config->account)) {
            return false;
        }
        config->has_account = true;
        return true;
    }
    if (WordIs(key, key_length, "server")) {
        if (!NextWord(&p, end, &a, &a_length) || !NextWord(&p, end, &b, &b_length) ||
            NextWord(&p, end, &extra, &extra_length) ||
            !NetParseIpv4(a, a_length, config->server_ip) || b_length > 5u) {
            return false;
        }
        unsigned port = 0;
        for (size_t i = 0; i < b_length; ++i) {
            if (!IsDigit(b[i])) {
                return false;
            }
            port = port * 10u + (unsigned)(b[i] - '0');
        }
        if (port == 0u || port > 65535u) {
            return false;
        }
        config->server_port = (uint16_t)port;
        config->has_server = true;
        return true;
    }
    if (WordIs(key, key_length, "server-name") || WordIs(key, key_length, "server-cert")) {
        char *field = WordIs(key, key_length, "server-name") ? config->server_name
                                                              : config->server_cert;
        return NextWord(&p, end, &a, &a_length) && !NextWord(&p, end, &extra, &extra_length) &&
               CopyWord(field, NET_NAME_MAX, a, a_length);
    }
    if (WordIs(key, key_length, "rewrite")) {
        if (config->rewrite_count >= NET_REWRITE_MAX || !NextWord(&p, end, &a, &a_length) ||
            !NextWord(&p, end, &b, &b_length) || !NextWord(&p, end, &c, &c_length) ||
            NextWord(&p, end, &extra, &extra_length)) {
            return false;
        }
        NetRewrite *rewrite = &config->rewrites[config->rewrite_count];
        if (!CopyWord(rewrite->source, sizeof(rewrite->source), a, a_length) ||
            !CopyWord(rewrite->destination, sizeof(rewrite->destination), b, b_length) ||
            !NetParseIpv4(c, c_length, rewrite->address)) {
            return false;
        }
        ++config->rewrite_count;
        return true;
    }
    if (WordIs(key, key_length, "blocked")) {
        if (config->blocked_count >= NET_BLOCKED_MAX || !NextWord(&p, end, &a, &a_length) ||
            NextWord(&p, end, &extra, &extra_length) ||
            !NetParseAccountId(a, a_length, &config->blocked[config->blocked_count])) {
            return false;
        }
        ++config->blocked_count;
        return true;
    }
    if (WordIs(key, key_length, "profile")) {
        if (config->profile_count >= NET_PROFILE_MAX || !NextWord(&p, end, &a, &a_length) ||
            !NextWord(&p, end, &b, &b_length)) {
            return false;
        }
        NetProfile *profile = &config->profiles[config->profile_count];
        memset(profile, 0, sizeof(*profile));
        if (!NetParseAccountId(a, a_length, &profile->account)) {
            return false;
        }
        if (!(b_length == 1u && b[0] == '-') &&
            !CopyWord(profile->url, sizeof(profile->url), b, b_length)) {
            return false;
        }
        // The name is the rest of the line with its outer spaces taken off, and may be empty.
        while (p < end && IsSpace(*p)) {
            ++p;
        }
        const char *name_end = end;
        while (name_end > p && IsSpace(name_end[-1])) {
            --name_end;
        }
        if (!CopyWord(profile->name, sizeof(profile->name), p, (size_t)(name_end - p))) {
            return false;
        }
        ++config->profile_count;
        return true;
    }
    return false;
}

void NetConfigParse(const char *text, NetConfig *out) {
    memset(out, 0, sizeof(*out));
    out->file_found = text != NULL;
    if (text == NULL) {
        return;
    }
    unsigned line = 0;
    const char *p = text;
    while (*p != '\0') {
        ++line;
        const char *line_end = strchr(p, '\n');
        if (line_end == NULL) {
            line_end = p + strlen(p);
        }
        const char *content_end = memchr(p, '#', (size_t)(line_end - p));
        if (content_end == NULL) {
            content_end = line_end;
        }
        if (!TakeLine(out, p, content_end)) {
            NoteProblem(out, line);
        }
        p = *line_end == '\n' ? line_end + 1 : line_end;
    }
}

bool NetConfigDeclaresOnline(const NetConfig *config) {
    return config != NULL && (config->has_session || config->has_server);
}

size_t NetConfigDescribe(char *out, size_t out_size, const NetConfig *config) {
    if (out == NULL || out_size == 0u) {
        return 0;
    }
    out[0] = '\0';
    if (config == NULL || !config->file_found) {
        snprintf(out, out_size, "net: no %s, so every case that needs a backend is skipped",
                 NET_FILE_PATH);
        return strlen(out);
    }
    char server[48] = "none";
    if (config->has_server) {
        snprintf(server, sizeof(server), "%u.%u.%u.%u:%u", config->server_ip[0],
                 config->server_ip[1], config->server_ip[2], config->server_ip[3],
                 (unsigned)config->server_port);
    }
    snprintf(out, out_size,
             "net: %s; session %.100s; account %s; %u rewrite%s, %u blocked, %u profile%s; "
             "server %s, name %.60s, certificate %.60s",
             NET_FILE_PATH, config->has_session ? config->session : "none",
             config->has_account ? "named" : "none", (unsigned)config->rewrite_count,
             config->rewrite_count == 1u ? "" : "s", (unsigned)config->blocked_count,
             (unsigned)config->profile_count, config->profile_count == 1u ? "" : "s", server,
             config->server_name[0] != '\0' ? config->server_name : "none",
             config->server_cert[0] != '\0' ? config->server_cert : "none");
    if (config->problem_count > 0u) {
        const size_t used = strlen(out);
        snprintf(out + used, out_size - used, "; %u line%s not understood: %s",
                 (unsigned)config->problem_count, config->problem_count == 1u ? "" : "s",
                 config->problems);
    }
    return strlen(out);
}

uint64_t NetUnknownAccount(const NetConfig *config) {
    uint64_t candidate = config != NULL && config->has_account ? config->account + 1u : 1u;
    for (;;) {
        bool taken = candidate == 0u || (config != NULL && config->has_account &&
                                         candidate == config->account);
        for (uint32_t i = 0; config != NULL && i < config->profile_count && !taken; ++i) {
            taken = config->profiles[i].account == candidate;
        }
        if (!taken) {
            return candidate;
        }
        ++candidate;
    }
}

bool NetExpandPlaceholder(char *out, size_t out_size, const char *name, const char *environment) {
    if (out == NULL || out_size == 0u || name == NULL || environment == NULL) {
        return false;
    }
    const size_t environment_length = strlen(environment);
    size_t w = 0;
    for (size_t r = 0; name[r] != '\0'; ++r) {
        const char *piece = name[r] == '%' ? environment : &name[r];
        const size_t piece_length = name[r] == '%' ? environment_length : 1u;
        if (w + piece_length + 1u > out_size) {
            out[0] = '\0';
            return false;
        }
        memcpy(out + w, piece, piece_length);
        w += piece_length;
    }
    out[w] = '\0';
    return true;
}

bool NetIsB64Token(const uint8_t *token, size_t length) {
    if (token == NULL || length == 0u) {
        return false;
    }
    size_t i = 0;
    for (; i < length; ++i) {
        const uint8_t c = token[i];
        const bool body = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                          (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' ||
                          c == '~' || c == '+' || c == '/';
        if (!body) {
            break;
        }
    }
    if (i == 0u) {
        return false; // the grammar wants at least one character before any padding
    }
    for (; i < length; ++i) {
        if (token[i] != '=') {
            return false;
        }
    }
    return true;
}

bool NetSameIds(const uint64_t *a, size_t a_count, const uint64_t *b, size_t b_count) {
    if (a_count != b_count) {
        return false;
    }
    for (size_t i = 0; i < a_count; ++i) {
        size_t in_a = 0;
        size_t in_b = 0;
        for (size_t j = 0; j < a_count; ++j) {
            in_a += a[j] == a[i] ? 1u : 0u;
            in_b += b[j] == a[i] ? 1u : 0u;
        }
        if (in_a != in_b) {
            return false;
        }
    }
    return true;
}

static uint32_t ReadBe32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static uint16_t ReadBe16(const uint8_t *p) {
    return (uint16_t)((unsigned)p[0] << 8 | p[1]);
}

/// The address form both serialisations share: a little endian word whose high byte is the
/// first octet.
static void ReadSwappedAddress(const uint8_t *p, uint8_t out[4]) {
    out[0] = p[3];
    out[1] = p[2];
    out[2] = p[1];
    out[3] = p[0];
}

/// Skips a NUL terminated string, false when there is no terminator before @p size.
static bool SkipString(const uint8_t *data, size_t size, size_t *at) {
    const uint8_t *nul = memchr(data + *at, 0, size - *at);
    if (nul == NULL) {
        return false;
    }
    *at = (size_t)(nul - data) + 1u;
    return true;
}

bool NetParseHostEnt(const uint8_t *data, size_t size, NetHostEnt *out) {
    if (data == NULL || out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    size_t at = 0;
    if (!SkipString(data, size, &at) || at > sizeof(out->name)) {
        return false;
    }
    memcpy(out->name, data, at);
    if (size - at < 4u) {
        return false;
    }
    out->alias_count = ReadBe32(data + at);
    at += 4u;
    for (uint32_t i = 0; i < out->alias_count; ++i) {
        if (at >= size || !SkipString(data, size, &at)) {
            return false;
        }
    }
    if (size - at < 8u) {
        return false;
    }
    out->address_type = ReadBe16(data + at);
    out->address_length = ReadBe16(data + at + 2u);
    out->address_count = ReadBe32(data + at + 4u);
    at += 8u;
    if (out->address_length != 4u || out->address_count > (size - at) / 4u) {
        return false;
    }
    for (uint32_t i = 0; i < out->address_count && i < NET_ADDRESSES_MAX; ++i) {
        ReadSwappedAddress(data + at + 4u * i, out->addresses[i]);
    }
    return true;
}

bool NetParseAddrInfo(const uint8_t *data, size_t size, NetAddrList *out) {
    if (data == NULL || out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    size_t at = 0;
    for (;;) {
        if (size - at < 4u) {
            return false; // no sentinel
        }
        const uint32_t magic = ReadBe32(data + at);
        if (magic == 0u) {
            return true;
        }
        if (magic != 0xBEEFCAFEu || size - at < 24u) {
            return false;
        }
        const uint32_t family = ReadBe32(data + at + 8u);
        const uint32_t address_length = ReadBe32(data + at + 20u);
        at += 24u;
        if (address_length > size - at) {
            return false;
        }
        if (out->count < NET_ADDRESSES_MAX) {
            out->family[out->count] = family;
            if (address_length >= 8u) {
                // The port crosses the same htonl as the address does, so it lands little endian.
                out->port[out->count] = (uint16_t)(data[at + 2u] | (unsigned)data[at + 3u] << 8);
                ReadSwappedAddress(data + at + 4u, out->addresses[out->count]);
            }
        }
        ++out->count;
        at += address_length;
        if (at >= size || !SkipString(data, size, &at)) {
            return false;
        }
    }
}

static int Base64Value(char c) {
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9') {
        return c - '0' + 52;
    }
    if (c == '+') {
        return 62;
    }
    if (c == '/') {
        return 63;
    }
    return -1;
}

size_t NetPemToDer(const char *pem, uint8_t *out, size_t out_size) {
    static const char BEGIN[] = "-----BEGIN CERTIFICATE-----";
    static const char END[] = "-----END CERTIFICATE-----";
    if (pem == NULL || out == NULL) {
        return 0;
    }
    const char *start = strstr(pem, BEGIN);
    if (start == NULL) {
        return 0;
    }
    start += sizeof(BEGIN) - 1u;
    const char *stop = strstr(start, END);
    if (stop == NULL) {
        return 0;
    }
    uint32_t bits = 0;
    int held = 0;
    size_t w = 0;
    bool padding = false;
    for (const char *p = start; p < stop; ++p) {
        if (*p == '\n' || *p == '\r' || *p == ' ' || *p == '\t') {
            continue;
        }
        if (*p == '=') {
            padding = true;
            continue;
        }
        const int value = Base64Value(*p);
        if (value < 0 || padding) {
            return 0;
        }
        bits = bits << 6 | (uint32_t)value;
        held += 6;
        if (held >= 8) {
            held -= 8;
            if (w >= out_size) {
                return 0;
            }
            out[w++] = (uint8_t)(bits >> held);
        }
    }
    return w;
}

/// One DER element: its tag, where its contents are and how long, and where the next one starts.
typedef struct {
    uint8_t tag;
    size_t header;
    size_t length;
} Der;

static bool DerRead(const uint8_t *data, size_t size, Der *out) {
    if (size < 2u) {
        return false;
    }
    out->tag = data[0];
    size_t length = data[1];
    size_t header = 2u;
    if (length & 0x80u) {
        const size_t count = length & 0x7Fu;
        if (count == 0u || count > 4u || size < 2u + count) {
            return false;
        }
        length = 0;
        for (size_t i = 0; i < count; ++i) {
            length = length << 8 | data[2u + i];
        }
        header += count;
    }
    if (length > size - header) {
        return false;
    }
    out->header = header;
    out->length = length;
    return true;
}

bool NetCertSelfIssued(const uint8_t *der, size_t length, bool *parsed) {
    if (parsed != NULL) {
        *parsed = false;
    }
    Der certificate;
    Der tbs;
    if (der == NULL || !DerRead(der, length, &certificate) || certificate.tag != 0x30u) {
        return false;
    }
    const uint8_t *p = der + certificate.header;
    if (!DerRead(p, certificate.length, &tbs) || tbs.tag != 0x30u) {
        return false;
    }
    const uint8_t *field = p + tbs.header;
    size_t left = tbs.length;
    // version [0] when present, then serial, signature algorithm, issuer, validity, subject.
    const uint8_t *elements[6];
    size_t sizes[6];
    int count = 0;
    while (count < 6 && left > 0u) {
        Der element;
        if (!DerRead(field, left, &element)) {
            return false;
        }
        const size_t whole = element.header + element.length;
        if (!(count == 0 && element.tag == 0xA0u)) {
            elements[count] = field;
            sizes[count] = whole;
            ++count;
        }
        field += whole;
        left -= whole;
    }
    if (count < 5 || elements[2][0] != 0x30u || elements[4][0] != 0x30u) {
        return false;
    }
    if (parsed != NULL) {
        *parsed = true;
    }
    return sizes[2] == sizes[4] && memcmp(elements[2], elements[4], sizes[2]) == 0;
}

bool NetFirstServerCert(const uint8_t *data, size_t size, const uint8_t **cert, size_t *length) {
    static const uint8_t MAGIC[8] = { 'C', 'e', 'r', 't', 'C', 'h', 'M', 'N' };
    if (data == NULL || cert == NULL || length == NULL) {
        return false;
    }
    if (size >= 24u && memcmp(data, MAGIC, sizeof(MAGIC)) == 0) {
        const uint32_t count = (uint32_t)data[8] | (uint32_t)data[9] << 8 |
                               (uint32_t)data[10] << 16 | (uint32_t)data[11] << 24;
        const uint32_t entry_size = (uint32_t)data[16] | (uint32_t)data[17] << 8 |
                                    (uint32_t)data[18] << 16 | (uint32_t)data[19] << 24;
        const uint32_t offset = (uint32_t)data[20] | (uint32_t)data[21] << 8 |
                                (uint32_t)data[22] << 16 | (uint32_t)data[23] << 24;
        if (count == 0u || offset > size || entry_size > size - offset) {
            return false;
        }
        *cert = data + offset;
        *length = entry_size;
        return true;
    }
    Der whole;
    if (!DerRead(data, size, &whole) || whole.tag != 0x30u) {
        return false;
    }
    *cert = data;
    *length = whole.header + whole.length;
    return true;
}

static char Lower(char c) {
    return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
}

static bool StartsWithNoCase(const char *p, size_t size, const char *prefix) {
    const size_t n = strlen(prefix);
    if (size < n) {
        return false;
    }
    for (size_t i = 0; i < n; ++i) {
        if (Lower(p[i]) != prefix[i]) {
            return false;
        }
    }
    return true;
}

bool NetHttpParse(const char *data, size_t size, NetHttpResponse *out) {
    if (data == NULL || out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->content_length = -1;
    // "HTTP/1.x NNN" is twelve characters and the status is the three after the space.
    if (size < 12u || memcmp(data, "HTTP/1.", 7) != 0 || data[8] != ' ' || !IsDigit(data[9]) ||
        !IsDigit(data[10]) || !IsDigit(data[11])) {
        return false;
    }
    out->status = (data[9] - '0') * 100 + (data[10] - '0') * 10 + (data[11] - '0');
    size_t at = 0;
    for (;;) {
        const char *eol = NULL;
        for (size_t i = at; i + 1u < size; ++i) {
            if (data[i] == '\r' && data[i + 1u] == '\n') {
                eol = data + i;
                break;
            }
        }
        if (eol == NULL) {
            return true; // head still arriving
        }
        const size_t line_length = (size_t)(eol - (data + at));
        if (line_length == 0u) {
            out->body_offset = at + 2u;
            break;
        }
        if (at > 0u && StartsWithNoCase(data + at, line_length, "content-length:")) {
            long value = 0;
            size_t i = strlen("content-length:");
            while (i < line_length && IsSpace(data[at + i])) {
                ++i;
            }
            size_t digits = 0;
            while (i < line_length && IsDigit(data[at + i]) && digits < 9u) {
                value = value * 10 + (data[at + i] - '0');
                ++i;
                ++digits;
            }
            if (digits == 0u) {
                return false;
            }
            out->content_length = value;
        }
        at += line_length + 2u;
    }
    out->body_length = size - out->body_offset;
    if (out->content_length >= 0) {
        if (out->body_length >= (size_t)out->content_length) {
            out->body_length = (size_t)out->content_length;
            out->complete = true;
        }
    }
    return true;
}

size_t NetStatusRequest(char *out, size_t out_size, const char *host) {
    const int wrote = snprintf(out, out_size,
                               "GET /service_status_hac.json HTTP/1.1\r\nHost: %s\r\n"
                               "Accept: application/json\r\nConnection: close\r\n\r\n",
                               host != NULL ? host : "");
    return wrote < 0 || (size_t)wrote >= out_size ? 0u : (size_t)wrote;
}

// A JSON reader that only has to say whether text is JSON and, at the top level, which keys hold
// an empty array. Depth is bounded so a hostile body cannot run the stack out.
typedef struct {
    const char *p;
    const char *end;
} Json;

static void JsonSpace(Json *j) {
    while (j->p < j->end && (*j->p == ' ' || *j->p == '\t' || *j->p == '\n' || *j->p == '\r')) {
        ++j->p;
    }
}

static bool JsonString(Json *j, const char **start, size_t *length) {
    if (j->p >= j->end || *j->p != '"') {
        return false;
    }
    ++j->p;
    const char *s = j->p;
    while (j->p < j->end && *j->p != '"') {
        if ((unsigned char)*j->p < 0x20u) {
            return false;
        }
        if (*j->p == '\\') {
            ++j->p;
            if (j->p >= j->end) {
                return false;
            }
        }
        ++j->p;
    }
    if (j->p >= j->end) {
        return false;
    }
    if (start != NULL) {
        *start = s;
        *length = (size_t)(j->p - s);
    }
    ++j->p;
    return true;
}

static bool JsonValue(Json *j, int depth);

static bool JsonLiteral(Json *j, const char *word) {
    const size_t n = strlen(word);
    if ((size_t)(j->end - j->p) < n || memcmp(j->p, word, n) != 0) {
        return false;
    }
    j->p += n;
    return true;
}

static bool JsonNumber(Json *j) {
    const char *start = j->p;
    if (j->p < j->end && *j->p == '-') {
        ++j->p;
    }
    size_t digits = 0;
    while (j->p < j->end && (IsDigit(*j->p) || *j->p == '.' || *j->p == 'e' || *j->p == 'E' ||
                             *j->p == '+' || *j->p == '-')) {
        digits += IsDigit(*j->p) ? 1u : 0u;
        ++j->p;
    }
    return j->p > start && digits > 0u;
}

/// An array, and whether it was empty.
static bool JsonArray(Json *j, int depth, bool *empty) {
    ++j->p; // '['
    JsonSpace(j);
    if (j->p < j->end && *j->p == ']') {
        ++j->p;
        *empty = true;
        return true;
    }
    *empty = false;
    for (;;) {
        if (!JsonValue(j, depth + 1)) {
            return false;
        }
        JsonSpace(j);
        if (j->p < j->end && *j->p == ',') {
            ++j->p;
            continue;
        }
        if (j->p < j->end && *j->p == ']') {
            ++j->p;
            return true;
        }
        return false;
    }
}

/// An object; at the top level, which of the two status keys hold an empty array.
static bool JsonObject(Json *j, int depth, bool *maintenances_clear, bool *incidents_clear) {
    ++j->p; // '{'
    JsonSpace(j);
    if (j->p < j->end && *j->p == '}') {
        ++j->p;
        return true;
    }
    for (;;) {
        JsonSpace(j);
        const char *key;
        size_t key_length;
        if (!JsonString(j, &key, &key_length)) {
            return false;
        }
        JsonSpace(j);
        if (j->p >= j->end || *j->p != ':') {
            return false;
        }
        ++j->p;
        JsonSpace(j);
        bool *clear = NULL;
        if (depth == 0 && key_length == 12u && memcmp(key, "maintenances", 12) == 0) {
            clear = maintenances_clear;
        } else if (depth == 0 && key_length == 9u && memcmp(key, "incidents", 9) == 0) {
            clear = incidents_clear;
        }
        if (clear != NULL && j->p < j->end && *j->p == '[') {
            bool empty = false;
            if (!JsonArray(j, depth + 1, &empty)) {
                return false;
            }
            *clear = empty;
        } else if (!JsonValue(j, depth + 1)) {
            return false;
        } else if (clear != NULL) {
            *clear = false;
        }
        JsonSpace(j);
        if (j->p < j->end && *j->p == ',') {
            ++j->p;
            continue;
        }
        if (j->p < j->end && *j->p == '}') {
            ++j->p;
            return true;
        }
        return false;
    }
}

static bool JsonValue(Json *j, int depth) {
    if (depth > 32) {
        return false;
    }
    JsonSpace(j);
    if (j->p >= j->end) {
        return false;
    }
    switch (*j->p) {
    case '{': {
        bool ignored_a = false;
        bool ignored_b = false;
        return JsonObject(j, depth, &ignored_a, &ignored_b);
    }
    case '[': {
        bool empty = false;
        return JsonArray(j, depth, &empty);
    }
    case '"':
        return JsonString(j, NULL, NULL);
    case 't':
        return JsonLiteral(j, "true");
    case 'f':
        return JsonLiteral(j, "false");
    case 'n':
        return JsonLiteral(j, "null");
    default:
        return JsonNumber(j);
    }
}

bool NetStatusAllClear(const char *body, size_t length) {
    if (body == NULL) {
        return false;
    }
    Json j = { body, body + length };
    JsonSpace(&j);
    if (j.p >= j.end || *j.p != '{') {
        return false;
    }
    bool maintenances_clear = false;
    bool incidents_clear = false;
    if (!JsonObject(&j, 0, &maintenances_clear, &incidents_clear)) {
        return false;
    }
    JsonSpace(&j);
    return j.p == j.end && maintenances_clear && incidents_clear;
}

/// Copies a fixed guest field into a C string, stopping at the first NUL or at the field's end.
static void CopyField(char *out, size_t out_size, const uint8_t *field, size_t field_size) {
    size_t n = 0;
    while (n < field_size && n + 1u < out_size && field[n] != 0u) {
        out[n] = (char)field[n];
        ++n;
    }
    out[n] = '\0';
}

void NetFriendProfileDecode(const uint8_t raw[NET_FRIEND_PROFILE_SIZE], NetFriendProfile *out) {
    memset(out, 0, sizeof(*out));
    for (int i = 7; i >= 0; --i) {
        out->account = out->account << 8 | raw[i];
    }
    CopyField(out->name, sizeof(out->name), raw + 8u, NET_PROFILE_NAME_MAX);
    CopyField(out->url, sizeof(out->url), raw + 48u, NET_PROFILE_URL_MAX);
    out->valid = raw[208];
}

bool NetExpectNsd(const NetConfig *config, const char *name, const char *environment, char *out,
                  size_t out_size) {
    if (name == NULL || out == NULL || out_size == 0u) {
        return false;
    }
    for (uint32_t i = 0; config != NULL && i < config->rewrite_count; ++i) {
        if (strcmp(config->rewrites[i].source, name) == 0) {
            const size_t length = strlen(config->rewrites[i].destination);
            if (length + 1u > out_size) {
                return false;
            }
            memcpy(out, config->rewrites[i].destination, length + 1u);
            return true;
        }
    }
    return NetExpandPlaceholder(out, out_size, name, environment != NULL ? environment : "");
}

static bool IsDotted(const char *name, uint8_t out[4]) {
    return NetParseIpv4(name, strlen(name), out);
}

bool NetExpectAddress(const NetConfig *config, const char *name, const char *handed_out,
                      uint8_t out[4]) {
    static const uint8_t LOOPBACK[4] = { 127, 0, 0, 1 };
    if (name == NULL || out == NULL) {
        return false;
    }
    if (IsDotted(name, out)) {
        return true;
    }
    for (uint32_t i = 0; config != NULL && i < config->rewrite_count; ++i) {
        if (strcmp(config->rewrites[i].source, name) == 0) {
            memcpy(out, config->rewrites[i].address, 4);
            return true;
        }
    }
    if (config != NULL && config->has_session &&
        (strchr(name, '%') != NULL || (handed_out != NULL && strcmp(name, handed_out) == 0))) {
        memcpy(out, LOOPBACK, 4);
        return true;
    }
    return false;
}

const char *NetSessionProbeName(const NetConfig *config) {
    return config != NULL && config->rewrite_count > 0u ? config->rewrites[0].source
                                                        : NET_PLACEHOLDER_NAME;
}

bool NetSessionAnswered(const NetConfig *config, const char *asked, const char *environment,
                        const char *answer) {
    char expected[NET_NAME_MAX + 64];
    return asked != NULL && answer != NULL &&
           NetExpectNsd(config, asked, environment, expected, sizeof(expected)) &&
           strcmp(expected, asked) != 0 && strcmp(answer, expected) == 0;
}

bool NetExpandSize(char *out, size_t out_size, const char *url, uint32_t pixels) {
    static const char PLACEHOLDER[] = "{size}";
    if (out == NULL || out_size == 0u || url == NULL) {
        return false;
    }
    char digits[16];
    snprintf(digits, sizeof(digits), "%u", (unsigned)pixels);
    const size_t digits_length = strlen(digits);
    size_t w = 0;
    for (size_t r = 0; url[r] != '\0';) {
        const bool here = strncmp(url + r, PLACEHOLDER, sizeof(PLACEHOLDER) - 1u) == 0;
        const char *piece = here ? digits : url + r;
        const size_t piece_length = here ? digits_length : 1u;
        if (w + piece_length + 1u > out_size) {
            out[0] = '\0';
            return false;
        }
        memcpy(out + w, piece, piece_length);
        w += piece_length;
        r += here ? sizeof(PLACEHOLDER) - 1u : 1u;
    }
    out[w] = '\0';
    return true;
}

bool NetProfileMatches(const NetProfile *expected, const NetFriendProfile *got) {
    return expected != NULL && got != NULL && got->valid == 1u &&
           got->account == expected->account && strcmp(got->name, expected->name) == 0 &&
           strcmp(got->url, expected->url) == 0;
}

bool NetProfileIsUnknown(uint64_t account, const NetFriendProfile *got) {
    return got != NULL && got->valid == 0u && got->account == account;
}

bool NetSslWouldBlock(uint32_t result) {
    return result == NET_SSL_WOULD_BLOCK;
}

void NetResultText(char *out, size_t out_size, uint32_t result) {
    const unsigned module = result & 0x1FFu;
    const unsigned description = (result >> 9) & 0x1FFFu;
    snprintf(out, out_size, "%04u-%04u (0x%x)", 2000u + module, description, (unsigned)result);
}
