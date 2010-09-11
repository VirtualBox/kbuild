/* $Id: alloccache.c 2413 2010-09-11 17:43:04Z knut.osmundsen@oracle.com $ */
/** @file
 * alloccache - Fixed sized allocation cache.
 *
 * The rational for using an allocation cache, is that it is way faster
 * than malloc+free on most systems.  It may be more efficient as well,
 * depending on the way the heap implementes small allocations.  Also,
 * with the incdep.c code being threaded, all heaps (except for MSC)
 * ran into severe lock contention issues since both the main thread
 * and the incdep worker thread was allocating a crazy amount of tiny
 * allocations (struct dep, struct nameseq, ++).
 *
 * Darwin also showed a significant amount of time spent just executing
 * free(), which is kind of silly.  The alloccache helps a bit here too.
 */

/*
 * Copyright (c) 2008-2010 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
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

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include "make.h"
#include "dep.h"
#include "debug.h"
#include <assert.h>


#ifdef CONFIG_WITH_ALLOC_CACHES

/* Free am item.
   This was not inlined because of aliasing issues arrising with GCC.
   It is also in a separate file for this reason (it used to be in misc.c
   but since free_dep_chain() was using it there, we ran the risk of it
   being inlined and gcc screwing up).  */
void
alloccache_free (struct alloccache *cache, void *item)
{
  struct alloccache_free_ent *f = (struct alloccache_free_ent *)item;
#if 0 /*ndef NDEBUG*/
  struct alloccache_free_ent *c;
  unsigned int i = 0;
  for (c = cache->free_head; c != NULL; c = c->next, i++)
    MY_ASSERT_MSG (c != f && i < 0x10000000,
                   ("i=%u total_count=%u\n", i, cache->total_count));
#endif

  f->next = cache->free_head;
  cache->free_head = f;
  MAKE_STATS(cache->free_count++;);
}

/* Default allocator. */
static void *
alloccache_default_grow_alloc(void *ignore, unsigned int size)
{
  return xmalloc (size);
}

/* Worker for growing the cache. */
struct alloccache_free_ent *
alloccache_alloc_grow (struct alloccache *cache)
{
  void *item;
  unsigned int items = (64*1024 - 32) / cache->size;
  cache->free_start  = cache->grow_alloc (cache->grow_arg, items * cache->size);
  cache->free_end    = cache->free_start + items * cache->size;
  cache->total_count+= items;

#ifndef NDEBUG /* skip the first item so the heap can detect free(). */
  cache->total_count--;
  cache->free_start += cache->size;
#endif

  item = cache->free_start;
  cache->free_start += cache->size;
  /* caller counts */
  return (struct alloccache_free_ent *)item;
}

/* List of alloc caches, for printing. */
static struct alloccache *alloccache_head = NULL;

/* Initializes an alloc cache */
void
alloccache_init (struct alloccache *cache, unsigned int size, const char *name,
                 void *(*grow_alloc)(void *grow_arg, unsigned int size), void *grow_arg)
{
  unsigned act_size;

  /* ensure OK alignment and min sizeof (struct alloccache_free_ent). */
  if (size <= sizeof (struct alloccache_free_ent))
    act_size = sizeof (struct alloccache_free_ent);
  else if (size <= 32)
    {
      act_size = 4;
      while (act_size < size)
        act_size <<= 1;
    }
  else
    act_size = (size + 31U) & ~(size_t)31;

  /* align the structure. */
  cache->free_start  = NULL;
  cache->free_end    = NULL;
  cache->free_head   = NULL;
  cache->size        = act_size;
  cache->total_count = 0;
  cache->alloc_count = 0;
  cache->free_count  = 0;
  cache->name        = name;
  cache->grow_arg    = grow_arg;
  cache->grow_alloc  = grow_alloc ? grow_alloc : alloccache_default_grow_alloc;

  /* link it. */
  cache->next        = alloccache_head;
  alloccache_head    = cache;
}

/* Terminate an alloc cache, free all the memory it contains. */
void
alloccache_term (struct alloccache *cache,
                 void (*term_free)(void *term_arg, void *ptr, unsigned int size), void *term_arg)
{
    /*cache->size = 0;*/
    (void)cache;
    (void)term_free;
    (void)term_arg;
    /* FIXME: Implement memory segment tracking and cleanup. */
}

/* Joins to caches, unlinking the 2nd one. */
void
alloccache_join (struct alloccache *cache, struct alloccache *eat)
{
  assert (cache->size == eat->size);

#if 0 /* probably a waste of time */ /* FIXME: Optimize joining, avoid all list walking. */
  /* add the free list... */
  if (eat->free_head)
    {
     unsigned int eat_in_use = eat->alloc_count - eat->free_count;
     unsigned int dst_in_use = cache->alloc_count - cache->free_count;
     if (!cache->free_head)
       cache->free_head = eat->free_head;
     else if (eat->total_count - eat_in_use < cache->total_count - dst_ins_use)
       {
         struct alloccache_free_ent *last = eat->free_head;
         while (last->next)
           last = last->next;
         last->next = cache->free_head;
         cache->free_head = eat->free_head;
       }
     else
       {
         struct alloccache_free_ent *last = cache->free_head;
         while (last->next)
           last = last->next;
         last->next = eat->free_head;
       }
    }

  /* ... and the free space. */
  while (eat->free_start != eat->free_end)
    {
      struct alloccache_free_ent *f = (struct alloccache_free_ent *)eat->free_start;
      eat->free_start += eat->size;
      f->next = cache->free_head;
      cache->free_head = f;
    }

  /* and statistics */
  cache->alloc_count += eat->alloc_count;
  cache->free_count  += eat->free_count;
#else
  /* and statistics */
  cache->alloc_count += eat->alloc_count;
  cache->free_count  += eat->free_count;
#endif
  cache->total_count += eat->total_count;

  /* unlink and disable the eat cache */
  if (alloccache_head == eat)
    alloccache_head = eat->next;
  else
    {
      struct alloccache *cur = alloccache_head;
      while (cur->next != eat)
        cur = cur->next;
      assert (cur && cur->next == eat);
      cur->next = eat->next;
    }

  eat->size = 0;
  eat->free_end = eat->free_start = NULL;
  eat->free_head = NULL;
}

/* Print one alloc cache. */
void
alloccache_print (struct alloccache *cache)
{
  printf (_("\n# Alloc Cache: %s\n"
              "#  Items: size = %-3u  total = %-6u"),
          cache->name, cache->size, cache->total_count);
  MAKE_STATS(printf (_("  in-use = %-6lu"),
                     cache->alloc_count - cache->free_count););
  MAKE_STATS(printf (_("\n#         alloc calls = %-7lu  free calls = %-7lu"),
                     cache->alloc_count, cache->free_count););
  printf ("\n");
}

/* Print all alloc caches. */
void
alloccache_print_all (void)
{
  struct alloccache *cur;
  puts ("");
  for (cur = alloccache_head; cur; cur = cur->next)
    alloccache_print (cur);
}

#endif /* CONFIG_WITH_ALLOC_CACHES */

