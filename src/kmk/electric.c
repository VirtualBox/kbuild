/* $Id: electric.c 2626 2012-08-09 14:11:47Z knut.osmundsen@oracle.com $ */
/** @file
 * A simple electric heap implementation.
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

# ifdef WINDOWS32
#  include <windows.h>
# else
#  include <sys/mman.h>
#  include <errno.h>
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
#ifdef _MSC_VER
  fprintf (stderr, "electric heap error: %s\n", msg);
  __debugbreak ();
#else
  fprintf (stderr, "electric heap error: %s (errno=%d)\n", msg, errno);
  __asm__ ("int3"); /* not portable... */
#endif
  abort ();
  exit (1);
}

static void free_it (void *ptr, unsigned aligned)
{
# ifdef WINDOWS32
  if (!VirtualFree (ptr, 0, MEM_RELEASE))
    fatal_error ("VirtualFree failed");
# else
  if (munmap(ptr, aligned))
    fatal_error ("munmap failed");
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
  if (mprotect(hdr, aligned - 0x1000, PROT_NONE))
    fatal_error ("failed to protect freed memory");
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
      hdr = mmap(NULL, aligned, PROT_EXEC | PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
      if (hdr == MAP_FAILED)
        hdr = 0;
      if (hdr
       && mprotect((char *)hdr + aligned - 0x1000, 0x1000, PROT_NONE))
        fatal_error ("failed to set guard page protection");
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
xcalloc (unsigned size)
{
    void *result;
    result = xmalloc (size);
    return memset (result, 0, size);
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
  if (ptr)
    {
      size_t size = strlen (ptr) + 1;
      char *result = xmalloc (size);
      return memcpy (result, ptr, size);
    }
  return NULL;
}

#else /* !ELECTRIC_HEAP */
extern void electric_heap_keep_ansi_c_quiet (void);
#endif /* !ELECTRIC_HEAP */

