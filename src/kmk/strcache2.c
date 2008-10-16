/* $Id: strcache2.c 1869 2008-10-16 05:05:04Z knut.osmundsen@oracle.com $ */
/** @file
 * strcache2 - New string cache.
 */

/*
 * Copyright (c) 2008 knut st. osmundsen <bird-src-spam@anduin.net>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include "make.h"
#include "strcache2.h"

#include <assert.h>

#include "debug.h"

#ifdef WINDOWS32
# include <io.h>
# include <process.h>
# include <Windows.h>
# define PARSE_IN_WORKER
#endif

#ifdef __OS2__
# include <sys/fmutex.h>
#endif

#ifdef HAVE_PTHREAD
# include <pthread.h>
#endif


/*******************************************************************************
*   Defined Constants And Macros                                               *
*******************************************************************************/
/* The default size of a memory segment (1MB). */
#define STRCACHE2_SEG_SIZE            (1024U*1024U)
/* The initial hash table size (number of entries). Power of two. */
#define STRCACHE2_INITIAL_SIZE        0x10000U


/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
/* the deleted filler hash table entry. */
static struct strcache2_entry deleted_entry;


static struct strcache2_seg *
strcache2_new_seg (struct strcache2 *cache, unsigned int minlen)
{
  struct strcache2_seg *seg;
  size_t size;

  size = STRCACHE2_SEG_SIZE;
  if (size < (size_t)minlen + sizeof (struct strcache2_seg))
    {
      size = (size_t)minlen * 2;
      size = (size + 0xfff) & ~(size_t)0xfff;
    }

  seg = xmalloc (size);
  seg->cursor = seg->start = (char *)(seg + 1);
  seg->size = seg->avail = size - sizeof (struct strcache2_seg);
  assert (seg->size > minlen);

  seg->next = cache->seg_head;
  cache->seg_head = seg;

  return seg;
}

MY_INLINE unsigned int
strcache2_case_sensitive_hash_1 (const char *str, unsigned int len)
{
  unsigned int hash = 0;
  if (MY_PREDICT_TRUE(len >= 2))
    {
      unsigned int ch0 = *str++;
      hash = len--;
      while (len >= 2)
        {
          unsigned int ch1 = *str++;
          hash += ch0 << (ch1 & 0xf);

          ch0 = *str++;
          hash += ch1 << (ch0 & 0xf);

          len -= 2;
        }
      if (len == 1)
        {
          unsigned ch1 = *str;
          hash += ch0 << (ch1 & 0xf);

          hash += ch1;
        }
      else
        hash += ch0;
    }
  else if (len)
    {
      hash = *str;
      hash += hash << (hash & 0xf);
    }
  else
    hash = 0;
  return hash;
}

MY_INLINE unsigned int
strcache2_case_sensitive_hash_2 (const char *str, unsigned int len)
{
  unsigned int hash = 0;
  if (MY_PREDICT_TRUE(len >= 2))
    {
      unsigned int ch0 = *str++;
      hash = len--;
      while (len >= 2)
        {
          unsigned int ch1 = *str++;
          hash += ch0 << (ch1 & 0x7);

          ch0 = *str++;
          hash += ch1 << (ch0 & 0x7);

          len -= 2;
        }
      if (len == 1)
        {
          unsigned ch1 = *str;
          hash += ch0 << (ch1 & 0x7);

          hash += ch1;
        }
      else
        hash += ch0;
    }
  else if (len)
    {
      hash = *str;
      hash += hash << (hash & 0x7);
    }
  else
    hash = 0;
  return hash | 1;
}

MY_INLINE unsigned int
strcache2_case_insensitive_hash_1 (const char *str, unsigned int len)
{
  unsigned int hash = 0;
  if (MY_PREDICT_TRUE(len >= 2))
    {
      unsigned int ch0 = *str++;
      ch0 = tolower (ch0);
      hash = len--;
      while (len >= 2)
        {
          unsigned int ch1 = *str++;
          ch1 = tolower (ch1);
          hash += ch0 << (ch1 & 0xf);

          ch0 = *str++;
          ch0 = tolower (ch0);
          hash += ch1 << (ch0 & 0xf);

          len -= 2;
        }
      if (len == 1)
        {
          unsigned ch1 = *str;
          ch1 = tolower (ch1);
          hash += ch0 << (ch1 & 0xf);

          hash += ch1;
        }
      else
        hash += ch0;
    }
  else if (len)
    {
      hash = *str;
      hash += hash << (hash & 0xf);
    }
  else
    hash = 0;
  return hash;
}

MY_INLINE unsigned int
strcache2_case_insensitive_hash_2 (const char *str, unsigned int len)
{
  unsigned int hash = 0;
  if (MY_PREDICT_TRUE(len >= 2))
    {
      unsigned int ch0 = *str++;
      ch0 = tolower (ch0);
      hash = len--;
      while (len >= 2)
        {
          unsigned int ch1 = *str++;
          ch1 = tolower (ch1);
          hash += ch0 << (ch1 & 0x7);

          ch0 = *str++;
          ch0 = tolower (ch0);
          hash += ch1 << (ch0 & 0x7);

          len -= 2;
        }
      if (len == 1)
        {
          unsigned ch1 = *str;
          ch1 = tolower (ch1);
          hash += ch0 << (ch1 & 0x7);

          hash += ch1;
        }
      else
        hash += ch0;
    }
  else if (len)
    {
      hash = *str;
      hash += hash << (hash & 0x7);
    }
  else
    hash = 0;
  return hash;
}

MY_INLINE int
strcache2_is_equal (struct strcache2 *cache, struct strcache2_entry const *entry,
                    const char *str, unsigned int length, unsigned int hash1)
{
  /* the simple stuff first. */
  if (   entry == NULL
      || entry == &deleted_entry
      || entry->hash1 != hash1
      || entry->length != length)
      return 0;

  return !cache->case_insensitive
    ? memcmp (cache + 1, str, length) == 0
#if defined(_MSC_VER) || defined(__OS2__)
    : _memicmp (cache + 1, str, length) == 0
#else
    : strncasecmp ((const char *)(cache + 1), str, length) == 0
#endif
    ;
}

static void
strcache2_rehash (struct strcache2 *cache)
{
  /* TODO */
  struct strcache2 **ptr = (struct strcache2 **)1;
  assert(0);
  *ptr = cache;
}

/* Internal worker that enters a new string into the cache. */
static const char *
strcache2_enter_string (struct strcache2 *cache, unsigned int idx,
                        const char *str, unsigned int length,
                        unsigned int hash1, unsigned hash2)
{
  struct strcache2_entry *entry;
  struct strcache2_seg *seg;
  unsigned int size;
  char *str_copy;

  /* Allocate space for the string. */

  size = length + 1 + sizeof (struct strcache2_entry);
  size = (size + sizeof (void *) - 1) & ~(sizeof (void *) - 1U);

  seg = cache->seg_head;
  if (MY_PREDICT_FALSE(seg->avail < size))
    {
      do
        seg = seg->next;
      while (seg && seg->avail < size);
      if (!seg)
        seg = strcache2_new_seg (cache, size);
    }

  entry = (struct strcache2_entry *) seg->cursor;
  seg->cursor += size;
  seg->avail -= size;

  /* Setup the entry, copy the string and insert it into the hash table. */

  entry->user = NULL;
  entry->length = length;
  entry->hash1 = hash1;
  entry->hash2 = hash2;
  str_copy = (char *) memcpy (entry + 1, str, length);
  str_copy[length] = '\0';

  cache->hash_tab[idx] = entry;
  cache->count++;
  if (cache->count >= cache->rehash_count)
    strcache2_rehash (cache);

  return str_copy;
}

/* The public add/lookup string interface. */
const char *
strcache2_add (struct strcache2 *cache, const char *str, unsigned int length)
{
  struct strcache2_entry const *entry;
  unsigned int hash2;
  unsigned int hash1 = cache->case_insensitive
    ? strcache2_case_insensitive_hash_1 (str, length)
    : strcache2_case_sensitive_hash_1 (str, length);
  unsigned int idx;

  cache->lookup_count++;

  /* Lookup the entry in the hash table, hoping for an
     early match. */

  idx = hash1 / cache->hash_size;
  entry = cache->hash_tab[idx];
  if (strcache2_is_equal (cache, entry, str, length, hash1))
    return (const char *)(entry + 1);
  if (!entry)
    hash2 = 0;
  else
    {
      unsigned int deleted_idx = entry == &deleted_entry ? idx : ~0U;
      cache->collision_count++;

      hash2 = cache->case_insensitive
        ? strcache2_case_insensitive_hash_2 (str, length)
        : strcache2_case_sensitive_hash_2 (str, length);
      idx += hash2;
      idx /= cache->hash_size;
      entry = cache->hash_tab[idx];
      if (strcache2_is_equal (cache, entry, str, length, hash1))
        return (const char *)(entry + 1);

      if (entry)
        {
          do
            {
              if (deleted_idx == ~0U && entry == &deleted_entry)
                deleted_idx = idx;

              idx += hash2;
              idx /= cache->hash_size;
              entry = cache->hash_tab[idx];
              if (strcache2_is_equal (cache, entry, str, length, hash1))
                return (const char *)(entry + 1);
            }
          while (entry);
        }
      if (deleted_idx != ~0U)
        idx = deleted_idx;
    }

  /* Not found, add it at IDX. */
  return strcache2_enter_string (cache, idx, str, length, hash1, hash2);
}

/* Is the given string cached? returns 1 if it is, 0 if it isn't. */
int
strcache2_is_cached (struct strcache2 *cache, const char *str)
{
  /* Check mandatory alignment first. */

  if (!((size_t)str & (sizeof (void *) - 1)))
    {
      /* Check the segment list and consider the question answered if the
         string is within one of them. (Could check it more thoroughly...) */

      struct strcache2_seg const *seg;
      for (seg = cache->seg_head; seg; seg++)
        if ((size_t)(str - seg->start) < seg->size)
            return 1;
    }

  return 0;
}


/* Verify the integrity of the specified string, returning 0 if OK. */
int
strcache2_verify_entry (struct strcache2 *cache, const char *str)
{
  struct strcache2_entry const *entry;
  unsigned hash;
  const char *end;

  if ((size_t)str & (sizeof (void *) - 1))
    {
      fprintf (stderr,
               "strcache2[%s]: missaligned string %p\nstring: %s\n",
               cache->name, (void *)str, str);
      return -1;
    }

  entry = (struct strcache2_entry const *)str - 1;
  end = memchr (str, '\0', entry->length);
  if ((size_t)(end - str) == entry->length)
    {
      fprintf (stderr,
               "strcache2[%s]: corrupt entry %p, length: %lu, expected %u;\nstring: %s\n",
               cache->name, (void *)entry, (unsigned long)(end - str), entry->length, str);
      return -1;
    }

  hash = cache->case_insensitive
    ? strcache2_case_insensitive_hash_1 (str, entry->length)
    : strcache2_case_sensitive_hash_1 (str, entry->length);
  if (hash != entry->hash1)
    {
      fprintf (stderr,
               "strcache2[%s]: corrupt entry %p, hash#1: %x, expected %x;\nstring: %s\n",
               cache->name, (void *)entry, hash, entry->hash1, str);
      return -1;
    }

  if (entry->hash2)
    {
      hash = cache->case_insensitive
        ? strcache2_case_insensitive_hash_2 (str, entry->length)
        : strcache2_case_sensitive_hash_2 (str, entry->length);
      if (hash != entry->hash1)
        {
          fprintf (stderr,
                   "strcache2[%s]: corrupt entry %p, hash#2: %x, expected %x;\nstring: %s\n",
                   cache->name, (void *)entry, hash, entry->hash2, str);
          return -1;
        }
    }

  return 0;
}

/* Fallback for calculating and returning the 2nd hash. */
unsigned int
strcache2_get_hash2_fallback (struct strcache2 *cache, const char *str)
{
  struct strcache2_entry *entry = (struct strcache2_entry *) str - 1;
  unsigned hash2 = cache->case_insensitive
      ? strcache2_case_insensitive_hash_2 (str, entry->length)
      : strcache2_case_sensitive_hash_2 (str, entry->length);
  entry->hash2 = hash2;
  return hash2;
}

/* List of initialized string caches. */
static struct strcache2 *strcache_head;

/* Initalizes a new cache. */
void
strcache2_init (struct strcache2 *cache, const char *name,
                int case_insensitive, int thread_safe)
{
  assert (!thread_safe);

  cache->case_insensitive = case_insensitive;
  cache->hash_size = STRCACHE2_INITIAL_SIZE;
  cache->count = 0;
  cache->rehash_count = STRCACHE2_INITIAL_SIZE / 4 * 3;
  cache->collision_count = 0;
  cache->lookup_count = 0;
  cache->lock = NULL;
  cache->seg_head = NULL;
  cache->name = name;

  cache->hash_tab = (struct strcache2_entry **)
    xmalloc (STRCACHE2_INITIAL_SIZE * sizeof (struct strcache2_entry *));
  memset (cache->hash_tab, '\0', STRCACHE2_INITIAL_SIZE * sizeof (struct strcache2_entry *));
  strcache2_new_seg (cache, 0);

  /* link it */
  cache->next = strcache_head;
  strcache_head = cache;
}


/* Terminates a string cache, freeing all memory and other
   associated resources. */
void
strcache2_term (struct strcache2 *cache)
{
  /* unlink it */

  if (strcache_head == cache)
    strcache_head = cache->next;
  else
    {
      struct strcache2 *prev = strcache_head;
      while (prev->next != cache)
        prev = prev->next;
      assert (prev);
      prev->next = cache->next;
    }

  /* free the memory segments */

  do
    {
      void *free_it = cache->seg_head;
      cache->seg_head = cache->seg_head->next;
      free (free_it);
    }
  while (cache->seg_head);

  /* free the hash and clear the structure. */

  free (cache->hash_tab);
  memset (cache, '\0', sizeof (struct strcache2));
}


void
strcache2_print_stats (struct strcache2 *cache, const char *prefix)
{
  unsigned int  seg_count = 0;
  unsigned long seg_total_bytes = 0;
  unsigned long seg_avg_bytes;
  unsigned long seg_avail_bytes = 0;
  unsigned long seg_max_bytes = 0;
  struct strcache2_seg *seg;
  unsigned long str_total_len = 0;
  unsigned int  str_avg_len;
  unsigned int  str_min_len = ~0U;
  unsigned int  str_max_len = 0;
  unsigned int  idx;
  unsigned int  rehashes;

  printf (_("\n%s strcache2: %s\n"), prefix, cache->name);

  /* Segment statistics. */

  for (seg = cache->seg_head; seg; seg = seg->next)
    {
      seg_count++;
      seg_total_bytes += seg->size;
      seg_avail_bytes += seg->avail;
      if (seg->size > seg_max_bytes)
        seg_max_bytes = seg->size;
    }
  seg_avg_bytes = seg_total_bytes / seg_count;
  printf (_("%s %u segments: total = %lu / max = %lu / avg = %lu / def = %u  avail = %lu\n"),
          prefix, seg_count, seg_total_bytes, seg_max_bytes, seg_avg_bytes,
          STRCACHE2_SEG_SIZE, seg_avail_bytes);

  /* String statistics. */

  idx = cache->hash_size;
  while (idx-- > 0)
    {
      struct strcache2_entry const *entry = cache->hash_tab[idx];
      if (entry && entry != &deleted_entry)
        {
          unsigned int length = entry->length;
          str_total_len += length;
          if (length > str_max_len)
            str_max_len = length;
          if (length < str_min_len)
            str_min_len = length;
        }
    }
  str_avg_len = cache->count ? str_total_len / cache->count : 0;
  printf (_("%s %u strings: total len = %lu / max = %ul / avg = %ul / min = %ul\n"),
          prefix, cache->count, str_total_len, str_max_len, str_avg_len, str_min_len);

  /* Hash statistics. */

  idx = STRCACHE2_INITIAL_SIZE;
  rehashes = 0;
  while (idx < cache->hash_size)
    {
      idx *= 2;
      rehashes++;
    }

  printf (_("%s hash size = %u  lookups / collisions = %lu / %lu (%u%%)  %u rehashes\n"),
          prefix, cache->hash_size, cache->lookup_count, cache->collision_count,
          (unsigned int)((float)cache->collision_count / cache->lookup_count),
          rehashes);

}

/* Print statistics for all string caches. */
void
strcache2_print_stats_all (const char *prefix)
{
  struct strcache2 *cur;
  for (cur = strcache_head; cur; cur++)
    strcache2_print_stats (cur, prefix);
}

