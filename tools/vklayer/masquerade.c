// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// VK_LAYER_EDENPROBE_masquerade: makes this machine's Vulkan driver look like one that lacks
// what the variables name, to the emulator above it, so a path the emulator takes only on such
// a driver runs here, under edenprobe. Each mask is off unless its variable is set:
//
//   EDENPROBE_MASQUERADE_API=1.1           the physical device reports that Vulkan version, 1.0 to
//                                          1.4, where its own is newer: an emulator then enables
//                                          the later versions' features through their extensions.
//   EDENPROBE_MASQUERADE_HIDE=NAME,NAME    each NAME a device extension or a feature, by the
//                                          registry's name. An extension leaves the device's
//                                          list, and a feature structure that only hidden
//                                          extensions add reads all VK_FALSE, unless the version
//                                          reported has it in its core. A feature reads VK_FALSE
//                                          in every structure that has it, the core ones too.
//   EDENPROBE_MASQUERADE_NO_HOST_QUERY_RESET=1
//                                          the same as HIDE=VK_EXT_host_query_reset,hostQueryReset.
//
// A name the registry has not, or an API that is no version, makes vkCreateInstance fail with
// VK_ERROR_INITIALIZATION_FAILED and the reason on stderr: a typo would otherwise hide nothing.
// An instance that masks anything says what on stderr as it is made. Everything else is passed
// down untouched. The driver itself is not changed: an application that enables what was hidden
// still gets it, so the masks decide what an emulator chooses, not what the driver can do.
// Enabled with VK_ADD_LAYER_PATH pointing at the directory of the manifest and
// VK_INSTANCE_LAYERS=VK_LAYER_EDENPROBE_masquerade (see tools/vklayer/Makefile).

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vulkan/vk_layer.h>
#include <vulkan/vulkan.h>

#include "features.h"

#define LAYER_NAME "VK_LAYER_EDENPROBE_masquerade"
#define MAX_OBJECTS 16

#if defined(__GNUC__)
#define EXPORT __attribute__((visibility("default")))
#else
#define EXPORT
#endif

typedef struct {
    void *key;
    PFN_vkGetInstanceProcAddr next;
    PFN_vkDestroyInstance destroy;
    PFN_vkGetPhysicalDeviceProperties properties;
    PFN_vkGetPhysicalDeviceProperties2 properties2;
    PFN_vkGetPhysicalDeviceFeatures features;
    PFN_vkGetPhysicalDeviceFeatures2 features2;
    PFN_vkEnumerateDeviceExtensionProperties extensions;
    uint32_t api; // the version reported where the device's is newer, 0 for the device's own
    bool hides;   // an extension or a feature is hidden
    bool hide_extension[COUNT(DEVICE_EXTENSIONS)];
    bool hide_member[COUNT(MEMBERS)];
} Instance;

typedef struct {
    void *key;
    PFN_vkGetDeviceProcAddr next;
    PFN_vkDestroyDevice destroy;
} Device;

static Instance instances[MAX_OBJECTS];
static Device devices[MAX_OBJECTS];

/// The loader's dispatch table pointer, the first word of every dispatchable handle; a physical
/// device shares its instance's.
static void *Key(const void *handle) {
    return *(void *const *)handle;
}

static Instance *FindInstance(const void *handle) {
    void *const key = Key(handle);
    for (int i = 0; i < MAX_OBJECTS; ++i) {
        if (instances[i].key == key) {
            return &instances[i];
        }
    }
    return NULL;
}

static Device *FindDevice(const void *handle) {
    void *const key = Key(handle);
    for (int i = 0; i < MAX_OBJECTS; ++i) {
        if (devices[i].key == key) {
            return &devices[i];
        }
    }
    return NULL;
}

static bool Set(const char *name) {
    const char *const value = getenv(name);
    return value != NULL && value[0] != '\0' && strcmp(value, "0") != 0;
}

/// Hides one name, an extension or a feature (every member of that name); false for a name the
/// registry has not.
static bool Hide(Instance *instance, const char *name, size_t length) {
    const int extension = MaskExtension(name, length);
    if (extension >= 0) {
        instance->hide_extension[extension] = true;
        instance->hides = true;
        return true;
    }
    bool found = false;
    for (int i = 0; i < COUNT(MEMBERS); ++i) {
        if (strlen(MEMBERS[i]) == length && strncmp(MEMBERS[i], name, length) == 0) {
            instance->hide_member[i] = true;
            found = true;
        }
    }
    instance->hides |= found;
    return found;
}

/// The masks from the environment, into the instance; false, with the reason on stderr, for a
/// value that names nothing.
static bool ReadMasks(Instance *instance) {
    const char *const api = getenv("EDENPROBE_MASQUERADE_API");
    if (api != NULL && api[0] != '\0') {
        if (strlen(api) != 3 || api[0] != '1' || api[1] != '.' || api[2] < '0' || api[2] > '4') {
            fprintf(stderr,
                    LAYER_NAME ": EDENPROBE_MASQUERADE_API=%s is not 1.0, 1.1, 1.2, 1.3 or 1.4: no "
                               "instance is made\n",
                    api);
            return false;
        }
        instance->api = VK_MAKE_API_VERSION(0, 1, (uint32_t)(api[2] - '0'), 0);
    }
    if (Set("EDENPROBE_MASQUERADE_NO_HOST_QUERY_RESET")) {
        Hide(instance, "VK_EXT_host_query_reset", strlen("VK_EXT_host_query_reset"));
        Hide(instance, "hostQueryReset", strlen("hostQueryReset"));
    }
    const char *at = getenv("EDENPROBE_MASQUERADE_HIDE");
    while (at != NULL && *at != '\0') {
        at += strspn(at, ", ");
        const size_t length = strcspn(at, ", ");
        if (length != 0 && !Hide(instance, at, length)) {
            fprintf(stderr,
                    LAYER_NAME ": EDENPROBE_MASQUERADE_HIDE names %.*s, which is no device extension or "
                               "feature of the Vulkan registry " MASQUERADE_REGISTRY
                               " this layer was made from: no instance is made\n",
                    (int)length, at);
            return false;
        }
        at += length;
    }
    return true;
}

/// What the instance masks, on stderr, so the run's log says it.
static void SayMasks(const Instance *instance) {
    if (instance->api == 0 && !instance->hides) {
        return;
    }
    fprintf(stderr, LAYER_NAME ":");
    if (instance->api != 0) {
        fprintf(stderr, " Vulkan 1.%u where the device has a newer one;", VK_API_VERSION_MINOR(instance->api));
    }
    if (instance->hides) {
        fprintf(stderr, " hidden:");
        for (int i = 0; i < COUNT(DEVICE_EXTENSIONS); ++i) {
            if (instance->hide_extension[i]) {
                fprintf(stderr, " %s", DEVICE_EXTENSIONS[i]);
            }
        }
        for (int i = 0; i < COUNT(MEMBERS); ++i) {
            bool first = instance->hide_member[i];
            for (int j = 0; j < i && first; ++j) {
                first = !(instance->hide_member[j] && strcmp(MEMBERS[j], MEMBERS[i]) == 0);
            }
            if (first) {
                fprintf(stderr, " %s", MEMBERS[i]);
            }
        }
    }
    fprintf(stderr, "\n");
}

static void MaskVersion(const Instance *instance, VkPhysicalDeviceProperties *properties) {
    if (instance->api != 0 && properties->apiVersion > instance->api) {
        properties->apiVersion = VK_MAKE_API_VERSION(0, 1, VK_API_VERSION_MINOR(instance->api),
                                                     VK_API_VERSION_PATCH(properties->apiVersion));
    }
}

static VKAPI_ATTR void VKAPI_CALL GetPhysicalDeviceProperties(VkPhysicalDevice physical,
                                                              VkPhysicalDeviceProperties *out) {
    const Instance *const instance = FindInstance(physical);
    instance->properties(physical, out);
    MaskVersion(instance, out);
}

static VKAPI_ATTR void VKAPI_CALL GetPhysicalDeviceProperties2(VkPhysicalDevice physical,
                                                               VkPhysicalDeviceProperties2 *out) {
    const Instance *const instance = FindInstance(physical);
    instance->properties2(physical, out);
    MaskVersion(instance, &out->properties);
}

/// The driver's own extensions, for the caller to free with their count; NULL when they could
/// not be listed.
static VkExtensionProperties *DriverExtensions(const Instance *instance, VkPhysicalDevice physical,
                                               uint32_t *count) {
    *count = 0;
    if (instance->extensions(physical, NULL, count, NULL) != VK_SUCCESS) {
        return NULL;
    }
    VkExtensionProperties *const list = calloc(*count ? *count : 1, sizeof(*list));
    if (list != NULL && instance->extensions(physical, NULL, count, list) < 0) {
        free(list);
        return NULL;
    }
    return list;
}

static bool Hidden(const Instance *instance, const char *extension) {
    const int i = MaskExtension(extension, strlen(extension));
    return i >= 0 && instance->hide_extension[i];
}

/// Whether the application still sees an extension that adds structure s: one the driver lists
/// and this layer does not hide.
static bool StillAdded(const Instance *instance, const VkExtensionProperties *list, uint32_t count, int s) {
    for (int o = 0; o < COUNT(OWNERS); ++o) {
        if (OWNERS[o].structure != s || Hidden(instance, OWNERS[o].extension)) {
            continue;
        }
        for (uint32_t i = 0; i < count; ++i) {
            if (strcmp(list[i].extensionName, OWNERS[o].extension) == 0) {
                return true;
            }
        }
    }
    return false;
}

/// The extension list StillAdded reads, fetched once for a whole chain.
typedef struct {
    VkExtensionProperties *list;
    uint32_t count;
    bool fetched;
} Listed;

/// The hidden features of one structure turned off, and every feature of a structure that the
/// version reported has not in its core and no extension the application sees adds.
static void MaskStructure(const Instance *instance, VkPhysicalDevice physical, void *structure, int stype,
                          Listed *listed) {
    const int s = MaskStructOf(stype);
    if (s < 0) {
        return;
    }
    bool all = false;
    if (stype != 0) {
        VkPhysicalDeviceProperties properties;
        GetPhysicalDeviceProperties(physical, &properties);
        const int version =
            (int)(VK_API_VERSION_MAJOR(properties.apiVersion) * 10 + VK_API_VERSION_MINOR(properties.apiVersion));
        if (STRUCTS[s].core == 0 || STRUCTS[s].core > version) {
            if (!listed->fetched) {
                listed->list = DriverExtensions(instance, physical, &listed->count);
                listed->fetched = true;
            }
            all = listed->list != NULL && !StillAdded(instance, listed->list, listed->count, s);
        }
    }
    VkBool32 *const members = MaskMembers(structure, stype);
    for (int m = 0; m < STRUCTS[s].count; ++m) {
        if (all || instance->hide_member[STRUCTS[s].first + m]) {
            members[m] = VK_FALSE;
        }
    }
}

static VKAPI_ATTR void VKAPI_CALL GetPhysicalDeviceFeatures(VkPhysicalDevice physical,
                                                            VkPhysicalDeviceFeatures *out) {
    const Instance *const instance = FindInstance(physical);
    instance->features(physical, out);
    if (instance->hides) {
        Listed listed = { 0 };
        MaskStructure(instance, physical, out, 0, &listed);
    }
}

static VKAPI_ATTR void VKAPI_CALL GetPhysicalDeviceFeatures2(VkPhysicalDevice physical,
                                                             VkPhysicalDeviceFeatures2 *out) {
    const Instance *const instance = FindInstance(physical);
    instance->features2(physical, out);
    if (!instance->hides && instance->api == 0) {
        return;
    }
    Listed listed = { 0 };
    MaskStructure(instance, physical, &out->features, 0, &listed);
    for (VkBaseOutStructure *next = (VkBaseOutStructure *)out->pNext; next != NULL; next = next->pNext) {
        MaskStructure(instance, physical, next, (int)next->sType, &listed);
    }
    free(listed.list);
}

static VKAPI_ATTR VkResult VKAPI_CALL EnumerateDeviceExtensionProperties(
    VkPhysicalDevice physical, const char *layer, uint32_t *count, VkExtensionProperties *out) {
    if (layer != NULL && strcmp(layer, LAYER_NAME) == 0) {
        *count = 0;
        return VK_SUCCESS;
    }
    Instance *const instance = FindInstance(physical);
    if (layer != NULL || !instance->hides) {
        return instance->extensions(physical, layer, count, out);
    }
    uint32_t all = 0;
    VkExtensionProperties *const list = DriverExtensions(instance, physical, &all);
    if (list == NULL) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    uint32_t kept = 0;
    for (uint32_t i = 0; i < all; ++i) {
        if (!Hidden(instance, list[i].extensionName)) {
            list[kept++] = list[i];
        }
    }
    VkResult result = VK_SUCCESS;
    if (out == NULL) {
        *count = kept;
    } else {
        const uint32_t written = *count < kept ? *count : kept;
        memcpy(out, list, written * sizeof(*list));
        if (written < kept) {
            result = VK_INCOMPLETE;
        }
        *count = written;
    }
    free(list);
    return result;
}

static VKAPI_ATTR VkResult VKAPI_CALL CreateInstance(const VkInstanceCreateInfo *info,
                                                     const VkAllocationCallbacks *allocator,
                                                     VkInstance *instance) {
    VkLayerInstanceCreateInfo *link = (VkLayerInstanceCreateInfo *)info->pNext;
    while (link != NULL && !(link->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO &&
                             link->function == VK_LAYER_LINK_INFO)) {
        link = (VkLayerInstanceCreateInfo *)link->pNext;
    }
    if (link == NULL) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    Instance *slot = NULL;
    for (int i = 0; i < MAX_OBJECTS && slot == NULL; ++i) {
        if (instances[i].key == NULL) {
            slot = &instances[i];
        }
    }
    if (slot == NULL) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    memset(slot, 0, sizeof(*slot));
    if (!ReadMasks(slot)) {
        memset(slot, 0, sizeof(*slot));
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    const PFN_vkGetInstanceProcAddr next = link->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    link->u.pLayerInfo = link->u.pLayerInfo->pNext;
    const PFN_vkCreateInstance create = (PFN_vkCreateInstance)next(NULL, "vkCreateInstance");
    const VkResult result = create(info, allocator, instance);
    if (result != VK_SUCCESS) {
        memset(slot, 0, sizeof(*slot));
        return result;
    }
    slot->key = Key(*instance);
    slot->next = next;
    slot->destroy = (PFN_vkDestroyInstance)next(*instance, "vkDestroyInstance");
    slot->properties = (PFN_vkGetPhysicalDeviceProperties)next(*instance, "vkGetPhysicalDeviceProperties");
    slot->properties2 = (PFN_vkGetPhysicalDeviceProperties2)next(*instance, "vkGetPhysicalDeviceProperties2");
    if (slot->properties2 == NULL) {
        slot->properties2 =
            (PFN_vkGetPhysicalDeviceProperties2)next(*instance, "vkGetPhysicalDeviceProperties2KHR");
    }
    slot->features = (PFN_vkGetPhysicalDeviceFeatures)next(*instance, "vkGetPhysicalDeviceFeatures");
    slot->features2 = (PFN_vkGetPhysicalDeviceFeatures2)next(*instance, "vkGetPhysicalDeviceFeatures2");
    if (slot->features2 == NULL) {
        slot->features2 = (PFN_vkGetPhysicalDeviceFeatures2)next(*instance, "vkGetPhysicalDeviceFeatures2KHR");
    }
    slot->extensions =
        (PFN_vkEnumerateDeviceExtensionProperties)next(*instance, "vkEnumerateDeviceExtensionProperties");
    SayMasks(slot);
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL DestroyInstance(VkInstance instance,
                                                  const VkAllocationCallbacks *allocator) {
    Instance *const data = FindInstance(instance);
    const PFN_vkDestroyInstance destroy = data->destroy;
    memset(data, 0, sizeof(*data));
    destroy(instance, allocator);
}

static VKAPI_ATTR VkResult VKAPI_CALL CreateDevice(VkPhysicalDevice physical,
                                                   const VkDeviceCreateInfo *info,
                                                   const VkAllocationCallbacks *allocator,
                                                   VkDevice *device) {
    VkLayerDeviceCreateInfo *link = (VkLayerDeviceCreateInfo *)info->pNext;
    while (link != NULL && !(link->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO &&
                             link->function == VK_LAYER_LINK_INFO)) {
        link = (VkLayerDeviceCreateInfo *)link->pNext;
    }
    if (link == NULL) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    const PFN_vkGetInstanceProcAddr next_instance = link->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    const PFN_vkGetDeviceProcAddr next_device = link->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    link->u.pLayerInfo = link->u.pLayerInfo->pNext;
    const PFN_vkCreateDevice create = (PFN_vkCreateDevice)next_instance(NULL, "vkCreateDevice");
    const VkResult result = create(physical, info, allocator, device);
    if (result != VK_SUCCESS) {
        return result;
    }
    for (int i = 0; i < MAX_OBJECTS; ++i) {
        if (devices[i].key == NULL) {
            devices[i].key = Key(*device);
            devices[i].next = next_device;
            devices[i].destroy = (PFN_vkDestroyDevice)next_device(*device, "vkDestroyDevice");
            return VK_SUCCESS;
        }
    }
    return VK_ERROR_OUT_OF_HOST_MEMORY;
}

static VKAPI_ATTR void VKAPI_CALL DestroyDevice(VkDevice device,
                                                const VkAllocationCallbacks *allocator) {
    Device *const data = FindDevice(device);
    const PFN_vkDestroyDevice destroy = data->destroy;
    memset(data, 0, sizeof(*data));
    destroy(device, allocator);
}

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetDeviceProcAddr(VkDevice device,
                                                                  const char *name);

static PFN_vkVoidFunction Intercepted(const char *name) {
    static const struct {
        const char *name;
        PFN_vkVoidFunction function;
    } TABLE[] = {
        { "vkCreateInstance", (PFN_vkVoidFunction)CreateInstance },
        { "vkDestroyInstance", (PFN_vkVoidFunction)DestroyInstance },
        { "vkCreateDevice", (PFN_vkVoidFunction)CreateDevice },
        { "vkDestroyDevice", (PFN_vkVoidFunction)DestroyDevice },
        { "vkGetDeviceProcAddr", (PFN_vkVoidFunction)GetDeviceProcAddr },
        { "vkGetPhysicalDeviceProperties", (PFN_vkVoidFunction)GetPhysicalDeviceProperties },
        { "vkGetPhysicalDeviceProperties2", (PFN_vkVoidFunction)GetPhysicalDeviceProperties2 },
        { "vkGetPhysicalDeviceProperties2KHR", (PFN_vkVoidFunction)GetPhysicalDeviceProperties2 },
        { "vkGetPhysicalDeviceFeatures", (PFN_vkVoidFunction)GetPhysicalDeviceFeatures },
        { "vkGetPhysicalDeviceFeatures2", (PFN_vkVoidFunction)GetPhysicalDeviceFeatures2 },
        { "vkGetPhysicalDeviceFeatures2KHR", (PFN_vkVoidFunction)GetPhysicalDeviceFeatures2 },
        { "vkEnumerateDeviceExtensionProperties",
          (PFN_vkVoidFunction)EnumerateDeviceExtensionProperties },
    };
    for (size_t i = 0; i < sizeof(TABLE) / sizeof(TABLE[0]); ++i) {
        if (strcmp(TABLE[i].name, name) == 0) {
            return TABLE[i].function;
        }
    }
    return NULL;
}

// Static, and exported below under the Vulkan names only as wrappers: the loader the application
// already loaded exports the same names, and a reference to an exported one from inside this
// library would bind to the loader's, which calls back into the layer chain and never returns.
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetInstanceProcAddr(VkInstance instance,
                                                                    const char *name) {
    const PFN_vkVoidFunction mine = Intercepted(name);
    if (mine != NULL) {
        return mine;
    }
    if (instance == NULL) {
        return NULL;
    }
    Instance *const data = FindInstance(instance);
    return data != NULL ? data->next(instance, name) : NULL;
}

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetDeviceProcAddr(VkDevice device,
                                                                  const char *name) {
    if (strcmp(name, "vkGetDeviceProcAddr") == 0) {
        return (PFN_vkVoidFunction)GetDeviceProcAddr;
    }
    if (strcmp(name, "vkDestroyDevice") == 0) {
        return (PFN_vkVoidFunction)DestroyDevice;
    }
    Device *const data = FindDevice(device);
    return data != NULL ? data->next(device, name) : NULL;
}

EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance,
                                                                      const char *name) {
    return GetInstanceProcAddr(instance, name);
}

EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device,
                                                                    const char *name) {
    return GetDeviceProcAddr(device, name);
}

EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface *version) {
    if (version->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (version->loaderLayerInterfaceVersion > 2) {
        version->loaderLayerInterfaceVersion = 2;
    }
    version->pfnGetInstanceProcAddr = GetInstanceProcAddr;
    version->pfnGetDeviceProcAddr = GetDeviceProcAddr;
    version->pfnGetPhysicalDeviceProcAddr = NULL;
    return VK_SUCCESS;
}
