// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The registry's tables (features.inc, made by features.py) and how to read them: the layer and
// its check find an extension or a feature by its name, and a feature in a structure by its
// place after pNext, since every feature structure is sType, pNext and VkBool32 members.

#ifndef EDENPROBE_MASQUERADE_FEATURES_H
#define EDENPROBE_MASQUERADE_FEATURES_H

#include <stddef.h>
#include <string.h>

#include <vulkan/vulkan.h>

typedef struct {
    int stype;       // 0 for VkPhysicalDeviceFeatures, inside VkPhysicalDeviceFeatures2
    int core;        // the core version that has the structure, as tens (13 is 1.3); 0 for none
    int first;       // its first member in MEMBERS
    int count;       // and how many it has
} MaskStruct;

typedef struct {
    const char *extension;
    int structure;   // its place in STRUCTS
} MaskOwner;

#include "features.inc"

#define COUNT(array) ((int)(sizeof(array) / sizeof((array)[0])))

/// The members of a structure: VkPhysicalDeviceFeatures's at its start, every other one's after
/// sType and pNext.
static inline VkBool32 *MaskMembers(void *structure, int stype) {
    return stype == 0 ? (VkBool32 *)structure
                      : (VkBool32 *)((char *)structure + sizeof(VkBaseOutStructure));
}

/// The place in STRUCTS of the structure of this sType, -1 for one the registry has not.
static inline int MaskStructOf(int stype) {
    for (int i = 0; i < COUNT(STRUCTS); ++i) {
        if (STRUCTS[i].stype == stype) {
            return i;
        }
    }
    return -1;
}

static inline int MaskExtension(const char *name, size_t length) {
    for (int i = 0; i < COUNT(DEVICE_EXTENSIONS); ++i) {
        if (strlen(DEVICE_EXTENSIONS[i]) == length && strncmp(DEVICE_EXTENSIONS[i], name, length) == 0) {
            return i;
        }
    }
    return -1;
}

static inline int MaskFeature(const char *name, size_t length) {
    for (int i = 0; i < COUNT(MEMBERS); ++i) {
        if (strlen(MEMBERS[i]) == length && strncmp(MEMBERS[i], name, length) == 0) {
            return i;
        }
    }
    return -1;
}

#endif
