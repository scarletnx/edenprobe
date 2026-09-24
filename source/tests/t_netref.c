// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <string.h>

#include "../check.h"
#include "../netref.h"
#include "../report.h"
#include "tests.h"

// The net family's judge, with no service anywhere: the file that says what the backend
// promises, every format the services answer in, and the expectations built from them. Each
// fixture is written out by hand from the format's description, never captured from a machine,
// so a case that fails under an emulator cannot be the parser agreeing with its own past.

static const char *const SUITE = "reference";

// A self-signed root and a certificate it issued, made once with openssl and pinned by their
// length, their first eight and last four bytes and the sum of every byte, as openssl printed
// them (`openssl x509 -outform der`).
static const char ROOT_PEM[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIBkjCCATmgAwIBAgIUSW3GNwQ3XBfBWbhd5JsX52Sm6ncwCgYIKoZIzj0EAwIw\n"
    "HjEcMBoGA1UEAwwTZWRlbnByb2JlIHRlc3Qgcm9vdDAgFw0yNjA5MjMxMjE2NTJa\n"
    "GA8yMTI2MDgzMDEyMTY1MlowHjEcMBoGA1UEAwwTZWRlbnByb2JlIHRlc3Qgcm9v\n"
    "dDBZMBMGByqGSM49AgEGCCqGSM49AwEHA0IABCnaRsVITrpqllmsupnNOgq0lS87\n"
    "yyerW50Z1alchwWyZSQtHBLy0jRzyXSeIwbsU8N+lgRlpaN2JzpHyDKrvU+jUzBR\n"
    "MB0GA1UdDgQWBBT+IrNC3vZ/Wh62mzVsHtq6Wb1lEzAfBgNVHSMEGDAWgBT+IrNC\n"
    "3vZ/Wh62mzVsHtq6Wb1lEzAPBgNVHRMBAf8EBTADAQH/MAoGCCqGSM49BAMCA0cA\n"
    "MEQCID3ukp4t6Hs5zwvEF8+qpNNaexoPysrcKhxEw8VQqqs1AiACqTNy2aSPsc6Z\n"
    "n8ZGQZ2ocr23XrhOdgI22SV6QP0GdQ==\n"
    "-----END CERTIFICATE-----\n";
static const char LEAF_PEM[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIBZDCCAQmgAwIBAgIBAjAKBggqhkjOPQQDAjAeMRwwGgYDVQQDDBNlZGVucHJv\n"
    "YmUgdGVzdCByb290MCAXDTI2MDkyMzEyMTY1M1oYDzIxMjYwODMwMTIxNjUzWjAS\n"
    "MRAwDgYDVQQDDAdyZWRlbmV4MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEKfHV\n"
    "Va7hmMmzZtDhAh3yr/I9uqa4AFA1wBqjYs/Pi5Ndk0fPzcWNPQJV13mJYsH/Z8Xs\n"
    "xboB8Pz3c0zuTJ7qnaNCMEAwHQYDVR0OBBYEFK+zfJ9EYz9ckATAMUGKT0Rv1WcU\n"
    "MB8GA1UdIwQYMBaAFP4is0Le9n9aHrabNWwe2rpZvWUTMAoGCCqGSM49BAMCA0kA\n"
    "MEYCIQD8gJCBxaydFH/ksb8xqhK1wdpThNIbycFNblPlyS41vAIhAKBxvh9Ou4kG\n"
    "bGpzUfx9Q2/FC89OqXpdjrXgirzhRHWT\n"
    "-----END CERTIFICATE-----\n";

static uint32_t ByteSum(const uint8_t *data, size_t length) {
    uint32_t sum = 0;
    for (size_t i = 0; i < length; ++i) {
        sum += data[i];
    }
    return sum;
}

static void CheckNetFile(void) {
    static const char TEXT[] =
        "# written by tools/net-fixture.sh\n"
        "session http://127.0.0.1:41234\n"
        "account BB00000100000002   # from /api/v1/config\n"
        "rewrite game.edenprobe.invalid localhost 127.0.0.1\n"
        "blocked bb00000100000009\n"
        "blocked BB0000010000000A\n"
        "profile bb00000100000002 http://127.0.0.1:41234/avatar/{size}.png   edenprobe  player \n"
        "profile bb00000100000003 -\n"
        "server 127.0.0.1 8443\r\n"
        "server-name redenex\n"
        "server-cert net-server.pem\n"
        "\n"
        "sesion http://typo\n"
        "account 0000000000000000\n"
        "server 127.0.0.1 70000\n";
    static NetConfig config;
    NetConfigParse(TEXT, &config);
    const bool fields =
        config.file_found && config.has_session &&
        strcmp(config.session, "http://127.0.0.1:41234") == 0 && config.has_account &&
        config.account == 0xBB00000100000002ull && config.rewrite_count == 1u &&
        strcmp(config.rewrites[0].source, "game.edenprobe.invalid") == 0 &&
        strcmp(config.rewrites[0].destination, "localhost") == 0 &&
        memcmp(config.rewrites[0].address, (const uint8_t[]){ 127, 0, 0, 1 }, 4) == 0 &&
        config.blocked_count == 2u && config.blocked[0] == 0xBB00000100000009ull &&
        config.blocked[1] == 0xBB0000010000000Aull && config.profile_count == 2u &&
        config.profiles[0].account == 0xBB00000100000002ull &&
        strcmp(config.profiles[0].url, "http://127.0.0.1:41234/avatar/{size}.png") == 0 &&
        strcmp(config.profiles[0].name, "edenprobe  player") == 0 &&
        config.profiles[1].url[0] == '\0' && config.profiles[1].name[0] == '\0' &&
        config.has_server && config.server_port == 8443u &&
        memcmp(config.server_ip, (const uint8_t[]){ 127, 0, 0, 1 }, 4) == 0 &&
        strcmp(config.server_name, "redenex") == 0 &&
        strcmp(config.server_cert, "net-server.pem") == 0;
    // The three bad lines at the end are counted and named, and none of them moved a field that a
    // good line before them had set: a zero account is refused and so is a port past 65535.
    const bool problems = config.problem_count == 3u &&
                          strcmp(config.problems, "line 13, line 14, line 15") == 0;
    CHECK_TRUE(SUITE, "the net file is read a line at a time", CHECK_NONE, fields && problems,
               "fields %s, %u problems \"%s\"", fields ? "right" : "wrong",
               (unsigned)config.problem_count, config.problems);

    char said[512];
    NetConfigDescribe(said, sizeof(said), &config);
    static const char EXPECTED[] =
        "net: sdmc:/edenprobe/net; session http://127.0.0.1:41234; account named; 1 rewrite, "
        "2 blocked, 2 profiles; server 127.0.0.1:8443, name redenex, certificate net-server.pem; "
        "3 lines not understood: line 13, line 14, line 15";
    static NetConfig none;
    NetConfigParse(NULL, &none);
    char none_said[256];
    NetConfigDescribe(none_said, sizeof(none_said), &none);
    const bool none_right =
        !none.file_found && !NetConfigDeclaresOnline(&none) &&
        strcmp(none_said,
               "net: no sdmc:/edenprobe/net, so every case that needs a backend is skipped") == 0;
    static NetConfig server_only;
    NetConfigParse("server 10.0.0.2 443\n", &server_only);
    static NetConfig session_only;
    NetConfigParse("session https://example.test\n", &session_only);
    static NetConfig empty;
    NetConfigParse("", &empty);
    const bool online = NetConfigDeclaresOnline(&config) &&
                        NetConfigDeclaresOnline(&server_only) &&
                        NetConfigDeclaresOnline(&session_only) && empty.file_found &&
                        !NetConfigDeclaresOnline(&empty);
    CHECK_TRUE(SUITE, "the net file's report line, and whether it says the machine is online",
               CHECK_NONE, strcmp(said, EXPECTED) == 0 && none_right && online,
               "said \"%.200s\"; no file %s; online %s", said, none_right ? "right" : none_said,
               online ? "right" : "wrong");

    // The id nobody knows: the smallest above the account that no profile line names.
    static NetConfig crowded;
    NetConfigParse("account 0000000000000010\nprofile 0000000000000011 - a\n"
                   "profile 0000000000000012 - b\n",
                   &crowded);
    static NetConfig bare;
    NetConfigParse("", &bare);
    CHECK_TRUE(SUITE, "the account nobody knows is one no line names", CHECK_NONE,
               NetUnknownAccount(&crowded) == 0x13u && NetUnknownAccount(&bare) == 1u &&
                   NetUnknownAccount(&config) == 0xBB00000100000004ull,
               "got 0x%llx, 0x%llx, 0x%llx", (unsigned long long)NetUnknownAccount(&crowded),
               (unsigned long long)NetUnknownAccount(&bare),
               (unsigned long long)NetUnknownAccount(&config));
}

static void CheckWords(void) {
    uint64_t id = 0;
    const bool ids = NetParseAccountId("BB00000100000002", 16, &id) &&
                     id == 0xBB00000100000002ull &&
                     NetParseAccountId("bb0000010000000f", 16, &id) &&
                     id == 0xBB0000010000000Full &&
                     !NetParseAccountId("BB0000010000000", 15, &id) &&
                     !NetParseAccountId("BB000001000000020", 17, &id) &&
                     !NetParseAccountId("0000000000000000", 16, &id) &&
                     !NetParseAccountId("BB0000010000000G", 16, &id);
    uint8_t ip[4] = { 0 };
    const bool ips = NetParseIpv4("127.0.0.1", 9, ip) &&
                     memcmp(ip, (const uint8_t[]){ 127, 0, 0, 1 }, 4) == 0 &&
                     NetParseIpv4("255.254.0.10", 12, ip) &&
                     memcmp(ip, (const uint8_t[]){ 255, 254, 0, 10 }, 4) == 0 &&
                     !NetParseIpv4("256.0.0.1", 9, ip) && !NetParseIpv4("1.2.3", 5, ip) &&
                     !NetParseIpv4("1.2.3.4.5", 9, ip) && !NetParseIpv4("1..2.3", 6, ip) &&
                     !NetParseIpv4("1.2.3.1000", 10, ip) && !NetParseIpv4("-1.2.3.4", 8, ip);
    CHECK_TRUE(SUITE, "account ids and dotted addresses are read whole or refused", CHECK_NONE,
               ids && ips, "ids %s, addresses %s", ids ? "right" : "wrong",
               ips ? "right" : "wrong");

    // The placeholder is every percent sign, not the first.
    char out[64];
    const bool one = NetExpandPlaceholder(out, sizeof(out), "probe-%.edenprobe.invalid", "lp1") &&
                     strcmp(out, "probe-lp1.edenprobe.invalid") == 0;
    char two_out[64];
    const bool two = NetExpandPlaceholder(two_out, sizeof(two_out), "%.a.%", "dd1") &&
                     strcmp(two_out, "dd1.a.dd1") == 0;
    char none_out[64];
    const bool none = NetExpandPlaceholder(none_out, sizeof(none_out), "plain.test", "lp1") &&
                      strcmp(none_out, "plain.test") == 0;
    char small[8];
    const bool refused = !NetExpandPlaceholder(small, sizeof(small), "abc%def", "lp1") &&
                         !NetExpandPlaceholder(small, sizeof(small), "abc%de", "lp1") &&
                         NetExpandPlaceholder(small, sizeof(small), "ab%de", "lp1") &&
                         strcmp(small, "ablp1de") == 0;
    CHECK_TRUE(SUITE, "the placeholder is replaced everywhere, and a name that will not fit is "
                      "refused",
               CHECK_NONE, one && two && none && refused, "one \"%s\" two \"%s\" none \"%s\" %s",
               out, two_out, none_out, refused ? "fit right" : "fit wrong");

    // RFC 6750 section 2.1, and a token of the shape Python's token_urlsafe hands out.
    const char *const good[] = { "dGhpcyBpcyBub3QgYSB0b2tlbg", "a", "a.b-c_d~e+f/g==",
                                 "Zm9v=" };
    const char *const bad[] = { "", "==", "=abc", "a=b", "a b", "a\"b", "caf\xc3\xa9" };
    bool tokens = true;
    for (size_t i = 0; i < sizeof(good) / sizeof(good[0]); ++i) {
        tokens = tokens && NetIsB64Token((const uint8_t *)good[i], strlen(good[i]));
    }
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        tokens = tokens && !NetIsB64Token((const uint8_t *)bad[i], strlen(bad[i]));
    }
    const uint64_t a[] = { 1, 2, 2, 3 };
    const uint64_t b[] = { 2, 3, 1, 2 };
    const uint64_t c[] = { 1, 2, 3, 3 };
    const bool sets = NetSameIds(a, 4, b, 4) && !NetSameIds(a, 4, c, 4) &&
                      !NetSameIds(a, 3, b, 4) && NetSameIds(a, 0, b, 0);
    const bool lengths = NetAddressLengthUsable(16, 16) && NetAddressLengthUsable(16, 32) &&
                         !NetAddressLengthUsable(0, 16) && !NetAddressLengthUsable(8, 16) &&
                         !NetAddressLengthUsable(20, 16);
    CHECK_TRUE(SUITE, "an address length recvfrom left unset is not answered with", CHECK_NONE,
               lengths, "lengths %s", lengths ? "right" : "wrong");
    CHECK_TRUE(SUITE, "a bearer token's alphabet, and two id lists compared as sets", CHECK_NONE,
               tokens && sets, "tokens %s, sets %s", tokens ? "right" : "wrong",
               sets ? "right" : "wrong");

    char text[48];
    NetResultText(text, sizeof(text), 0xDE6Eu);
    char zero[48];
    NetResultText(zero, sizeof(zero), 0u);
    char block[48];
    NetResultText(block, sizeof(block), NET_SSL_WOULD_BLOCK);
    CHECK_TRUE(SUITE, "a result code reads as the platform prints it", CHECK_NONE,
               strcmp(text, "2110-0111 (0xde6e)") == 0 &&
                   strcmp(zero, "2000-0000 (0x0)") == 0 &&
                   strcmp(block, "2123-0204 (0x1987b)") == 0 &&
                   NetSslWouldBlock(0x1987Bu) && !NetSslWouldBlock(0x7CE7Bu) &&
                   !NetSslWouldBlock(0u),
               "got \"%s\", \"%s\" and \"%s\"", text, zero, block);
}

static void CheckResolverFormats(void) {
    // A hostent for localhost with one alias and two addresses, written out from the format:
    // 127.0.0.1 is the word 0x7F000001 stored little end first.
    static const uint8_t HOSTENT[] = {
        'l', 'o', 'c', 'a', 'l', 'h', 'o', 's', 't', 0, // h_name
        0, 0, 0, 1, 'l', 'o', 0,                        // one alias
        0, 2, 0, 4,                                     // AF_INET, four bytes
        0, 0, 0, 2,                                     // two addresses
        0x01, 0x00, 0x00, 0x7F, 0x04, 0x03, 0x02, 0x0A, // 127.0.0.1, 10.2.3.4
    };
    NetHostEnt host;
    const bool read = NetParseHostEnt(HOSTENT, sizeof(HOSTENT), &host) &&
                      strcmp(host.name, "localhost") == 0 && host.alias_count == 1u &&
                      host.address_type == 2u && host.address_count == 2u &&
                      memcmp(host.addresses[0], (const uint8_t[]){ 127, 0, 0, 1 }, 4) == 0 &&
                      memcmp(host.addresses[1], (const uint8_t[]){ 10, 2, 3, 4 }, 4) == 0;
    NetHostEnt cut;
    const bool refused = !NetParseHostEnt(HOSTENT, sizeof(HOSTENT) - 1u, &cut) &&
                         !NetParseHostEnt(HOSTENT, 5, &cut);
    CHECK_TRUE(SUITE, "a serialised hostent reads back as its name and addresses", CHECK_NONE,
               read && refused, "read %s (\"%s\", %u addresses), short input %s",
               read ? "right" : "wrong", host.name, (unsigned)host.address_count,
               refused ? "refused" : "accepted");

    static const uint8_t ADDRINFO[] = {
        0xBE, 0xEF, 0xCA, 0xFE, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 1, 0, 0, 0, 6, 0, 0, 0, 16,
        0, 2, 0x90, 0x1F, 0x01, 0x00, 0x00, 0x7F, 0, 0, 0, 0, 0, 0, 0, 0, // :8080, 127.0.0.1
        'l', 'o', 0,                                                       // canonical name
        0xBE, 0xEF, 0xCA, 0xFE, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 2, 0, 0, 0, 17, 0, 0, 0, 16,
        0, 2, 0x00, 0x00, 0x08, 0x08, 0x08, 0x08, 0, 0, 0, 0, 0, 0, 0, 0, // 8.8.8.8
        0,                                                                 // no canonical name
        0, 0, 0, 0,                                                        // the sentinel
    };
    NetAddrList list;
    const bool listed = NetParseAddrInfo(ADDRINFO, sizeof(ADDRINFO), &list) && list.count == 2u &&
                        list.family[0] == 2u && list.port[0] == 8080u &&
                        memcmp(list.addresses[0], (const uint8_t[]){ 127, 0, 0, 1 }, 4) == 0 &&
                        memcmp(list.addresses[1], (const uint8_t[]){ 8, 8, 8, 8 }, 4) == 0;
    uint8_t bad_magic[sizeof(ADDRINFO)];
    memcpy(bad_magic, ADDRINFO, sizeof(ADDRINFO));
    bad_magic[3] = 0xFF;
    NetAddrList ignored;
    const bool guarded = !NetParseAddrInfo(ADDRINFO, sizeof(ADDRINFO) - 4u, &ignored) &&
                         !NetParseAddrInfo(bad_magic, sizeof(bad_magic), &ignored) &&
                         NetParseAddrInfo((const uint8_t[]){ 0, 0, 0, 0 }, 4, &ignored) &&
                         ignored.count == 0u;
    CHECK_TRUE(SUITE, "a serialised addrinfo list reads back entry by entry to its sentinel",
               CHECK_NONE, listed && guarded, "list %s (%u entries, port %u), guards %s",
               listed ? "right" : "wrong", (unsigned)list.count, (unsigned)list.port[0],
               guarded ? "right" : "wrong");
}

static void CheckCertificates(void) {
    static uint8_t root[1024];
    static uint8_t leaf[1024];
    const size_t root_length = NetPemToDer(ROOT_PEM, root, sizeof(root));
    const size_t leaf_length = NetPemToDer(LEAF_PEM, leaf, sizeof(leaf));
    static const uint8_t ROOT_HEAD[8] = { 0x30, 0x82, 0x01, 0x92, 0x30, 0x82, 0x01, 0x39 };
    static const uint8_t ROOT_TAIL[4] = { 0x40, 0xfd, 0x06, 0x75 };
    static const uint8_t LEAF_HEAD[8] = { 0x30, 0x82, 0x01, 0x64, 0x30, 0x82, 0x01, 0x09 };
    static const uint8_t LEAF_TAIL[4] = { 0xe1, 0x44, 0x75, 0x93 };
    const bool decoded = root_length == 406u && leaf_length == 360u &&
                         memcmp(root, ROOT_HEAD, 8) == 0 &&
                         memcmp(root + 402, ROOT_TAIL, 4) == 0 &&
                         ByteSum(root, root_length) == 34878u && memcmp(leaf, LEAF_HEAD, 8) == 0 &&
                         memcmp(leaf + 356, LEAF_TAIL, 4) == 0 &&
                         ByteSum(leaf, leaf_length) == 32595u;
    uint8_t tiny[16];
    const bool bounded = NetPemToDer(ROOT_PEM, tiny, sizeof(tiny)) == 0u &&
                         NetPemToDer("no certificate here", root + 512, 64) == 0u;
    CHECK_TRUE(SUITE, "a PEM certificate decodes to the DER openssl writes for it", CHECK_NONE,
               decoded && bounded, "lengths %zu and %zu, bytes %s, bounds %s", root_length,
               leaf_length, decoded ? "right" : "wrong", bounded ? "right" : "wrong");

    bool root_parsed = false;
    bool leaf_parsed = false;
    bool junk_parsed = true;
    const bool root_self = NetCertSelfIssued(root, root_length, &root_parsed);
    const bool leaf_self = NetCertSelfIssued(leaf, leaf_length, &leaf_parsed);
    const bool junk_self = NetCertSelfIssued(ROOT_HEAD, sizeof(ROOT_HEAD), &junk_parsed);
    CHECK_TRUE(SUITE, "a self-signed root names itself as issuer and the certificate it signed "
                      "does not",
               CHECK_NONE,
               root_parsed && root_self && leaf_parsed && !leaf_self && !junk_parsed &&
                   !junk_self,
               "root %d/%d, leaf %d/%d, junk %d/%d", root_parsed, root_self, leaf_parsed,
               leaf_self, junk_parsed, junk_self);

    // The chain form a handshake writes when asked for it: the magic, a count, the padding word,
    // then a size and an offset per certificate, then the certificates.
    static uint8_t chain[24 + 360];
    memcpy(chain, "CertChMN", 8);
    chain[8] = 1;
    chain[16] = (uint8_t)(leaf_length & 0xFFu);
    chain[17] = (uint8_t)(leaf_length >> 8);
    chain[20] = 24;
    memcpy(chain + 24, leaf, leaf_length);
    const uint8_t *first = NULL;
    size_t first_length = 0;
    const bool from_chain = NetFirstServerCert(chain, sizeof(chain), &first, &first_length) &&
                            first == chain + 24 && first_length == leaf_length;
    const uint8_t *bare = NULL;
    size_t bare_length = 0;
    const bool from_bare = NetFirstServerCert(root, root_length + 7u, &bare, &bare_length) &&
                           bare == root && bare_length == root_length;
    chain[20] = 0xF0;
    const bool refused = !NetFirstServerCert(chain, sizeof(chain), &first, &first_length) &&
                         !NetFirstServerCert((const uint8_t *)"\x04\x02zz", 4, &bare,
                                             &bare_length);
    CHECK_TRUE(SUITE, "the certificate a handshake hands back is found in either form",
               CHECK_NONE, from_chain && from_bare && refused, "chain %s, bare %s, junk %s",
               from_chain ? "right" : "wrong", from_bare ? "right" : "wrong",
               refused ? "refused" : "accepted");
}

static void CheckHttp(void) {
    static const char RESPONSE[] = "HTTP/1.1 200 OK\r\n"
                                   "Content-Type: application/json\r\n"
                                   "content-length:  34\r\n"
                                   "\r\n"
                                   "{\"maintenances\":[],\"incidents\":[]}";
    NetHttpResponse whole;
    const bool complete = NetHttpParse(RESPONSE, sizeof(RESPONSE) - 1u, &whole) &&
                          whole.complete && whole.status == 200 && whole.content_length == 34 &&
                          whole.body_length == 34u &&
                          memcmp(RESPONSE + whole.body_offset, "{\"maintenances\"", 15) == 0;
    NetHttpResponse part;
    const bool partial = NetHttpParse(RESPONSE, sizeof(RESPONSE) - 5u, &part) && !part.complete &&
                         part.status == 200;
    NetHttpResponse head_only;
    const bool head = NetHttpParse(RESPONSE, 30, &head_only) && !head_only.complete;
    static const char OPEN_ENDED[] = "HTTP/1.0 404 Not Found\r\n\r\nnothing";
    NetHttpResponse open_ended;
    const bool no_length = NetHttpParse(OPEN_ENDED, sizeof(OPEN_ENDED) - 1u, &open_ended) &&
                           open_ended.status == 404 && open_ended.content_length == -1 &&
                           !open_ended.complete && open_ended.body_length == 7u;
    static const char EMPTY_404[] = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
    NetHttpResponse empty;
    const bool empty_whole = NetHttpParse(EMPTY_404, sizeof(EMPTY_404) - 1u, &empty) &&
                             empty.complete && empty.status == 404 && empty.body_length == 0u;
    NetHttpResponse junk;
    const bool refused = !NetHttpParse("HTTP/2 200\r\n\r\n", 14, &junk) &&
                         !NetHttpParse("HTTP/1.1 2x0 OK\r\n\r\n", 19, &junk);
    CHECK_TRUE(SUITE, "an HTTP response head says where its body is and when it is whole",
               CHECK_NONE, complete && partial && head && no_length && empty_whole && refused,
               "complete %d partial %d head %d no length %d empty %d junk %d", complete, partial,
               head, no_length, empty_whole, refused);

    char request[256];
    NetStatusRequest(request, sizeof(request), "redenex");
    CHECK_TRUE(SUITE, "the status request is one GET that asks the server to close", CHECK_NONE,
               strcmp(request, "GET /service_status_hac.json HTTP/1.1\r\nHost: redenex\r\n"
                               "Accept: application/json\r\nConnection: close\r\n\r\n") == 0,
               "wrote %.200s", request);

    const char *const clear[] = {
        "{\"maintenances\":[],\"incidents\":[]}",
        " {\n \"incidents\" : [ ] , \"maintenances\":[]\t}\n",
        "{\"maintenances\":[],\"incidents\":[],\"note\":{\"a\":[1,-2.5e3,true,null,\"x\\\"y\"]}}",
    };
    const char *const not_clear[] = {
        "{\"maintenances\":[{}],\"incidents\":[]}",
        "{\"maintenances\":[]}",
        "{\"maintenances\":[],\"incidents\":null}",
        "{\"maintenances\":[],\"incidents\":[]} trailing",
        "{\"maintenances\":[],\"incidents\":[],}",
        "[\"maintenances\",\"incidents\"]",
        "",
        "{\"maintenances\":[],\"incidents\":[]",
    };
    unsigned wrong = 0;
    for (size_t i = 0; i < sizeof(clear) / sizeof(clear[0]); ++i) {
        wrong += NetStatusAllClear(clear[i], strlen(clear[i])) ? 0u : 1u;
    }
    for (size_t i = 0; i < sizeof(not_clear) / sizeof(not_clear[0]); ++i) {
        wrong += NetStatusAllClear(not_clear[i], strlen(not_clear[i])) ? 1u : 0u;
    }
    CHECK_TRUE(SUITE, "a status document says nothing is wrong only with both lists empty",
               CHECK_NONE, wrong == 0u, "%u of %u documents read wrong", wrong,
               (unsigned)(sizeof(clear) / sizeof(clear[0]) +
                          sizeof(not_clear) / sizeof(not_clear[0])));
}

static void CheckFriendProfile(void) {
    uint8_t raw[NET_FRIEND_PROFILE_SIZE];
    memset(raw, 0xAA, sizeof(raw));
    const uint8_t id[8] = { 0x02, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0xBB };
    memcpy(raw, id, 8);
    memcpy(raw + 8, "edenprobe", 10);
    memcpy(raw + 48, "http://127.0.0.1/avatar/{size}.png", 35);
    raw[208] = 1;
    NetFriendProfile profile;
    NetFriendProfileDecode(raw, &profile);
    const bool fields = profile.account == 0xBB00000100000002ull &&
                        strcmp(profile.name, "edenprobe") == 0 &&
                        strcmp(profile.url, "http://127.0.0.1/avatar/{size}.png") == 0 &&
                        profile.valid == 1u;
    // A name with no terminator in its field stops at the field's end, not in the url after it.
    memset(raw + 8, 'n', NET_PROFILE_NAME_MAX);
    NetFriendProfile full;
    NetFriendProfileDecode(raw, &full);
    CHECK_TRUE(SUITE, "a friend profile is read at the offsets the guest reads it at",
               CHECK_NONE, fields && strlen(full.name) == NET_PROFILE_NAME_MAX - 1u,
               "account 0x%llx name \"%s\" url \"%s\" valid %u; full name %zu",
               (unsigned long long)profile.account, profile.name, profile.url,
               (unsigned)profile.valid, strlen(full.name));
}

static void CheckExpectations(void) {
    static NetConfig signed_in;
    NetConfigParse("session http://127.0.0.1:1\n"
                   "rewrite game.edenprobe.invalid localhost 127.0.0.1\n"
                   "rewrite cdn.edenprobe.invalid cdn.example.test 10.0.0.9\n",
                   &signed_in);
    static NetConfig offline;
    NetConfigParse("server 10.0.0.2 443\n", &offline);

    // nsd: a published rewrite wins, then the placeholder, and a plain name comes back as it is.
    char a[64];
    char b[64];
    char c[64];
    char d[64];
    const bool nsd =
        NetExpectNsd(&signed_in, "cdn.edenprobe.invalid", "lp1", a, sizeof(a)) &&
        strcmp(a, "cdn.example.test") == 0 &&
        NetExpectNsd(&signed_in, NET_PLACEHOLDER_NAME, "lp1", b, sizeof(b)) &&
        strcmp(b, "probe-lp1.edenprobe.invalid") == 0 &&
        NetExpectNsd(&offline, "plain.edenprobe.invalid", "lp1", c, sizeof(c)) &&
        strcmp(c, "plain.edenprobe.invalid") == 0 &&
        NetExpectNsd(&offline, "game.edenprobe.invalid", "lp1", d, sizeof(d)) &&
        strcmp(d, "game.edenprobe.invalid") == 0 &&
        !NetExpectNsd(&signed_in, "cdn.edenprobe.invalid", "lp1", a, 8);
    CHECK_TRUE(SUITE, "nsd is expected to answer a rewrite, then the placeholder, else the name",
               CHECK_NONE, nsd, "got \"%s\", \"%s\", \"%s\", \"%s\"", a, b, c, d);

    // sfdnsres: what an address is expected to be, and when nothing says.
    uint8_t ip[4] = { 0 };
    const uint8_t LOOP[4] = { 127, 0, 0, 1 };
    bool addresses = NetExpectAddress(&offline, "192.168.4.5", NULL, ip) &&
                     memcmp(ip, (const uint8_t[]){ 192, 168, 4, 5 }, 4) == 0;
    addresses = addresses && NetExpectAddress(&signed_in, "cdn.edenprobe.invalid", NULL, ip) &&
                memcmp(ip, (const uint8_t[]){ 10, 0, 0, 9 }, 4) == 0;
    addresses = addresses && NetExpectAddress(&signed_in, NET_PLACEHOLDER_NAME, NULL, ip) &&
                memcmp(ip, LOOP, 4) == 0;
    addresses = addresses &&
                NetExpectAddress(&signed_in, "probe-lp1.edenprobe.invalid",
                                 "probe-lp1.edenprobe.invalid", ip) &&
                memcmp(ip, LOOP, 4) == 0;
    addresses = addresses && !NetExpectAddress(&signed_in, "probe-lp1.edenprobe.invalid", NULL,
                                               ip) &&
                !NetExpectAddress(&offline, NET_PLACEHOLDER_NAME, NULL, ip) &&
                !NetExpectAddress(&signed_in, "plain.edenprobe.invalid", NULL, ip);
    CHECK_TRUE(SUITE, "sfdnsres is expected to answer loopback only for a session's own names",
               CHECK_NONE, addresses, "last answer %u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);

    // The sign-in wait: it asks about the first rewrite, and is done only when nsd answers that
    // rewrite; with no rewrite it asks about the placeholder.
    static NetConfig bare;
    NetConfigParse("session http://127.0.0.1:1\n", &bare);
    const bool session =
        strcmp(NetSessionProbeName(&signed_in), "game.edenprobe.invalid") == 0 &&
        strcmp(NetSessionProbeName(&bare), NET_PLACEHOLDER_NAME) == 0 &&
        NetSessionAnswered(&signed_in, "game.edenprobe.invalid", "lp1", "localhost") &&
        !NetSessionAnswered(&signed_in, "game.edenprobe.invalid", "lp1",
                            "game.edenprobe.invalid") &&
        NetSessionAnswered(&bare, NET_PLACEHOLDER_NAME, "lp1", "probe-lp1.edenprobe.invalid") &&
        !NetSessionAnswered(&bare, NET_PLACEHOLDER_NAME, "lp1", NET_PLACEHOLDER_NAME) &&
        !NetSessionAnswered(&bare, "plain.edenprobe.invalid", "lp1", "plain.edenprobe.invalid");
    CHECK_TRUE(SUITE, "the sign-in wait ends only on the answer a signed in session gives",
               CHECK_NONE, session, "%s", "one of the seven readings was wrong");

    // {size}: every one, and the digits put in are never read as another placeholder.
    char url[64];
    char two[64];
    char none[64];
    char tight[16];
    const bool sizes =
        NetExpandSize(url, sizeof(url), "http://h/{size}.png", 256) &&
        strcmp(url, "http://h/256.png") == 0 &&
        NetExpandSize(two, sizeof(two), "{size}/{size}{size}", 64) &&
        strcmp(two, "64/6464") == 0 && NetExpandSize(none, sizeof(none), "http://h/a.png", 9) &&
        strcmp(none, "http://h/a.png") == 0 && NetExpandSize(none, sizeof(none), "{siz}", 9) &&
        strcmp(none, "{siz}") == 0 &&
        !NetExpandSize(tight, sizeof(tight), "0123456789a{size}", 12345) &&
        NetExpandSize(tight, sizeof(tight), "0123456789{size}", 12345) &&
        NetExpandSize(tight, sizeof(tight), "012345678{size}", 12345) &&
        strcmp(tight, "01234567812345") == 0;
    CHECK_TRUE(SUITE, "every {size} in a picture url becomes the size asked for", CHECK_NONE,
               sizes, "got \"%s\", \"%s\", \"%s\"", url, two, none);

    // A known slot matches every field; an unknown one is the id asked and not valid.
    NetProfile expected;
    memset(&expected, 0, sizeof(expected));
    expected.account = 0xBB00000100000002ull;
    snprintf(expected.name, sizeof(expected.name), "edenprobe");
    snprintf(expected.url, sizeof(expected.url), "http://h/{size}.png");
    NetFriendProfile got;
    memset(&got, 0, sizeof(got));
    got.account = expected.account;
    snprintf(got.name, sizeof(got.name), "edenprobe");
    snprintf(got.url, sizeof(got.url), "http://h/{size}.png");
    got.valid = 1;
    bool slots = NetProfileMatches(&expected, &got);
    got.account = expected.account + 1u;
    slots = slots && !NetProfileMatches(&expected, &got);
    got.account = expected.account;
    got.valid = 0;
    slots = slots && !NetProfileMatches(&expected, &got) &&
            NetProfileIsUnknown(expected.account, &got) &&
            !NetProfileIsUnknown(expected.account + 1u, &got);
    got.valid = 1;
    snprintf(got.url, sizeof(got.url), "http://h/64.png");
    slots = slots && !NetProfileMatches(&expected, &got) &&
            !NetProfileIsUnknown(expected.account, &got);
    CHECK_TRUE(SUITE, "a profile slot is judged by its id as well as its fields", CHECK_NONE,
               slots, "%s", "one of the seven readings was wrong");
}

void RunReferenceNet(void) {
    CheckNetFile();
    CheckWords();
    CheckResolverFormats();
    CheckCertificates();
    CheckHttp();
    CheckFriendProfile();
    CheckExpectations();
}
