/* $Id: $ */
/** @file
 *
 * File management. 
 *
 * Copyright (c) 2007 knut st. osmundsen <bird-src-spam@anduin.net>
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


#include "shtypes.h"

/**
 * One file.
 */
typedef struct shfile
{
    int                 fd;             /**< The shell file descriptor number. */
    int                 flags;          /**< Open flags. */
    intptr_t            native;         /**< The native file descriptor number. */
} shfile;

/**
 * The file descriptor table for a shell.
 */
typedef struct shfdtab
{
    shmtx               mtx;            /**< Mutex protecting any operations on the table and it's handles. */
    unsigned            size;           /**< The size of the table (number of entries). */
    shfile             *tab;            /**< Pointer to the table. */
} shfdtab;


int shfile_open(shinstance *, const char *, unsigned);

