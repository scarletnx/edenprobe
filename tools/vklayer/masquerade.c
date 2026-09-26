// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT
//
// VK_LAYER_EDENPROBE_masquerade: makes this machine's Vulkan driver look like an older one to
// the emulator above it, so a path the emulator takes only on such a driver runs here, under
// edenprobe. Two masks, each off unless its variable is set:
//
//   EDENPROBE_MASQUERADE_API=1.1           the physical device reports Vulkan 1.1: an emulator
//                                          then enables Vulkan 1.2 and 1.3 features through their
//                                          extensions, as it does on a Vulkan 1.1 driver.
//   EDENPROBE_MASQUERADE_NO_HOST_QUERY_RESET=1
//                                          no VK_EXT_host_query_reset and no hostQueryReset
//                                          feature: queries are reset in command buffers.
//
// Everything else is passed down untouched. The driver itself is not changed: an application
// that enables what was hidden still gets it, so the masks decide what an emulator chooses, not
// what the driver can do. Enabled with VK_ADD_LAYER_PATH pointing at the directory of the
// manifest and VK_INSTANCE_LAYERS=VK_LAYER_EDENPROBE_masquerade (see tools/vklayer/Makefile).

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <vulkan/vk_layer.h>
#include <vulkan/vulkan.h>

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
    PFN_vkGetPhysicalDeviceFeatures2 features2;
    PFN_vkEnumerateDeviceExtensionProperties extensions;
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

static bool ApiIs11(void) {
    const char *const value = getenv("EDENPROBE_MASQUERADE_API");
    return value != NULL && strcmp(value, "1.1") == 0;
}

static bool NoHostQueryReset(void) {
    return Set("EDENPROBE_MASQUERADE_NO_HOST_QUERY_RESET");
}

static void Mask(VkPhysicalDeviceProperties *properties) {
    if (ApiIs11() && properties->apiVersion > VK_API_VERSION_1_1) {
        const uint32_t patch = VK_API_VERSION_PATCH(properties->apiVersion);
        properties->apiVersion = VK_MAKE_API_VERSION(0, 1, 1, patch);
    }
}

static VKAPI_ATTR void VKAPI_CALL GetPhysicalDeviceProperties(VkPhysicalDevice physical,
                                                              VkPhysicalDeviceProperties *out) {
    FindInstance(physical)->properties(physical, out);
    Mask(out);
}

static VKAPI_ATTR void VKAPI_CALL GetPhysicalDeviceProperties2(VkPhysicalDevice physical,
                                                               VkPhysicalDeviceProperties2 *out) {
    FindInstance(physical)->properties2(physical, out);
    Mask(&out->properties);
}

static VKAPI_ATTR void VKAPI_CALL GetPhysicalDeviceFeatures2(VkPhysicalDevice physical,
                                                             VkPhysicalDeviceFeatures2 *out) {
    FindInstance(physical)->features2(physical, out);
    if (!NoHostQueryReset()) {
        return;
    }
    for (VkBaseOutStructure *next = (VkBaseOutStructure *)out->pNext; next != NULL;
         next = next->pNext) {
        if (next->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES) {
            ((VkPhysicalDeviceHostQueryResetFeatures *)next)->hostQueryReset = VK_FALSE;
        } else if (next->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES) {
            ((VkPhysicalDeviceVulkan12Features *)next)->hostQueryReset = VK_FALSE;
        }
    }
}

static VKAPI_ATTR VkResult VKAPI_CALL EnumerateDeviceExtensionProperties(
    VkPhysicalDevice physical, const char *layer, uint32_t *count, VkExtensionProperties *out) {
    if (layer != NULL && strcmp(layer, LAYER_NAME) == 0) {
        *count = 0;
        return VK_SUCCESS;
    }
    Instance *const instance = FindInstance(physical);
    if (layer != NULL || !NoHostQueryReset()) {
        return instance->extensions(physical, layer, count, out);
    }
    uint32_t all = 0;
    VkResult result = instance->extensions(physical, NULL, &all, NULL);
    if (result != VK_SUCCESS) {
        return result;
    }
    VkExtensionProperties *const list = calloc(all ? all : 1, sizeof(*list));
    if (list == NULL) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    result = instance->extensions(physical, NULL, &all, list);
    uint32_t kept = 0;
    for (uint32_t i = 0; i < all; ++i) {
        if (strcmp(list[i].extensionName, VK_EXT_HOST_QUERY_RESET_EXTENSION_NAME) != 0) {
            list[kept++] = list[i];
        }
    }
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
    const PFN_vkGetInstanceProcAddr next = link->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    link->u.pLayerInfo = link->u.pLayerInfo->pNext;
    const PFN_vkCreateInstance create = (PFN_vkCreateInstance)next(NULL, "vkCreateInstance");
    const VkResult result = create(info, allocator, instance);
    if (result != VK_SUCCESS) {
        return result;
    }
    for (int i = 0; i < MAX_OBJECTS; ++i) {
        if (instances[i].key == NULL) {
            Instance *const slot = &instances[i];
            slot->key = Key(*instance);
            slot->next = next;
            slot->destroy = (PFN_vkDestroyInstance)next(*instance, "vkDestroyInstance");
            slot->properties =
                (PFN_vkGetPhysicalDeviceProperties)next(*instance, "vkGetPhysicalDeviceProperties");
            slot->properties2 = (PFN_vkGetPhysicalDeviceProperties2)next(
                *instance, "vkGetPhysicalDeviceProperties2");
            if (slot->properties2 == NULL) {
                slot->properties2 = (PFN_vkGetPhysicalDeviceProperties2)next(
                    *instance, "vkGetPhysicalDeviceProperties2KHR");
            }
            slot->features2 =
                (PFN_vkGetPhysicalDeviceFeatures2)next(*instance, "vkGetPhysicalDeviceFeatures2");
            if (slot->features2 == NULL) {
                slot->features2 = (PFN_vkGetPhysicalDeviceFeatures2)next(
                    *instance, "vkGetPhysicalDeviceFeatures2KHR");
            }
            slot->extensions = (PFN_vkEnumerateDeviceExtensionProperties)next(
                *instance, "vkEnumerateDeviceExtensionProperties");
            return VK_SUCCESS;
        }
    }
    return VK_ERROR_OUT_OF_HOST_MEMORY;
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
