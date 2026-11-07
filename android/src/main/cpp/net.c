/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Sockets and name lookups from native code, on loopback and for names that never resolve. */
#include "probe.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

JNIEXPORT jstring JNICALL PROBE_FN(tcpLoopback)(JNIEnv* env, jclass cls) {
    (void)cls;
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t len = sizeof a;
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) return probe_string(env, "socket errno %d", errno);
    if (bind(srv, (struct sockaddr*)&a, sizeof a) || listen(srv, 1) || getsockname(srv, (struct sockaddr*)&a, &len)) {
        int e = errno;
        close(srv);
        return probe_string(env, "bind, listen or getsockname errno %d", e);
    }
    int port = ntohs(a.sin_port);
    int cl = socket(AF_INET, SOCK_STREAM, 0);
    if (cl < 0 || connect(cl, (struct sockaddr*)&a, sizeof a)) {
        int e = errno;
        if (cl >= 0) close(cl);
        close(srv);
        return probe_string(env, "connect to port %d errno %d", port, e);
    }
    int acc = accept(srv, NULL, NULL);
    if (acc < 0) {
        int e = errno;
        close(cl);
        close(srv);
        return probe_string(env, "accept errno %d", e);
    }
    struct timeval tv = {5, 0};
    setsockopt(acc, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    char buf[4] = "";
    errno = 0;
    ssize_t sent = send(cl, "abc", 3, 0), got = -1;
    int e = errno;
    if (sent == 3) {
        got = recv(acc, buf, 3, MSG_WAITALL);
        e = errno;
    }
    close(acc);
    close(cl);
    close(srv);
    if (sent != 3) return probe_string(env, "send %zd errno %d", sent, e);
    if (got != 3) return probe_string(env, "recv %zd errno %d", got, e);
    return probe_string(env, "listening on a %s, accepted, %s", port > 0 ? "port" : "port of 0", buf);
}

JNIEXPORT jstring JNICALL PROBE_FN(connectRefused)(JNIEnv* env, jclass cls, jint port) {
    (void)cls;
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons((unsigned short)port);
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return probe_string(env, "socket errno %d", errno);
    errno = 0;
    int r = connect(s, (struct sockaddr*)&a, sizeof a);
    int e = errno;
    close(s);
    return probe_string(env, "%ld errno %d", (long)r, e);
}

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
