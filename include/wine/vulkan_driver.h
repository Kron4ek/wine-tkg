/* Automatically generated from Vulkan vk.xml; DO NOT EDIT!
 *
 * This file is generated from Vulkan vk.xml file covered
 * by the following copyright and permission notice:
 *
 * Copyright 2015-2024 The Khronos Group Inc.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#ifndef __WINE_VULKAN_DRIVER_H
#define __WINE_VULKAN_DRIVER_H

/* Wine internal vulkan driver version, needs to be bumped upon vulkan_funcs changes. */
#define WINE_VULKAN_DRIVER_VERSION 19

struct vulkan_funcs
{
    /* Vulkan global functions. These are the only calls at this point a graphics driver
     * needs to provide. Other function calls will be provided indirectly by dispatch
     * tables part of dispatchable Vulkan objects such as VkInstance or vkDevice.
     */
    VkResult (*p_vkAcquireNextImage2KHR)(VkDevice, const VkAcquireNextImageInfoKHR *, uint32_t *);
    VkResult (*p_vkAcquireNextImageKHR)(VkDevice, VkSwapchainKHR, uint64_t, VkSemaphore, VkFence, uint32_t *);
    VkResult (*p_vkCreateInstance)(const VkInstanceCreateInfo *, const VkAllocationCallbacks *, VkInstance *);
    VkResult (*p_vkCreateSwapchainKHR)(VkDevice, const VkSwapchainCreateInfoKHR *, const VkAllocationCallbacks *, VkSwapchainKHR *);
    VkResult (*p_vkCreateWin32SurfaceKHR)(VkInstance, const VkWin32SurfaceCreateInfoKHR *, const VkAllocationCallbacks *, VkSurfaceKHR *);
    void (*p_vkDestroyInstance)(VkInstance, const VkAllocationCallbacks *);
    void (*p_vkDestroySurfaceKHR)(VkInstance, VkSurfaceKHR, const VkAllocationCallbacks *);
    void (*p_vkDestroySwapchainKHR)(VkDevice, VkSwapchainKHR, const VkAllocationCallbacks *);
    VkResult (*p_vkEnumerateInstanceExtensionProperties)(const char *, uint32_t *, VkExtensionProperties *);
    void * (*p_vkGetDeviceProcAddr)(VkDevice, const char *);
    void * (*p_vkGetInstanceProcAddr)(VkInstance, const char *);
    VkBool32 (*p_vkGetPhysicalDeviceWin32PresentationSupportKHR)(VkPhysicalDevice, uint32_t);
    VkResult (*p_vkGetSwapchainImagesKHR)(VkDevice, VkSwapchainKHR, uint32_t *, VkImage *);
    VkResult (*p_vkQueuePresentKHR)(VkQueue, const VkPresentInfoKHR *);

    /* winevulkan specific functions */
    VkSurfaceKHR (*p_wine_get_host_surface)(VkSurfaceKHR);
};

static inline void *get_vulkan_driver_device_proc_addr(
        const struct vulkan_funcs *vulkan_funcs, const char *name)
{
    if (!name || name[0] != 'v' || name[1] != 'k') return NULL;

    name += 2;

    if (!strcmp(name, "AcquireNextImage2KHR"))
        return vulkan_funcs->p_vkAcquireNextImage2KHR;
    if (!strcmp(name, "AcquireNextImageKHR"))
        return vulkan_funcs->p_vkAcquireNextImageKHR;
    if (!strcmp(name, "CreateSwapchainKHR"))
        return vulkan_funcs->p_vkCreateSwapchainKHR;
    if (!strcmp(name, "DestroySwapchainKHR"))
        return vulkan_funcs->p_vkDestroySwapchainKHR;
    if (!strcmp(name, "GetDeviceProcAddr"))
        return vulkan_funcs->p_vkGetDeviceProcAddr;
    if (!strcmp(name, "GetSwapchainImagesKHR"))
        return vulkan_funcs->p_vkGetSwapchainImagesKHR;
    if (!strcmp(name, "QueuePresentKHR"))
        return vulkan_funcs->p_vkQueuePresentKHR;

    return NULL;
}

static inline void *get_vulkan_driver_instance_proc_addr(
        const struct vulkan_funcs *vulkan_funcs, VkInstance instance, const char *name)
{
    if (!name || name[0] != 'v' || name[1] != 'k') return NULL;

    name += 2;

    if (!strcmp(name, "CreateInstance"))
        return vulkan_funcs->p_vkCreateInstance;
    if (!strcmp(name, "EnumerateInstanceExtensionProperties"))
        return vulkan_funcs->p_vkEnumerateInstanceExtensionProperties;

    if (!instance) return NULL;

    if (!strcmp(name, "CreateWin32SurfaceKHR"))
        return vulkan_funcs->p_vkCreateWin32SurfaceKHR;
    if (!strcmp(name, "DestroyInstance"))
        return vulkan_funcs->p_vkDestroyInstance;
    if (!strcmp(name, "DestroySurfaceKHR"))
        return vulkan_funcs->p_vkDestroySurfaceKHR;
    if (!strcmp(name, "GetInstanceProcAddr"))
        return vulkan_funcs->p_vkGetInstanceProcAddr;
    if (!strcmp(name, "GetPhysicalDeviceWin32PresentationSupportKHR"))
        return vulkan_funcs->p_vkGetPhysicalDeviceWin32PresentationSupportKHR;

    name -= 2;

    return get_vulkan_driver_device_proc_addr(vulkan_funcs, name);
}

#endif /* __WINE_VULKAN_DRIVER_H */
