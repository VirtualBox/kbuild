/* $Id: electric.h 3150 2018-03-15 18:18:03Z knut.osmundsen@oracle.com $ */
/** @file
 * A simple electric heap implementation, wrapper header.
 */

/*
 * Copyright (c) 2007-2010 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
 *
 * This file is part of kBuild.
 *
 * kBuild is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * kBuild is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with kBuild.  If not, see <http://www.gnu.org/licenses/>
 *
 */

#ifdef ELECTRIC_HEAP

#include <stdlib.h>
#ifdef WINDOWS32
# include <malloc.h>
#endif
#include <string.h> /* strdup */

void xfree (void *);
void *xcalloc (unsigned int);
void *xmalloc (unsigned int);
void *xrealloc (void *, unsigned int);
char *xstrdup (const char *);
#ifdef __GNUC__
void *xmalloc_size_t (size_t size);
void *xcalloc_size_t (size_t size, size_t items);
void *xrealloc_size_t (void *ptr, size_t size);
#endif


#undef  free
//#define free(a)         xfree(a)
#define free            xfree
#undef  strdup
#define strdup(a)       xstrdup(a)

#undef  calloc
#undef  malloc
#undef  realloc
#ifdef __GNUC__
# define calloc(a,b)     xcalloc_size_t(a,b)
# define malloc(a)       xmalloc_size_t(a)
# define realloc(a,b)    xrealloc_size_t(a,b)
#else
# define calloc(a,b)     xcalloc((a) * (b))
# define malloc(a)       xmalloc(a)
# define realloc(a,b)    xrealloc((a),(b))
#endif

#endif

