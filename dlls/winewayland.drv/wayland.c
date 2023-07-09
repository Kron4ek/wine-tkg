/*
 * Wayland core handling
 *
 * Copyright (c) 2020 Alexandros Frantzis for Collabora Ltd
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#if 0
#pragma makedep unix
#endif

#include "config.h"

#include "waylanddrv.h"

#include "wine/debug.h"

#include <stdlib.h>

WINE_DEFAULT_DEBUG_CHANNEL(waylanddrv);

struct wayland process_wayland =
{
    .output_list = {&process_wayland.output_list, &process_wayland.output_list},
    .output_mutex = PTHREAD_MUTEX_INITIALIZER
};

/**********************************************************************
 *          xdg_wm_base handling
 */

static void xdg_wm_base_handle_ping(void *data, struct xdg_wm_base *shell,
                                    uint32_t serial)
{
    xdg_wm_base_pong(shell, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener =
{
    xdg_wm_base_handle_ping
};

/**********************************************************************
 *          Registry handling
 */

static void registry_handle_global(void *data, struct wl_registry *registry,
                                   uint32_t id, const char *interface,
                                   uint32_t version)
{
    TRACE("interface=%s version=%u id=%u\n", interface, version, id);

    if (strcmp(interface, "wl_output") == 0)
    {
        if (!wayland_output_create(id, version))
            ERR("Failed to create wayland_output for global id=%u\n", id);
    }
    else if (strcmp(interface, "zxdg_output_manager_v1") == 0)
    {
        struct wayland_output *output;

        process_wayland.zxdg_output_manager_v1 =
            wl_registry_bind(registry, id, &zxdg_output_manager_v1_interface,
                             version < 3 ? version : 3);

        /* Add zxdg_output_v1 to existing outputs. */
        wl_list_for_each(output, &process_wayland.output_list, link)
            wayland_output_use_xdg_extension(output);
    }
    else if (strcmp(interface, "wl_compositor") == 0)
    {
        process_wayland.wl_compositor =
            wl_registry_bind(registry, id, &wl_compositor_interface, 4);
    }
    else if (strcmp(interface, "xdg_wm_base") == 0)
    {
        /* Bind version 2 so that compositors (e.g., sway) can properly send tiled
         * states, instead of falling back to (ab)using the maximized state. */
        process_wayland.xdg_wm_base =
            wl_registry_bind(registry, id, &xdg_wm_base_interface,
                             version < 2 ? version : 2);
        xdg_wm_base_add_listener(process_wayland.xdg_wm_base, &xdg_wm_base_listener, NULL);
    }
    else if (strcmp(interface, "wl_shm") == 0)
    {
        process_wayland.wl_shm = wl_registry_bind(registry, id, &wl_shm_interface, 1);
    }
}

static void registry_handle_global_remove(void *data, struct wl_registry *registry,
                                          uint32_t id)
{
    struct wayland_output *output, *tmp;

    TRACE("id=%u\n", id);

    wl_list_for_each_safe(output, tmp, &process_wayland.output_list, link)
    {
        if (output->global_id == id)
        {
            TRACE("removing output->name=%s\n", output->current.name);
            wayland_output_destroy(output);
            return;
        }
    }
}

static const struct wl_registry_listener registry_listener = {
    registry_handle_global,
    registry_handle_global_remove
};

/**********************************************************************
 *          wayland_process_init
 *
 *  Initialise the per process wayland objects.
 *
 */
BOOL wayland_process_init(void)
{
    struct wl_display *wl_display_wrapper;

    process_wayland.wl_display = wl_display_connect(NULL);
    if (!process_wayland.wl_display)
        return FALSE;

    TRACE("wl_display=%p\n", process_wayland.wl_display);

    if (!(process_wayland.wl_event_queue = wl_display_create_queue(process_wayland.wl_display)))
    {
        ERR("Failed to create event queue\n");
        return FALSE;
    }

    if (!(wl_display_wrapper = wl_proxy_create_wrapper(process_wayland.wl_display)))
    {
        ERR("Failed to create proxy wrapper for wl_display\n");
        return FALSE;
    }
    wl_proxy_set_queue((struct wl_proxy *) wl_display_wrapper,
                       process_wayland.wl_event_queue);

    process_wayland.wl_registry = wl_display_get_registry(wl_display_wrapper);
    wl_proxy_wrapper_destroy(wl_display_wrapper);
    if (!process_wayland.wl_registry)
    {
        ERR("Failed to get to wayland registry\n");
        return FALSE;
    }

    /* Populate registry */
    wl_registry_add_listener(process_wayland.wl_registry, &registry_listener, NULL);

    /* We need two roundtrips. One to get and bind globals, one to handle all
     * initial events produced from registering the globals. */
    wl_display_roundtrip_queue(process_wayland.wl_display, process_wayland.wl_event_queue);
    wl_display_roundtrip_queue(process_wayland.wl_display, process_wayland.wl_event_queue);

    /* Check for required protocol globals. */
    if (!process_wayland.wl_compositor)
    {
        ERR("Wayland compositor doesn't support wl_compositor\n");
        return FALSE;
    }
    if (!process_wayland.xdg_wm_base)
    {
        ERR("Wayland compositor doesn't support xdg_wm_base\n");
        return FALSE;
    }
    if (!process_wayland.wl_shm)
    {
        ERR("Wayland compositor doesn't support wl_shm\n");
        return FALSE;
    }

    wayland_init_display_devices(FALSE);

    process_wayland.initialized = TRUE;

    return TRUE;
}
