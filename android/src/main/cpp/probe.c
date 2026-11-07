// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
/* The library's own: JNI_OnLoad, which records what it was given and registers one native, and the helpers. */
#include "probe.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static struct {
    JavaVM* vm;
    int onload_count;
    int onload_getenv;
} g_probe = {.onload_getenv = -1};

JavaVM* probe_vm(void) { return g_probe.vm; }

jstring probe_string(JNIEnv* env, const char* fmt, ...) {
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    return (*env)->NewStringUTF(env, buf);
}

void probe_utf(JNIEnv* env, jstring s, char* out, size_t size) {
    out[0] = 0;
    if (!s) {
        snprintf(out, size, "(null)");
        return;
    }
    const char* u = (*env)->GetStringUTFChars(env, s, NULL);
    if (u) {
        snprintf(out, size, "%s", u);
        (*env)->ReleaseStringUTFChars(env, s, u);
    }
}

void probe_class_name(JNIEnv* env, jobject obj, char* out, size_t size) {
    jclass of = (*env)->GetObjectClass(env, obj);
    jclass class_class = (*env)->FindClass(env, "java/lang/Class");
    jmethodID get_name = (*env)->GetMethodID(env, class_class, "getName", "()Ljava/lang/String;");
    jstring name = (jstring)(*env)->CallObjectMethod(env, of, get_name);
    probe_utf(env, name, out, size);
    (*env)->DeleteLocalRef(env, name);
    (*env)->DeleteLocalRef(env, class_class);
    (*env)->DeleteLocalRef(env, of);
}

static jstring registered(JNIEnv* env, jclass cls) {
    (void)cls;
    return (*env)->NewStringUTF(env, "registered");
}

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* reserved) {
    (void)reserved;
    g_probe.vm = vm;
    ++g_probe.onload_count;
    JNIEnv* env = NULL;
    g_probe.onload_getenv = (*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6);
    if (g_probe.onload_getenv != JNI_OK) return JNI_ERR;
    jclass cls = (*env)->FindClass(env, "org/scarletnx/edenprobe/NativeLib");
    if (!cls) return JNI_ERR;
    JNINativeMethod methods[] = {{"registered", "()Ljava/lang/String;", (void*)registered}};
    if ((*env)->RegisterNatives(env, cls, methods, 1) != JNI_OK) return JNI_ERR;
    return JNI_VERSION_1_6;
}

JNIEXPORT jstring JNICALL PROBE_FN(onLoad)(JNIEnv* env, jclass cls) {
    (void)cls;
    return probe_string(env, "onload %d, GetEnv %d", g_probe.onload_count, g_probe.onload_getenv);
}

JNIEXPORT jstring JNICALL PROBE_FN(byName)(JNIEnv* env, jclass cls) {
    (void)cls;
    return (*env)->NewStringUTF(env, "by name");
}
