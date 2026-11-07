// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef ANDROIDPROBE_PROBE_H
#define ANDROIDPROBE_PROBE_H
#include <jni.h>
#include <stddef.h>

#define PROBE_FN(name) Java_org_scarletnx_edenprobe_NativeLib_##name

/* the JavaVM that JNI_OnLoad got */
JavaVM* probe_vm(void);
/* a Java string of the printf-style text (4096 bytes at most). Outside bionic.printf a number that can be negative is
   passed as a long (%ld) and a float as digits, so a formatter's own fault shows in that case alone. */
jstring probe_string(JNIEnv* env, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
/* the class name of obj (java.lang.Class.getName) into out; no exception may be pending */
void probe_class_name(JNIEnv* env, jobject obj, char* out, size_t size);
/* a Java string's modified UTF-8 into out */
void probe_utf(JNIEnv* env, jstring s, char* out, size_t size);
#endif
