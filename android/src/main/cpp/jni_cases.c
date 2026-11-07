// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
/* JNI as ART gives it to native code: the version, FindClass's class loaders, strings, exceptions, arrays, calls. */
#include "probe.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

JNIEXPORT jint JNICALL PROBE_FN(version)(JNIEnv* env, jclass cls) {
    (void)cls;
    return (*env)->GetVersion(env);
}

/* a thread of the native code's own, attached: what FindClass of an app class gives there */
static void* find_from_attached(void* arg) {
    char* out = arg;
    JavaVM* vm = probe_vm();
    JNIEnv* env = NULL;
    if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK) {
        snprintf(out, 256, "attach failed");
        return NULL;
    }
    jclass c = (*env)->FindClass(env, "org/scarletnx/edenprobe/NativeLib");
    if (c) {
        snprintf(out, 256, "found");
        (*env)->DeleteLocalRef(env, c);
    } else {
        jthrowable t = (*env)->ExceptionOccurred(env);
        (*env)->ExceptionClear(env);
        if (t) probe_class_name(env, t, out, 256);
        else snprintf(out, 256, "NULL and no exception");
    }
    (*vm)->DetachCurrentThread(vm);
    return NULL;
}

JNIEXPORT jstring JNICALL PROBE_FN(findClass)(JNIEnv* env, jclass cls) {
    (void)cls;
    jclass c = (*env)->FindClass(env, "org/scarletnx/edenprobe/NativeLib");
    const char* here = c ? "found" : "not found";
    if (!c) (*env)->ExceptionClear(env);
    char there[256] = "";
    pthread_t t;
    if (pthread_create(&t, NULL, find_from_attached, there)) snprintf(there, sizeof there, "pthread_create failed");
    else pthread_join(t, NULL);
    return probe_string(env, "java thread %s|attached thread %s", here, there);
}

JNIEXPORT jstring JNICALL PROBE_FN(utf)(JNIEnv* env, jclass cls, jstring s) {
    (void)cls;
    jsize utf_length = (*env)->GetStringUTFLength(env, s), length = (*env)->GetStringLength(env, s);
    const char* u = (*env)->GetStringUTFChars(env, s, NULL);
    char hex[256] = "";
    for (jsize i = 0; u && i < utf_length && i < 100; ++i) snprintf(hex + 2 * i, 3, "%02x", (unsigned char)u[i]);
    if (u) (*env)->ReleaseStringUTFChars(env, s, u);
    return probe_string(env, "%s utf-length %d length %d", hex, (int)utf_length, (int)length);
}

JNIEXPORT jstring JNICALL PROBE_FN(catchJava)(JNIEnv* env, jclass cls) {
    jmethodID thrower = (*env)->GetStaticMethodID(env, cls, "thrower", "()V");
    (*env)->CallStaticVoidMethod(env, cls, thrower);
    if (!(*env)->ExceptionCheck(env)) return (*env)->NewStringUTF(env, "no exception");
    jthrowable t = (*env)->ExceptionOccurred(env);
    (*env)->ExceptionClear(env);
    char name[256], message[256];
    probe_class_name(env, t, name, sizeof name);
    jclass tc = (*env)->FindClass(env, "java/lang/Throwable");
    jmethodID get_message = (*env)->GetMethodID(env, tc, "getMessage", "()Ljava/lang/String;");
    probe_utf(env, (jstring)(*env)->CallObjectMethod(env, t, get_message), message, sizeof message);
    return probe_string(env, "%s: %s", name, message);
}

JNIEXPORT void JNICALL PROBE_FN(throwNative)(JNIEnv* env, jclass cls) {
    (void)cls;
    (*env)->ThrowNew(env, (*env)->FindClass(env, "java/lang/IllegalStateException"), "from native");
}

JNIEXPORT jstring JNICALL PROBE_FN(arrays)(JNIEnv* env, jclass cls, jintArray ints, jbyteArray bytes, jobject direct, jobject heap) {
    (void)cls;
    jint* e = (*env)->GetIntArrayElements(env, ints, NULL);
    jint sum = 0;
    for (jsize i = 0; i < (*env)->GetArrayLength(env, ints); ++i) sum += e[i];
    e[0] = 100;
    (*env)->ReleaseIntArrayElements(env, ints, e, 0);
    jbyte* b = (*env)->GetPrimitiveArrayCritical(env, bytes, NULL);
    for (int i = 0; i < 4; ++i) b[i] = (jbyte)i;
    (*env)->ReleasePrimitiveArrayCritical(env, bytes, b, 0);
    unsigned char* d = (*env)->GetDirectBufferAddress(env, direct);
    if (d) d[5] = 5;
    return probe_string(env, "sum %d, direct capacity %lld, heap capacity %lld", (int)sum,
                        (long long)(*env)->GetDirectBufferCapacity(env, direct), (long long)(*env)->GetDirectBufferCapacity(env, heap));
}

static unsigned char g_direct[32];

JNIEXPORT jobject JNICALL PROBE_FN(newDirect)(JNIEnv* env, jclass cls) {
    (void)cls;
    for (int i = 0; i < 32; ++i) g_direct[i] = (unsigned char)i;
    return (*env)->NewDirectByteBuffer(env, g_direct, sizeof g_direct);
}

JNIEXPORT jstring JNICALL PROBE_FN(calls)(JNIEnv* env, jclass cls) {
    jmethodID mix = (*env)->GetStaticMethodID(env, cls, "mix", "(IJFDLjava/lang/String;ZCSBDF)Ljava/lang/String;");
    jmethodID half = (*env)->GetStaticMethodID(env, cls, "half", "(F)F");
    jstring five = (*env)->NewStringUTF(env, "five");
    /* variadic: a float goes as a double, boolean, char, short and byte as ints */
    jstring a = (jstring)(*env)->CallStaticObjectMethod(env, cls, mix, (jint)1, (jlong)-2, (jdouble)3.5f, (jdouble)4.25, five,
                                                         (jint)JNI_TRUE, (jint)'x', (jint)-6, (jint)7, (jdouble)8.125, (jdouble)9.5f);
    jvalue v[11];
    v[0].i = 1;
    v[1].j = -2;
    v[2].f = 3.5f;
    v[3].d = 4.25;
    v[4].l = five;
    v[5].z = JNI_TRUE;
    v[6].c = 'x';
    v[7].s = -6;
    v[8].b = 7;
    v[9].d = 8.125;
    v[10].f = 9.5f;
    jstring b = (jstring)(*env)->CallStaticObjectMethodA(env, cls, mix, v);
    jfloat h = (*env)->CallStaticFloatMethod(env, cls, half, (jdouble)2.5f);
    char sa[256], sb[256];
    probe_utf(env, a, sa, sizeof sa);
    probe_utf(env, b, sb, sizeof sb);
    return probe_string(env, "%s|%s|%d.%02d", sa, sb, (int)h, (int)(h * 100) % 100);
}
