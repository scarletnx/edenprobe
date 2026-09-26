// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: MIT
//
// Asks the first physical device what the masquerade layer lets through: its API version,
// hostQueryReset and VK_EXT_host_query_reset. Queries only, nothing is submitted.

#include <stdio.h>
#include <string.h>

#include <vulkan/vulkan.h>

int main(void) {
    const VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                                    .apiVersion = VK_API_VERSION_1_3 };
    const VkInstanceCreateInfo info = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                                        .pApplicationInfo = &app };
    VkInstance instance;
    if (vkCreateInstance(&info, NULL, &instance) != VK_SUCCESS) {
        printf("no instance\n");
        return 1;
    }
    uint32_t count = 1;
    VkPhysicalDevice physical;
    if (vkEnumeratePhysicalDevices(instance, &count, &physical) < 0 || count == 0) {
        printf("no device\n");
        return 1;
    }
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(physical, &properties);
    VkPhysicalDeviceHostQueryResetFeatures reset = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES
    };
    VkPhysicalDeviceFeatures2 features = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
                                           .pNext = &reset };
    vkGetPhysicalDeviceFeatures2(physical, &features);
    uint32_t extensions = 0;
    vkEnumerateDeviceExtensionProperties(physical, NULL, &extensions, NULL);
    VkExtensionProperties list[512];
    if (extensions > 512) {
        extensions = 512;
    }
    vkEnumerateDeviceExtensionProperties(physical, NULL, &extensions, list);
    int has_extension = 0;
    for (uint32_t i = 0; i < extensions; ++i) {
        has_extension |= strcmp(list[i].extensionName, VK_EXT_HOST_QUERY_RESET_EXTENSION_NAME) == 0;
    }
    printf("device %s api %u.%u hostQueryReset %u VK_EXT_host_query_reset %d extensions %u\n",
           properties.deviceName, VK_API_VERSION_MAJOR(properties.apiVersion),
           VK_API_VERSION_MINOR(properties.apiVersion), reset.hostQueryReset, has_extension,
           extensions);
    vkDestroyInstance(instance, NULL);
    return 0;
}
