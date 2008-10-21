/* $Id: strcache2.c 1897 2008-10-21 01:21:39Z knut.osmundsen@oracle.com $ */
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
#define STRCACHE2_SEG_SIZE          (1024U*1024U)
/* The default hash table shift (hash size give as a power of two). */
#define STRCACHE2_HASH_SHIFT        16


/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
/* List of initialized string caches. */
static struct strcache2 *strcache_head;



static struct strcache2_seg *
strcache2_new_seg (struct strcache2 *cache, unsigned int minlen)
{
  struct strcache2_seg *seg;
  size_t size;

  size = cache->def_seg_size;
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
      hash = 0;
      len--;
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
      hash = 0;
      len--;
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
    hash = 1;
  hash |= 1;
  return hash;
}

MY_INLINE unsigned int
strcache2_case_insensitive_hash_1 (const char *str, unsigned int len)
{
  unsigned int hash = 0;
  if (MY_PREDICT_TRUE(len >= 2))
    {
      unsigned int ch0 = *str++;
      ch0 = tolower (ch0);
      hash = 0;
      len--;
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
      hash = 0;
      len--;
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
    hash = 1;
  hash |= 1;
  return hash;
}

#if 0 /* FIXME: Use this (salvaged from variable.c) */

MY_INLINE int
variable_hash_cmp_2_memcmp (const char *xs, const char *ys, unsigned int length)
{
  /* short string compare - ~50% of the kBuild calls. */
  assert ( !((size_t)ys & 3) );
  if (!((size_t)xs & 3))
    {
      /* aligned */
      int result;
      switch (length)
        {
          case 8:
              result  = *(int32_t*)(xs + 4) - *(int32_t*)(ys + 4);
              result |= *(int32_t*)xs - *(int32_t*)ys;
              return result;
          case 7:
              result  = xs[6] - ys[6];
              result |= xs[5] - ys[5];
              result |= xs[4] - ys[4];
              result |= *(int32_t*)xs - *(int32_t*)ys;
              return result;
          case 6:
              result  = xs[5] - ys[5];
              result |= xs[4] - ys[4];
              result |= *(int32_t*)xs - *(int32_t*)ys;
              return result;
          case 5:
              result  = xs[4] - ys[4];
              result |= *(int32_t*)xs - *(int32_t*)ys;
              return result;
          case 4:
              return *(int32_t*)xs - *(int32_t*)ys;
          case 3:
              result  = xs[2] - ys[2];
              result |= xs[1] - ys[1];
              result |= xs[0] - ys[0];
              return result;
          case 2:
              result  = xs[1] - ys[1];
              result |= xs[0] - ys[0];
              return result;
          case 1:
              return *xs - *ys;
          case 0:
              return 0;
        }
    }
  else
    {
      /* unaligned */
      int result = 0;
      switch (length)
        {
          case 8: result |= xs[7] - ys[7];
          case 7: result |= xs[6] - ys[6];
          case 6: result |= xs[5] - ys[5];
          case 5: result |= xs[4] - ys[4];
          case 4: result |= xs[3] - ys[3];
          case 3: result |= xs[2] - ys[2];
          case 2: result |= xs[1] - ys[1];
          case 1: result |= xs[0] - ys[0];
          case 0:
              return result;
        }
    }

  /* memcmp for longer strings */
# ifdef __GNUC__
  return __builtin_memcmp (xs, ys, length);
# else
  return memcmp (xs, ys, length);
# endif
}

MY_INLINE int
variable_hash_cmp_2_inlined (const char *xs, const char *ys, unsigned int length)
{
#ifndef ELECTRIC_HEAP
  assert ( !((size_t)ys & 3) );
#endif
  if (!((size_t)xs & 3))
    {
      int result;
      /* aligned */
      while (length >= 8)
        {
          result  = *(int32_t*)xs - *(int32_t*)ys;
          result |= *(int32_t*)(xs + 4) - *(int32_t*)(ys + 4);
          if (MY_PREDICT_FALSE(result))
            return result;
          xs += 8;
          ys += 8;
          length -= 8;
        }
      switch (length)
        {
          case 7:
              result  = *(int32_t*)xs - *(int32_t*)ys;
              result |= xs[6] - ys[6];
              result |= xs[5] - ys[5];
              result |= xs[4] - ys[4];
              return result;
          case 6:
              result  = *(int32_t*)xs - *(int32_t*)ys;
              result |= xs[5] - ys[5];
              result |= xs[4] - ys[4];
              return result;
          case 5:
              result  = *(int32_t*)xs - *(int32_t*)ys;
              result |= xs[4] - ys[4];
              return result;
          case 4:
              return *(int32_t*)xs - *(int32_t*)ys;
          case 3:
              result  = xs[2] - ys[2];
              result |= xs[1] - ys[1];
              result |= xs[0] - ys[0];
              return result;
          case 2:
              result  = xs[1] - ys[1];
              result |= xs[0] - ys[0];
              return result;
          case 1:
              return *xs - *ys;
          default:
          case 0:
              return 0;
        }
    }
  else
    {
      /* unaligned */
      int result;
      while (length >= 8)
        {
#if defined(__i386__) || defined(__x86_64__)
          result  = (  ((int32_t)xs[3] << 24)
                     | ((int32_t)xs[2] << 16)
                     | ((int32_t)xs[1] <<  8)
                     |           xs[0]       )
                  - *(int32_t*)ys;
          result |= (  ((int32_t)xs[7] << 24)
                     | ((int32_t)xs[6] << 16)
                     | ((int32_t)xs[5] <<  8)
                     |           xs[4]       )
                  - *(int32_t*)(ys + 4);
#else
          result  = xs[3] - ys[3];
          result |= xs[2] - ys[2];
          result |= xs[1] - ys[1];
          result |= xs[0] - ys[0];
          result |= xs[7] - ys[7];
          result |= xs[6] - ys[6];
          result |= xs[5] - ys[5];
          result |= xs[4] - ys[4];
#endif
          if (MY_PREDICT_FALSE(result))
            return result;
          xs += 8;
          ys += 8;
          length -= 8;
        }
      result = 0;
      switch (length)
        {
          case 7: result |= xs[6] - ys[6];
          case 6: result |= xs[5] - ys[5];
          case 5: result |= xs[4] - ys[4];
          case 4: result |= xs[3] - ys[3];
          case 3: result |= xs[2] - ys[2];
          case 2: result |= xs[1] - ys[1];
          case 1: result |= xs[0] - ys[0];
              return result;
          default:
          case 0:
              return 0;
        }
    }
}

#endif

MY_INLINE int
strcache2_is_equal (struct strcache2 *cache, struct strcache2_entry const *entry,
                    const char *str, unsigned int length, unsigned int hash1)
{
  /* the simple stuff first. */
  if (   entry == NULL
      || entry->hash1 != hash1
      || entry->length != length)
      return 0;

  return !cache->case_insensitive
    ? memcmp (entry + 1, str, length) == 0
#if defined(_MSC_VER) || defined(__OS2__)
    : _memicmp (entry + 1, str, length) == 0
#else
    : strncasecmp ((const char *)(entry + 1), str, length) == 0
#endif
    ;
}

static void
strcache2_rehash (struct strcache2 *cache)
{
  unsigned int src = cache->hash_size;
  struct strcache2_entry **src_tab = cache->hash_tab;
  struct strcache2_entry **dst_tab;
  unsigned int hash_mask;

  /* Allocate a new hash table twice the size of the current. */
  cache->hash_size <<= 1;
  cache->hash_mask <<= 1;
  cache->hash_mask |= 1;
  cache->rehash_count <<= 1;
  cache->hash_tab = dst_tab = (struct strcache2_entry **)
    xmalloc (cache->hash_size * sizeof (struct strcache2_entry *));
  memset (dst_tab, '\0', cache->hash_size * sizeof (struct strcache2_entry *));

  /* Copy the entries from the old to the new hash table. */
  hash_mask = cache->hash_mask;
  while (src-- > 0)
    {
      struct strcache2_entry *entry = src_tab[src];
      if (entry)
        {
          unsigned int dst = entry->hash1 & hash_mask;
          if (dst_tab[dst])
            {
              unsigned int hash2 = entry->hash2;
              if (!hash2)
                entry->hash2 = hash2 = cache->case_insensitive
                  ? strcache2_case_insensitive_hash_2 ((const char *)(entry + 1), entry->length)
                  : strcache2_case_sensitive_hash_2 ((const char *)(entry + 1), entry->length);
              dst += hash2;
              dst &= hash_mask;
              while (dst_tab[dst])
                {
                  dst += hash2;
                  dst &= hash_mask;
                }
            }

          dst_tab[dst] = entry;
        }
    }

  /* That's it, just free the old table and we're done. */
  free (src_tab);
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
  size = (size + STRCACHE2_ENTRY_ALIGNMENT - 1) & ~(STRCACHE2_ENTRY_ALIGNMENT - 1U);

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

/* The public add string interface. */
const char *
strcache2_add (struct strcache2 *cache, const char *str, unsigned int length)
{
  struct strcache2_entry const *entry;
  unsigned int hash2;
  unsigned int hash1 = strcache2_case_sensitive_hash_1 (str, length);
  unsigned int idx;

  assert (!cache->case_insensitive);

  cache->lookup_count++;

  /* Lookup the entry in the hash table, hoping for an
     early match. */
  idx = hash1 & cache->hash_mask;
  entry = cache->hash_tab[idx];
  if (strcache2_is_equal (cache, entry, str, length, hash1))
    return (const char *)(entry + 1);
  if (!entry)
    hash2 = 0;
  else
    {
      cache->collision_1st_count++;

      hash2 = strcache2_case_sensitive_hash_2 (str, length);
      idx += hash2;
      idx &= cache->hash_mask;
      entry = cache->hash_tab[idx];
      if (strcache2_is_equal (cache, entry, str, length, hash1))
        return (const char *)(entry + 1);

      if (entry)
        {
          cache->collision_2nd_count++;
          do
            {
              idx += hash2;
              idx &= cache->hash_mask;
              entry = cache->hash_tab[idx];
              cache->collision_3rd_count++;
              if (strcache2_is_equal (cache, entry, str, length, hash1))
                return (const char *)(entry + 1);
            }
          while (entry);
        }
    }

  /* Not found, add it at IDX. */
  return strcache2_enter_string (cache, idx, str, length, hash1, hash2);
}

/* The public add string interface for prehashed strings.
   Use strcache2_hash_str to calculate the hash of a string. */
const char *
strcache2_add_hashed (struct strcache2 *cache, const char *str, unsigned int length,
                      unsigned int hash1, unsigned int hash2)
{
  struct strcache2_entry const *entry;
  unsigned int idx;
#ifndef NDEBUG
  unsigned correct_hash;

  correct_hash = cache->case_insensitive
               ? strcache2_case_insensitive_hash_1 (str, length)
               : strcache2_case_sensitive_hash_1 (str, length);
  MY_ASSERT_MSG (hash1 == correct_hash, ("%#x != %#x\n", hash1, correct_hash));
  if (hash2)
    {
      correct_hash = cache->case_insensitive
                   ? strcache2_case_insensitive_hash_2 (str, length)
                   : strcache2_case_sensitive_hash_2 (str, length);
      MY_ASSERT_MSG (hash2 == correct_hash, ("%#x != %#x\n", hash2, correct_hash));
    }
#endif /* NDEBUG */

  cache->lookup_count++;

  /* Lookup the entry in the hash table, hoping for an
     early match. */
  idx = hash1 & cache->hash_mask;
  entry = cache->hash_tab[idx];
  if (strcache2_is_equal (cache, entry, str, length, hash1))
    return (const char *)(entry + 1);
  if (entry)
    {
      cache->collision_1st_count++;

      if (!hash2)
        hash2 = cache->case_insensitive
          ? strcache2_case_insensitive_hash_2 (str, length)
          : strcache2_case_sensitive_hash_2 (str, length);
      idx += hash2;
      idx &= cache->hash_mask;
      entry = cache->hash_tab[idx];
      if (strcache2_is_equal (cache, entry, str, length, hash1))
        return (const char *)(entry + 1);

      if (entry)
        {
          cache->collision_2nd_count++;
          do
            {
              idx += hash2;
              idx &= cache->hash_mask;
              entry = cache->hash_tab[idx];
              cache->collision_3rd_count++;
              if (strcache2_is_equal (cache, entry, str, length, hash1))
                return (const char *)(entry + 1);
            }
          while (entry);
        }
    }

  /* Not found, add it at IDX. */
  return strcache2_enter_string (cache, idx, str, length, hash1, hash2);
}

/* The public lookup string interface. */
const char *
strcache2_lookup (struct strcache2 *cache, const char *str, unsigned int length)
{
  struct strcache2_entry const *entry;
  unsigned int hash2;
  unsigned int hash1 = strcache2_case_sensitive_hash_1 (str, length);
  unsigned int idx;

  assert (!cache->case_insensitive);

  cache->lookup_count++;

  /* Lookup the entry in the hash table, hoping for an
     early match. */
  idx = hash1 & cache->hash_mask;
  entry = cache->hash_tab[idx];
  if (strcache2_is_equal (cache, entry, str, length, hash1))
    return (const char *)(entry + 1);
  if (!entry)
    hash2 = 0;
  else
    {
      cache->collision_1st_count++;

      hash2 = strcache2_case_sensitive_hash_2 (str, length);
      idx += hash2;
      idx &= cache->hash_mask;
      entry = cache->hash_tab[idx];
      if (strcache2_is_equal (cache, entry, str, length, hash1))
        return (const char *)(entry + 1);

      if (entry)
        {
          cache->collision_2nd_count++;
          do
            {
              idx += hash2;
              idx &= cache->hash_mask;
              entry = cache->hash_tab[idx];
              cache->collision_3rd_count++;
              if (strcache2_is_equal (cache, entry, str, length, hash1))
                return (const char *)(entry + 1);
            }
          while (entry);
        }
    }

  /* Not found. */
  return NULL;
}

#if defined(HAVE_CASE_INSENSITIVE_FS)

/* The public add string interface for case insensitive strings. */
const char *
strcache2_iadd (struct strcache2 *cache, const char *str, unsigned int length)
{
  struct strcache2_entry const *entry;
  unsigned int hash2;
  unsigned int hash1 = strcache2_case_insensitive_hash_1 (str, length);
  unsigned int idx;

  assert (cache->case_insensitive);
  cache->lookup_count++;

  /* Lookup the entry in the hash table, hoping for an
     early match. */
  idx = hash1 & cache->hash_mask;
  entry = cache->hash_tab[idx];
  if (strcache2_is_equal (cache, entry, str, length, hash1))
    return (const char *)(entry + 1);
  if (!entry)
    hash2 = 0;
  else
    {
      cache->collision_1st_count++;

      hash2 = strcache2_case_insensitive_hash_2 (str, length);
      idx += hash2;
      idx &= cache->hash_mask;
      entry = cache->hash_tab[idx];
      if (strcache2_is_equal (cache, entry, str, length, hash1))
        return (const char *)(entry + 1);

      if (entry)
        {
          cache->collision_2nd_count++;
          do
            {
              idx += hash2;
              idx &= cache->hash_mask;
              entry = cache->hash_tab[idx];
              cache->collision_3rd_count++;
              if (strcache2_is_equal (cache, entry, str, length, hash1))
                return (const char *)(entry + 1);
            }
          while (entry);
        }
    }

  /* Not found, add it at IDX. */
  return strcache2_enter_string (cache, idx, str, length, hash1, hash2);
}

/* strcache_ilookup later */

#endif /* HAVE_CASE_INSENSITIVE_FS */

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
      for (seg = cache->seg_head; seg; seg = seg->next)
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
      if (hash != entry->hash2)
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

/* Calculates the case sensitive hash values of the string.
   The first hash is returned, the other is put at HASH2P. */
unsigned int strcache2_hash_str (const char *str, unsigned int length, unsigned int *hash2p)
{
  *hash2p = strcache2_case_sensitive_hash_2 (str, length);
  return    strcache2_case_sensitive_hash_1 (str, length);
}

/* Calculates the case insensitive hash values of the string.
   The first hash is returned, the other is put at HASH2P. */
unsigned int strcache2_hash_istr (const char *str, unsigned int length, unsigned int *hash2p)
{
  *hash2p = strcache2_case_insensitive_hash_2 (str, length);
  return    strcache2_case_insensitive_hash_1 (str, length);
}



/* Initalizes a new cache. */
void
strcache2_init (struct strcache2 *cache, const char *name, unsigned int size,
                unsigned int def_seg_size, int case_insensitive, int thread_safe)
{
  unsigned hash_shift;
  assert (!thread_safe);

  /* calc the size as a power of two */
  if (!size)
    hash_shift = STRCACHE2_HASH_SHIFT;
  else
    {
      assert (size <= (~0U / 2 + 1));
      for (hash_shift = 8; (1U << hash_shift) < size; hash_shift++)
        /* nothing */;
    }

  /* adjust the default segment size */
  if (!def_seg_size)
    def_seg_size = STRCACHE2_SEG_SIZE;
  else if (def_seg_size < sizeof (struct strcache2_seg) * 10)
    def_seg_size = sizeof (struct strcache2_seg) * 10;
  else if ((def_seg_size & 0xfff) < 0xf00)
    def_seg_size = ((def_seg_size + 0xfff) & ~0xfffU);


  /* init the structure. */
  cache->case_insensitive = case_insensitive;
  cache->hash_mask = (1U << hash_shift) - 1U;
  cache->count = 0;
  cache->lookup_count = 0;
  cache->collision_1st_count = 0;
  cache->collision_2nd_count = 0;
  cache->collision_3rd_count = 0;
  cache->rehash_count = (1U << hash_shift) / 4 * 3;   /* rehash at 75% */
  cache->init_size = 1U << hash_shift;
  cache->hash_size = 1U << hash_shift;
  cache->def_seg_size = def_seg_size;
  cache->lock = NULL;
  cache->name = name;

  /* allocate the hash table and first segment. */
  cache->hash_tab = (struct strcache2_entry **)
    xmalloc (cache->init_size * sizeof (struct strcache2_entry *));
  memset (cache->hash_tab, '\0', cache->init_size * sizeof (struct strcache2_entry *));
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

/* Print statistics a string cache. */
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
  printf (_("%s  %u segments: total = %lu / max = %lu / avg = %lu / def = %u  avail = %lu\n"),
          prefix, seg_count, seg_total_bytes, seg_max_bytes, seg_avg_bytes,
          cache->def_seg_size, seg_avail_bytes);

  /* String statistics. */
  idx = cache->hash_size;
  while (idx-- > 0)
    {
      struct strcache2_entry const *entry = cache->hash_tab[idx];
      if (entry)
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
  printf (_("%s  %u strings: total len = %lu / max = %u / avg = %u / min = %u\n"),
          prefix, cache->count, str_total_len, str_max_len, str_avg_len, str_min_len);

  /* Hash statistics. */
  idx = cache->init_size;
  rehashes = 0;
  while (idx < cache->hash_size)
    {
      idx *= 2;
      rehashes++;
    }

  printf (_("%s  hash size = %u  mask = %#x  rehashed %u times  lookups = %lu\n"),
          prefix, cache->hash_size, cache->hash_mask, rehashes, cache->lookup_count);
  printf (_("%s  hash collisions 1st = %lu (%u%%)  2nd = %lu (%u%%)  3rd = %lu (%u%%)\n"),
          prefix,
          cache->collision_1st_count, (unsigned int)((float)cache->collision_1st_count * 100 / cache->lookup_count),
          cache->collision_2nd_count, (unsigned int)((float)cache->collision_2nd_count * 100 / cache->lookup_count),
          cache->collision_3rd_count, (unsigned int)((float)cache->collision_3rd_count * 100 / cache->lookup_count));
}

/* Print statistics for all string caches. */
void
strcache2_print_stats_all (const char *prefix)
{
  struct strcache2 *cur;
  for (cur = strcache_head; cur; cur = cur->next)
    strcache2_print_stats (cur, prefix);
}

