// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Asks the first physical device, once without the masquerade layer and once through it, about
// each NAME given: a device extension (listed or not, and how many features of the structures it
// adds are on) or a feature (on or not, in every structure that has it). The layer is enabled
// here by name, so VK_ADD_LAYER_PATH must find it and VK_INSTANCE_LAYERS must not name it; its
// masks come from the environment, as they would for an emulator. Queries only, nothing is
// submitted.
//
//   check_masquerade [--expect hidden|same] [--api 1.N] NAME...
//
// --expect hidden fails (exit 1) unless the driver has every NAME and the layer hides each one;
// --expect same fails unless the layer shows each NAME as the driver does; --api fails unless the
// layer reports that version. A layer that refuses the instance is exit 3, an unknown NAME 2.

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vulkan/vulkan.h>

#include "features.h"

typedef struct {
    VkInstance instance;
    VkPhysicalDevice physical;
    VkPhysicalDeviceProperties properties;
    VkExtensionProperties *extensions;
    uint32_t count;
} Side;

static int Open(Side *side, bool layer) {
    static const char *const LAYERS[] = { "VK_LAYER_EDENPROBE_masquerade" };
    const VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                                    .apiVersion = VK_API_VERSION_1_4 };
    const VkInstanceCreateInfo info = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                                        .pApplicationInfo = &app,
                                        .enabledLayerCount = layer ? 1 : 0,
                                        .ppEnabledLayerNames = layer ? LAYERS : NULL };
    const VkResult made = vkCreateInstance(&info, NULL, &side->instance);
    if (made != VK_SUCCESS) {
        printf("%s: vkCreateInstance gave %d\n", layer ? "layer" : "driver", (int)made);
        return 3;
    }
    uint32_t count = 1;
    if (vkEnumeratePhysicalDevices(side->instance, &count, &side->physical) < 0 || count == 0) {
        printf("%s: no device\n", layer ? "layer" : "driver");
        return 3;
    }
    vkGetPhysicalDeviceProperties(side->physical, &side->properties);
    vkEnumerateDeviceExtensionProperties(side->physical, NULL, &side->count, NULL);
    side->extensions = calloc(side->count ? side->count : 1, sizeof(*side->extensions));
    if (side->extensions == NULL) {
        printf("out of memory for %u extensions\n", side->count);
        return 3;
    }
    vkEnumerateDeviceExtensionProperties(side->physical, NULL, &side->count, side->extensions);
    return 0;
}

static bool Listed(const Side *side, const char *name) {
    for (uint32_t i = 0; i < side->count; ++i) {
        if (strcmp(side->extensions[i].extensionName, name) == 0) {
            return true;
        }
    }
    return false;
}

static int Version(const Side *side) {
    return (int)(VK_API_VERSION_MAJOR(side->properties.apiVersion) * 10 +
                 VK_API_VERSION_MINOR(side->properties.apiVersion));
}

/// Whether the driver may be asked for structure s: its version's core has it, or an extension
/// it lists adds it.
static bool Askable(const Side *driver, int s) {
    if (STRUCTS[s].stype == 0 || (STRUCTS[s].core != 0 && STRUCTS[s].core <= Version(driver))) {
        return true;
    }
    for (int o = 0; o < COUNT(OWNERS); ++o) {
        if (OWNERS[o].structure == s && Listed(driver, OWNERS[o].extension)) {
            return true;
        }
    }
    return false;
}

/// The structures picked (wanted[s]) asked of one side in one chain; each one's members land in
/// values at the structure's place in MEMBERS.
static void Ask(const Side *side, const bool *wanted, VkBool32 *values) {
    VkPhysicalDeviceFeatures2 features = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    static void *made[COUNT(STRUCTS)];
    VkBaseOutStructure **tail = (VkBaseOutStructure **)&features.pNext;
    for (int s = 1; s < COUNT(STRUCTS); ++s) {
        made[s] = NULL;
        if (wanted[s]) {
            made[s] = calloc(1, sizeof(VkBaseOutStructure) + sizeof(VkBool32) * (size_t)STRUCTS[s].count);
            if (made[s] == NULL) {
                printf("out of memory for a feature structure\n");
                exit(3);
            }
            ((VkBaseOutStructure *)made[s])->sType = (VkStructureType)STRUCTS[s].stype;
            *tail = made[s];
            tail = &((VkBaseOutStructure *)made[s])->pNext;
        }
    }
    vkGetPhysicalDeviceFeatures2(side->physical, &features);
    memcpy(values, &features.features, sizeof(VkBool32) * (size_t)STRUCTS[0].count);
    for (int s = 1; s < COUNT(STRUCTS); ++s) {
        if (made[s] != NULL) {
            memcpy(values + STRUCTS[s].first, MaskMembers(made[s], STRUCTS[s].stype),
                   sizeof(VkBool32) * (size_t)STRUCTS[s].count);
            free(made[s]);
        }
    }
}

int main(int argc, char **argv) {
    const char *expect = NULL;
    const char *api = NULL;
    int first = 1;
    for (; first + 1 < argc && strncmp(argv[first], "--", 2) == 0; first += 2) {
        if (strcmp(argv[first], "--expect") == 0) {
            expect = argv[first + 1];
        } else if (strcmp(argv[first], "--api") == 0) {
            api = argv[first + 1];
        } else {
            printf("%s is not --expect or --api\n", argv[first]);
            return 2;
        }
    }
    if (expect != NULL && strcmp(expect, "hidden") != 0 && strcmp(expect, "same") != 0) {
        printf("--expect %s is not hidden or same\n", expect);
        return 2;
    }
    if (api != NULL && (strlen(api) != 3 || api[0] != '1' || api[1] != '.' || api[2] < '0' || api[2] > '4')) {
        printf("--api %s is not 1.0 to 1.4\n", api);
        return 2;
    }
    Side driver = { 0 }, layer = { 0 };
    int failed = Open(&driver, false);
    if (failed == 0) {
        failed = Open(&layer, true);
    }
    if (failed != 0) {
        return failed;
    }
    printf("device %s: Vulkan %d.%d, through the layer %d.%d\n", driver.properties.deviceName,
           Version(&driver) / 10, Version(&driver) % 10, Version(&layer) / 10, Version(&layer) % 10);
    if (api != NULL && (Version(&layer) != 10 + api[2] - '0' || Version(&driver) <= Version(&layer))) {
        printf("FAIL: --api %s, and the layer reports %d.%d where the driver has %d.%d\n", api, Version(&layer) / 10,
               Version(&layer) % 10, Version(&driver) / 10, Version(&driver) % 10);
        failed = 1;
    }
    static bool wanted[COUNT(STRUCTS)];
    static VkBool32 seen_driver[COUNT(MEMBERS)], seen_layer[COUNT(MEMBERS)];
    for (int i = first; i < argc; ++i) {
        const size_t length = strlen(argv[i]);
        const bool extension = MaskExtension(argv[i], length) >= 0;
        if (!extension && MaskFeature(argv[i], length) < 0) {
            printf("%s is no device extension or feature of the registry " MASQUERADE_REGISTRY "\n", argv[i]);
            return 2;
        }
        memset(wanted, 0, sizeof(wanted));
        bool has_driver = false, has_layer = false;
        int on_driver = 0, on_layer = 0;
        if (extension) {
            has_driver = Listed(&driver, argv[i]);
            has_layer = Listed(&layer, argv[i]);
            for (int o = 0; o < COUNT(OWNERS); ++o) {
                wanted[OWNERS[o].structure] |= has_driver && strcmp(OWNERS[o].extension, argv[i]) == 0;
            }
        } else {
            for (int s = 0; s < COUNT(STRUCTS); ++s) {
                for (int m = 0; m < STRUCTS[s].count; ++m) {
                    wanted[s] |= strcmp(MEMBERS[STRUCTS[s].first + m], argv[i]) == 0 && Askable(&driver, s);
                }
            }
        }
        Ask(&driver, wanted, seen_driver);
        Ask(&layer, wanted, seen_layer);
        for (int s = 0; s < COUNT(STRUCTS); ++s) {
            for (int m = 0; wanted[s] && m < STRUCTS[s].count; ++m) {
                const int at = STRUCTS[s].first + m;
                if (extension) {
                    on_driver += seen_driver[at] != VK_FALSE;
                    on_layer += seen_layer[at] != VK_FALSE;
                } else if (strcmp(MEMBERS[at], argv[i]) == 0) {
                    has_driver |= seen_driver[at] != VK_FALSE;
                    has_layer |= seen_layer[at] != VK_FALSE;
                }
            }
        }
        if (extension) {
            printf("%s: the driver %s it, %d of its features on; through the layer %s, %d on\n", argv[i],
                   has_driver ? "lists" : "does not list", on_driver, has_layer ? "listed" : "not listed", on_layer);
        } else {
            printf("%s: %s for the driver, %s through the layer\n", argv[i], has_driver ? "on" : "off",
                   has_layer ? "on" : "off");
        }
        if (expect != NULL && strcmp(expect, "hidden") == 0 && !has_driver) {
            printf("FAIL: the driver has no %s, so hiding it proves nothing here\n", argv[i]);
            failed = 1;
        } else if (expect != NULL && strcmp(expect, "hidden") == 0 && (has_layer || on_layer != 0)) {
            printf("FAIL: %s shows through the layer\n", argv[i]);
            failed = 1;
        } else if (expect != NULL && strcmp(expect, "same") == 0 &&
                   (has_layer != has_driver || on_layer != on_driver)) {
            printf("FAIL: the layer shows %s otherwise than the driver\n", argv[i]);
            failed = 1;
        }
    }
    vkDestroyInstance(layer.instance, NULL);
    vkDestroyInstance(driver.instance, NULL);
    free(layer.extensions);
    free(driver.extensions);
    return failed;
}
