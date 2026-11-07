# SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# libedenprobe.so: edenprobe's engine and the cases every frontend runs (source/portable.txt), with
# Android's frontend and its own native cases: bionic's own, JNI and GLES. C only, so no C++
# runtime ships. build.gradle names the build (EDENPROBE_VERSION, EDENPROBE_BUILD, EDENPROBE_HASH),
# as the Makefile does for the NRO.
cmake_minimum_required(VERSION 3.22)
project(edenprobe C)

foreach(name EDENPROBE_VERSION EDENPROBE_BUILD EDENPROBE_HASH)
    if(NOT DEFINED ${name})
        message(FATAL_ERROR "${name} is not set: android/build.gradle passes it, from VERSION and git")
    endif()
endforeach()

set(root "${CMAKE_CURRENT_SOURCE_DIR}/../../../..")
file(STRINGS "${root}/source/portable.txt" portable REGEX "^[^#]")
list(TRANSFORM portable PREPEND "${root}/")

add_library(edenprobe SHARED ${portable} frontend.c platform_android.c probe.c bionic.c jni_cases.c
                             gles.c net.c)
target_include_directories(edenprobe PRIVATE "${root}/source")
target_compile_definitions(edenprobe PRIVATE EDENPROBE_VERSION="${EDENPROBE_VERSION}"
                           EDENPROBE_BUILD="${EDENPROBE_BUILD}" EDENPROBE_HASH="${EDENPROBE_HASH}")
# The engine's flags, as host.mk gives them: C11, every warning an error
target_compile_options(edenprobe PRIVATE -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter)
target_link_libraries(edenprobe PRIVATE EGL GLESv3 log m dl)
