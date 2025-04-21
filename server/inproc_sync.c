/*
 * In-process synchronization primitives
 *
 * Copyright (C) 2021-2022 Elizabeth Figura for CodeWeavers
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

#include "config.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "winternl.h"

#include "file.h"
#include "handle.h"
#include "request.h"
#include "thread.h"

#ifdef HAVE_LINUX_NTSYNC_H
# include <linux/ntsync.h>
#endif

#ifdef NTSYNC_IOC_EVENT_READ

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

static int get_linux_device(void)
{
    static int fd = -2;

    if (fd == -2)
        fd = open( "/dev/ntsync", O_CLOEXEC | O_RDONLY );

    return fd;
}

int use_inproc_sync(void)
{
    return get_linux_device() >= 0;
}

int create_inproc_event( int manual_reset, int signaled )
{
    struct ntsync_event_args args;
    int device;

    if ((device = get_linux_device()) < 0) return -1;

    args.signaled = signaled;
    args.manual = manual_reset;
    return ioctl( device, NTSYNC_IOC_CREATE_EVENT, &args );
}

int create_inproc_semaphore( unsigned int count, unsigned int max )
{
    struct ntsync_sem_args args;
    int device;

    if ((device = get_linux_device()) < 0) return -1;

    args.count = count;
    args.max = max;
    return ioctl( device, NTSYNC_IOC_CREATE_SEM, &args );
}

int create_inproc_mutex( thread_id_t owner, unsigned int count )
{
    struct ntsync_mutex_args args;
    int device;

    if ((device = get_linux_device()) < 0) return -1;

    args.owner = owner;
    args.count = count;
    return ioctl( device, NTSYNC_IOC_CREATE_MUTEX, &args );
}

void set_inproc_event( int event )
{
    __u32 count;

    if (!use_inproc_sync()) return;

    if (debug_level) fprintf( stderr, "set_inproc_event %d\n", event );

    ioctl( event, NTSYNC_IOC_EVENT_SET, &count );
}

void reset_inproc_event( int event )
{
    __u32 count;

    if (!use_inproc_sync()) return;

    if (debug_level) fprintf( stderr, "reset_inproc_event %d\n", event );

    ioctl( event, NTSYNC_IOC_EVENT_RESET, &count );
}

void abandon_inproc_mutex( thread_id_t tid, int mutex )
{
    ioctl( mutex, NTSYNC_IOC_MUTEX_KILL, &tid );
}

#else

int use_inproc_sync(void)
{
    return 0;
}

int create_inproc_event( int manual_reset, int signaled )
{
    return -1;
}

int create_inproc_semaphore( unsigned int count, unsigned int max )
{
    return -1;
}

int create_inproc_mutex( thread_id_t owner, unsigned int count )
{
    return -1;
}

void set_inproc_event( int event )
{
}

void reset_inproc_event( int event )
{
}

void abandon_inproc_mutex( thread_id_t tid, int mutex )
{
}

#endif


DECL_HANDLER(get_linux_sync_device)
{
#ifdef NTSYNC_IOC_EVENT_READ
    int fd;

    if ((fd = get_linux_device()) >= 0)
        send_client_fd( current->process, fd, 0 );
    else
        set_error( STATUS_NOT_IMPLEMENTED );
#else
    set_error( STATUS_NOT_IMPLEMENTED );
#endif
}

DECL_HANDLER(get_linux_sync_obj)
{
#ifdef NTSYNC_IOC_EVENT_READ
    struct object *obj;
    int fd;

    if ((obj = get_handle_obj( current->process, req->handle, 0, NULL )))
    {
        enum inproc_sync_type type;

        if ((fd = obj->ops->get_inproc_sync( obj, &type )) >= 0)
        {
            reply->type = type;
            reply->access = get_handle_access( current->process, req->handle );
            send_client_fd( current->process, fd, req->handle );
        }
        else
            set_error( STATUS_NOT_IMPLEMENTED );

        release_object( obj );
    }
#else
    set_error( STATUS_NOT_IMPLEMENTED );
#endif
}
