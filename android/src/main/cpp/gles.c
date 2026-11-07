// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
/* EGL and GLES on a 16 by 16 pbuffer of the case's own, read back with glReadPixels. A pixel is RGBA as 8 hex digits;
   (0,0) is the bottom left. The display is never terminated: the app's own UI may share it. */
#include "probe.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    EGLDisplay display;
    EGLSurface surface;
    EGLContext context;
    char error[160];
} Gl;

/* a context of the GLES version (1, 2 or 3) current on a 16 by 16 RGBA8 pbuffer */
static bool gl_begin(Gl* g, int version) {
    memset(g, 0, sizeof *g);
    g->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major = 0, minor = 0;
    if (!eglInitialize(g->display, &major, &minor)) {
        snprintf(g->error, sizeof g->error, "eglInitialize error 0x%x", eglGetError());
        return false;
    }
    EGLint bit = version == 3 ? EGL_OPENGL_ES3_BIT_KHR : version == 2 ? EGL_OPENGL_ES2_BIT : EGL_OPENGL_ES_BIT;
    const EGLint attrs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, bit, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
                            EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLConfig config;
    EGLint n = 0;
    if (!eglChooseConfig(g->display, attrs, &config, 1, &n) || n < 1) {
        snprintf(g->error, sizeof g->error, "no RGBA8 pbuffer config for GLES %d (error 0x%x)", version, eglGetError());
        return false;
    }
    const EGLint size[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
    g->surface = eglCreatePbufferSurface(g->display, config, size);
    const EGLint context_attrs[] = {EGL_CONTEXT_CLIENT_VERSION, version, EGL_NONE};
    g->context = eglCreateContext(g->display, config, EGL_NO_CONTEXT, context_attrs);
    if (g->surface == EGL_NO_SURFACE || g->context == EGL_NO_CONTEXT ||
        !eglMakeCurrent(g->display, g->surface, g->surface, g->context)) {
        snprintf(g->error, sizeof g->error, "pbuffer, context or make current for GLES %d: error 0x%x", version, eglGetError());
        return false;
    }
    glViewport(0, 0, 16, 16);
    return true;
}

static void gl_end(Gl* g) {
    eglMakeCurrent(g->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (g->context != EGL_NO_CONTEXT) eglDestroyContext(g->display, g->context);
    if (g->surface != EGL_NO_SURFACE) eglDestroySurface(g->display, g->surface);
}

static unsigned pixel(int x, int y) {
    unsigned char p[4] = {0};
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
    return (unsigned)p[0] << 24 | (unsigned)p[1] << 16 | (unsigned)p[2] << 8 | p[3];
}

static GLuint shader(GLenum type, const char* source, char* error, size_t size) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &source, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[128] = "";
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        snprintf(error, size, "a %s shader did not compile: %s", type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

/* a program with the attributes p (location 0) and t (1), or 0 with the reason in error */
static GLuint program(const char* vs, const char* fs, char* error, size_t size) {
    GLuint v = shader(GL_VERTEX_SHADER, vs, error, size), f = v ? shader(GL_FRAGMENT_SHADER, fs, error, size) : 0;
    if (!f) return 0;
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glBindAttribLocation(p, 0, "p");
    glBindAttribLocation(p, 1, "t");
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    glDeleteShader(v);
    glDeleteShader(f);
    if (!ok) {
        char log[128] = "";
        glGetProgramInfoLog(p, sizeof log, NULL, log);
        snprintf(error, size, "the program did not link: %s", log);
        glDeleteProgram(p);
        return 0;
    }
    glUseProgram(p);
    return p;
}

static const char k_vs[] = "attribute vec2 p; attribute vec2 t; varying vec2 v; void main() { v = t; gl_Position = vec4(p, 0.0, 1.0); }";
static const char k_color_fs[] = "precision mediump float; uniform vec4 c; void main() { gl_FragColor = c; }";
static const float k_quad[] = {-1, -1, 1, -1, -1, 1, 1, 1};
static const float k_quad_uv[] = {0, 0, 1, 0, 0, 1, 1, 1};

static void draw_quad(void) {
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, k_quad);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, k_quad_uv);
    glEnableVertexAttribArray(1);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

JNIEXPORT jstring JNICALL PROBE_FN(eglInit)(JNIEnv* env, jclass cls) {
    (void)cls;
    EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major = 0, minor = 0;
    if (!eglInitialize(d, &major, &minor)) return probe_string(env, "eglInitialize error 0x%x", eglGetError());
    EGLint n2 = 0, n3 = 0;
    const EGLint a2[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE};
    const EGLint a3[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR, EGL_NONE};
    eglChooseConfig(d, a2, NULL, 0, &n2);
    eglChooseConfig(d, a3, NULL, 0, &n3);
    char version[32];
    if (major > 1 || (major == 1 && minor >= 4)) snprintf(version, sizeof version, "1.4 or newer");
    else snprintf(version, sizeof version, "%d.%d", major, minor);
    return probe_string(env, "EGL %s|%s|%s", version, n2 > 0 ? "an ES2 pbuffer config" : "no ES2 pbuffer config",
                        n3 > 0 ? "an ES3 pbuffer config" : "no ES3 pbuffer config");
}

JNIEXPORT jstring JNICALL PROBE_FN(glClearRead)(JNIEnv* env, jclass cls) {
    (void)cls;
    Gl g;
    if (!gl_begin(&g, 2)) return (*env)->NewStringUTF(env, g.error);
    glClearColor(1, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    unsigned a = pixel(0, 0), b = pixel(15, 15);
    gl_end(&g);
    return probe_string(env, "%08x %08x", a, b);
}

JNIEXPORT jstring JNICALL PROBE_FN(glColorMask)(JNIEnv* env, jclass cls) {
    (void)cls;
    Gl g;
    if (!gl_begin(&g, 2)) return (*env)->NewStringUTF(env, g.error);
    glClearColor(0, 0, 1, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glColorMask(GL_TRUE, GL_FALSE, GL_FALSE, GL_TRUE);
    glClearColor(1, 1, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    unsigned a = pixel(8, 8);
    gl_end(&g);
    return probe_string(env, "%08x", a);
}

JNIEXPORT jstring JNICALL PROBE_FN(glTriangle)(JNIEnv* env, jclass cls) {
    (void)cls;
    Gl g;
    if (!gl_begin(&g, 2)) return (*env)->NewStringUTF(env, g.error);
    char error[256] = "";
    GLuint p = program(k_vs, k_color_fs, error, sizeof error);
    if (!p) {
        gl_end(&g);
        return (*env)->NewStringUTF(env, error);
    }
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glUniform4f(glGetUniformLocation(p, "c"), 0, 0, 1, 1);
    static const float tri[] = {-1, -1, 1, -1, -1, 1};
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, tri);
    glEnableVertexAttribArray(0);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    unsigned in = pixel(2, 2), out = pixel(13, 13);
    glDeleteProgram(p);
    gl_end(&g);
    return probe_string(env, "%08x %08x", in, out);
}

JNIEXPORT jstring JNICALL PROBE_FN(glTexture)(JNIEnv* env, jclass cls) {
    (void)cls;
    Gl g;
    if (!gl_begin(&g, 2)) return (*env)->NewStringUTF(env, g.error);
    char error[256] = "";
    GLuint p = program(k_vs, "precision mediump float; uniform sampler2D s; varying vec2 v; void main() { gl_FragColor = texture2D(s, v); }",
                       error, sizeof error);
    if (!p) {
        gl_end(&g);
        return (*env)->NewStringUTF(env, error);
    }
    static const unsigned char texels[] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glUniform1i(glGetUniformLocation(p, "s"), 0);
    draw_quad();
    unsigned a = pixel(4, 4), b = pixel(12, 4), c = pixel(4, 12), d = pixel(12, 12);
    glDeleteTextures(1, &tex);
    glDeleteProgram(p);
    gl_end(&g);
    return probe_string(env, "%08x %08x %08x %08x", a, b, c, d);
}

JNIEXPORT jstring JNICALL PROBE_FN(glBlend)(JNIEnv* env, jclass cls) {
    (void)cls;
    Gl g;
    if (!gl_begin(&g, 2)) return (*env)->NewStringUTF(env, g.error);
    char error[256] = "";
    GLuint p = program(k_vs, k_color_fs, error, sizeof error);
    if (!p) {
        gl_end(&g);
        return (*env)->NewStringUTF(env, error);
    }
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUniform4f(glGetUniformLocation(p, "c"), 1, 1, 1, 0.5f);
    draw_quad();
    unsigned c = pixel(8, 8);
    glDeleteProgram(p);
    gl_end(&g);
    return probe_string(env, "%u %u %u %u", c >> 24, c >> 16 & 255, c >> 8 & 255, c & 255);
}

JNIEXPORT jstring JNICALL PROBE_FN(glErrors)(JNIEnv* env, jclass cls) {
    (void)cls;
    Gl g;
    if (!gl_begin(&g, 2)) return (*env)->NewStringUTF(env, g.error);
    const char* bad = "precision mediump float; void main() { gl_FragColor = undefined_thing; }";
    GLuint s = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(s, 1, &bad, NULL);
    glCompileShader(s);
    GLint ok = -1, log_length = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    glGetShaderiv(s, GL_INFO_LOG_LENGTH, &log_length);
    glDeleteShader(s);
    while (glGetError() != GL_NO_ERROR) {
    }
    glEnable(0x1234);
    GLenum first = glGetError(), second = glGetError();
    gl_end(&g);
    return probe_string(env, "compile status %d, log %s|0x%04x then 0x%04x", ok, log_length > 1 ? "yes" : "no", first, second);
}

JNIEXPORT jstring JNICALL PROBE_FN(gl3Framebuffer)(JNIEnv* env, jclass cls) {
    (void)cls;
    Gl g;
    if (!gl_begin(&g, 3)) return (*env)->NewStringUTF(env, g.error);
    GLint major = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    GLuint tex, fbo;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glViewport(0, 0, 8, 8);
    glClearColor(0, 0, 1, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    char error[256] = "";
    GLuint p = program("#version 300 es\nin vec2 p; void main() { gl_Position = vec4(p, 0.0, 1.0); }",
                       "#version 300 es\nprecision highp float; uniform int a; uniform uint b; out vec4 o;\n"
                       "void main() { o = vec4(float(a & 5) / 255.0, float(b >> 2u) / 255.0, 0.0, 1.0); }",
                       error, sizeof error);
    unsigned c = 0;
    if (p) {
        glUniform1i(glGetUniformLocation(p, "a"), 7);
        glUniform1ui(glGetUniformLocation(p, "b"), 13);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, k_quad);
        glEnableVertexAttribArray(0);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        c = pixel(3, 3);
        glDeleteProgram(p);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &tex);
    gl_end(&g);
    char m[32];
    if (major >= 3) snprintf(m, sizeof m, "3 or more");
    else snprintf(m, sizeof m, "%d", major);
    if (!p) return probe_string(env, "major %s|complete 0x%04x|%s", m, status, error);
    return probe_string(env, "major %s|complete 0x%04x|%08x", m, status, c);
}

JNIEXPORT jstring JNICALL PROBE_FN(gl1ClearRead)(JNIEnv* env, jclass cls) {
    (void)cls;
    Gl g;
    if (!gl_begin(&g, 1)) return (*env)->NewStringUTF(env, g.error);
    glClearColor(0, 1, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    unsigned a = pixel(8, 8);
    gl_end(&g);
    return probe_string(env, "%08x", a);
}
