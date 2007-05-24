/* $Id: electric.c 909 2007-05-24 03:09:09Z knut.osmundsen@oracle.com $ */
/** @file
 *
 * A simple electric heap implementation.
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

# ifdef WINDOWS32
#  include <windows.h>
# else
#  include <sys/mman.h>
# endif
# include <string.h>
# include <stdlib.h>
# include <stdio.h>


# define FREED_ENTRIES 512
static struct
{
  void *ptr;
  unsigned aligned;
} freed[FREED_ENTRIES];
static unsigned freed_head = 0;
static unsigned freed_tail = 0;


static void fatal_error (const char *msg)
{
  fprintf (stderr, "electric heap error: %s\n", msg);
  abort ();
  exit (1);
}

static void free_it (void *ptr, unsigned aligned)
{
# ifdef WINDOWS32
  if (!VirtualFree (ptr, 0, MEM_RELEASE))
    fatal_error ("VirtualFree failed");
# else
# endif
}

/* Return 1 if something was freed, 0 otherwise. */
static int free_up_some (void)
{
  if (freed_tail == freed_head)
    return 0;
  free_it (freed[freed_tail].ptr, freed[freed_tail].aligned);
  freed[freed_tail].ptr = NULL;
  freed[freed_tail].aligned = 0;
  freed_tail = (freed_tail + 1) % FREED_ENTRIES;
  return 1;
}

static unsigned *get_hdr (void *ptr)
{
  if (((uintptr_t)ptr & 0xfff) < sizeof(unsigned))
    return (unsigned *)(((uintptr_t)ptr - 0x1000) & ~0xfff);
  return (unsigned *)((uintptr_t)ptr & ~0xfff);
}

void xfree (void *ptr)
{
  unsigned int size, aligned;
  unsigned *hdr;
# ifdef WINDOWS32
  DWORD fFlags = PAGE_NOACCESS;
# endif

  if (!ptr)
    return;

  hdr = get_hdr (ptr);
  size = *hdr;
  aligned = (size + 0x1fff + sizeof(unsigned)) & ~0xfff;
# ifdef WINDOWS32
  if (!VirtualProtect (hdr, aligned - 0x1000, fFlags, &fFlags))
    fatal_error ("failed to protect freed memory");
# else
# endif

  freed[freed_head].ptr = hdr;
  freed[freed_head].aligned = aligned;
  if (((freed_head + 1) % FREED_ENTRIES) == freed_tail)
    free_up_some();
  freed_head = (freed_head + 1) % FREED_ENTRIES;
}

void *
xmalloc (unsigned int size)
{
  /* Make sure we don't allocate 0, for pre-ANSI libraries.  */
  unsigned int aligned = (size + 0x1fff + sizeof(unsigned)) & ~0xfff;
  unsigned *hdr;
  unsigned i;
  for (i = 0; i < FREED_ENTRIES; i++)
    {
# ifdef WINDOWS32
      DWORD fFlags = PAGE_NOACCESS;
      hdr = VirtualAlloc(NULL, aligned, MEM_COMMIT, PAGE_READWRITE);
      if (hdr
       && !VirtualProtect((char *)hdr + aligned - 0x1000, 0x1000, fFlags, &fFlags))
        fatal_error ("failed to set guard page protection");
# else
# endif
      if (hdr)
        break;
      if (!free_up_some ())
        break;
    }
  if (hdr == 0)
    fatal_error ("virtual memory exhausted");

  *hdr = size;
# if 0
  return hdr + 1;
# else
  return (char *)hdr + aligned - 0x1000 - size;
# endif
}

void *
xcalloc (size_t size, size_t items)
{
    void *result;
    result = xmalloc (size * items);
    return memset (result, 0, size * items);
}

void *
xrealloc (void *ptr, unsigned int size)
{
  void *result;
  result = xmalloc (size);
  if (ptr)
    {
      unsigned *hdr = get_hdr (ptr);
      unsigned int oldsize = *hdr;
      memcpy (result, ptr, oldsize >= size ? size : oldsize);
      xfree (ptr);
    }
  return result;
}

char *
xstrdup (const char *ptr)
{
  size_t size = strlen (ptr) + 1;
  char *result = xmalloc (size);
  return memcpy (result, ptr, size);
}

#endif /* ELECTRIC_HEAP */

