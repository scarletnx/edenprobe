// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
/* bionic's resolver from native code, for names that never resolve and for localhost; the sockets themselves are
   the libc family's socket suite (source/tests/t_libc.c). */
#include "probe.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

JNIEXPORT jstring JNICALL PROBE_FN(resolve)(JNIEnv* env, jclass cls, jstring host) {
    (void)cls;
    char name[256];
    probe_utf(env, host, name, sizeof name);
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    int r = getaddrinfo(name, NULL, &hints, &res);
    if (r) return probe_string(env, "getaddrinfo %d", r);
    char out[512] = "";
    size_t n = 0;
    for (struct addrinfo* i = res; i; i = i->ai_next) {
        char text[INET6_ADDRSTRLEN] = "?";
        const void* where = i->ai_family == AF_INET ? (const void*)&((struct sockaddr_in*)i->ai_addr)->sin_addr
                                                    : (const void*)&((struct sockaddr_in6*)i->ai_addr)->sin6_addr;
        inet_ntop(i->ai_family, where, text, sizeof text);
        if (!strstr(out, text)) n += (size_t)snprintf(out + n, sizeof out - n, "%s%s", n ? " " : "", text);
    }
    freeaddrinfo(res);
    return (*env)->NewStringUTF(env, out);
}
