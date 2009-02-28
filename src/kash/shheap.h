/* $Id: shheap.h 2292 2009-02-28 04:46:25Z knut.osmundsen@oracle.com $ */
/** @file
 * The shell memory heap methods.
 */

/*
 * Copyright (c) 2009  knut st. osmundsen <bird-kBuild-spamix@anduin.net>
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

#ifndef ___shheap_h
#define ___shheap_h

#include "shtypes.h"

/* heap */
int shheap_init(void);
void *sh_malloc(shinstance *, size_t);
void *sh_calloc(shinstance *, size_t, size_t);
void *sh_realloc(shinstance *, void *, size_t);
char *sh_strdup(shinstance *, const char *);
void  sh_free(shinstance *, void *);

#endif

