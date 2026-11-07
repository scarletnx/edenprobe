# SPDX-License-Identifier: GPL-3.0-or-later
# libandroidprobe.so: the cases that need native code (bionic, JNI, GLES, sockets). C only, so no C++ runtime ships.
cmake_minimum_required(VERSION 3.22)
project(androidprobe C)
add_library(androidprobe SHARED probe.c bionic.c jni_cases.c gles.c net.c)
target_compile_options(androidprobe PRIVATE -std=gnu11 -Wall -Wextra -Werror)
target_link_libraries(androidprobe PRIVATE EGL GLESv3 log m dl)
