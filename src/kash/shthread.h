/* $Id: shthread.h 3446 2020-09-10 20:48:14Z knut.osmundsen@oracle.com $ */
/** @file
 *
 * Shell thread methods.
 *
 * Copyright (c) 2007-2010 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
 *
 *
 * This file is part of kBuild.
 *
 * kBuild is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * kBuild is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with kBuild; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef ___shthread_h___
#define ___shthread_h___

#include "shtypes.h"

typedef union shmtx
{
    char b[64];
    KU64 au64[64/sizeof(KU64)];
    void *aptrs[64/sizeof(void *)];
} shmtx;

/** Magic mutex value (final u64).
 * This is used to detect whether the mutex has been initialized or not,
 * allowing shmtx_delete to be called more than once without doing harm.
 * @internal */
#define SHMTX_MAGIC        KU64_C(0x8888000019641018) /**< Charles Stross */
/** Index into shmtx::au64 of the SHMTX_MAGIC value.
 * @internal */
#define SHMTX_MAGIC_IDX    (sizeof(shmtx) / sizeof(KU64) - 1)

typedef struct shmtxtmp { int i; } shmtxtmp;

typedef uintptr_t shtid;

void shthread_set_shell(struct shinstance *);
struct shinstance *shthread_get_shell(void);

int shmtx_init(shmtx *pmtx);
void shmtx_delete(shmtx *pmtx);
void shmtx_enter(shmtx *pmtx, shmtxtmp *ptmp);
void shmtx_leave(shmtx *pmtx, shmtxtmp *ptmp);

#endif

