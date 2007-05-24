/* $Id: electric.h 915 2007-05-24 05:00:18Z knut.osmundsen@oracle.com $ */
/** @file
 *
 * A simple electric heap implementation, wrapper header.
 *
 * Copyright (c) 2007 knut st. osmundsen <bird-src-spam@anduin.net>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with This program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifdef ELECTRIC_HEAP

#include <stdlib.h>
#ifdef WINDOWS32
# include <malloc.h>
#endif
#include <string.h> /* strdup */

void xfree (void *);
void *xcalloc (size_t, size_t);
void *xmalloc (unsigned int);
void *xrealloc (void *, unsigned int);
char *xstrdup (const char *);

#define free(a)         xfree(a)
#define calloc(a,b)     xcalloc((a),(b))
#define malloc(a)       xmalloc(a)
#define realloc(a,b)    xrealloc((a),(b))
#define strdup(a)       xstrdup(a)

#endif

