/* $Id: strcache2.c 1921 2008-10-24 00:30:58Z knut.osmundsen@oracle.com $ */
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

#ifdef _MSC_VER
typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;
typedef signed char    int8_t;
typedef signed short   int16_t;
typedef signed int     int32_t;
#else
# include <stdint.h>
#endif

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
#define STRCACHE2_SEG_SIZE              (1024U*1024U)
/* The default hash table shift (hash size give as a power of two). */
#define STRCACHE2_HASH_SHIFT            16
/** Does the modding / masking of a hash number into an index. */
#ifdef STRCACHE2_USE_MASK
# define STRCACHE2_MOD_IT(cache, hash)  ((hash) & (cache)->hash_mask)
#else
# define STRCACHE2_MOD_IT(cache, hash)  ((hash) % (cache)->hash_div)
#endif

# if defined(__amd64__) || defined(__x86_64__) || defined(__AMD64__) || defined(_M_X64) || defined(__amd64) \
 || defined(__i386__) || defined(__x86__) || defined(__X86__) || defined(_M_IX86) || defined(__i386)
#  define strcache2_get_unaligned_16bits(ptr)   ( *((const uint16_t *)(ptr)))
# else
   /* (endian doesn't matter) */
#  define strcache2_get_unaligned_16bits(ptr)   (   (((const uint8_t *)(ptr))[0] << 8) \
                                                  | (((const uint8_t *)(ptr))[1]) )
# endif


/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
/* List of initialized string caches. */
static struct strcache2 *strcache_head;


/** Finds the closest primary number for power of two value (or something else
 *  useful if not support).   */
MY_INLINE unsigned int strcache2_find_prime(unsigned int shift)
{
  switch (shift)
    {
      case  5:  return 31;
      case  6:  return 61;
      case  7:  return 127;
      case  8:  return 251;
      case  9:  return 509;
      case 10:  return 1021;
      case 11:  return 2039;
      case 12:  return 4093;
      case 13:  return 8191;
      case 14:  return 16381;
      case 15:  return 32749;
      case 16:  return 65521;
      case 17:  return 131063;
      case 18:  return 262139;
      case 19:  return 524269;
      case 20:  return 1048573;
      case 21:  return 2097143;
      case 22:  return 4194301;
      case 23:  return 8388593;

      default:
          assert (0);
          return (1 << shift) - 1;
    }
}

/* The following is a bit experiment. It produces longer chains, i.e. worse
   distribution of the strings in the table, however the actual make
   performances is better (<time).  The explanation is probably that the
   collisions only really increase for entries that aren't looked up that
   much and that it actually improoves the situation for those that is. Or
   that we spend so much less time hashing that it makes up (and more) for
   the pentalty we suffer from the longer chains and worse distribution.

   XXX: Check how this works out with different path lengths. I suspect it
        might depend on the length of PATH_ROOT and the depth of the files
        in the project as well. If it does, this might make matters worse
        for some and better for others which isn't very cool...  */

#if 0
# define BIG_HASH_SIZE  32 /* kinda fast */
# define BIG_HASH_HEAD  16
# define BIG_HASH_TAIL  12
#elif 0
# define BIG_HASH_SIZE  68 /* kinda safe */
# define BIG_HASH_HEAD  24
# define BIG_HASH_TAIL  24
#elif 0
# define BIG_HASH_SIZE 128 /* safe */
# define BIG_HASH_HEAD  32
# define BIG_HASH_TAIL  32
#endif

#ifdef BIG_HASH_SIZE
/* long string: hash head and tail, drop the middle. */
MY_INLINE unsigned int
strcache2_case_sensitive_hash_big (const char *str, unsigned int len)
{
  uint32_t hash = len;
  uint32_t tmp;
  unsigned int head;

  /* head BIG_HASH_HEAD bytes */
  head = (BIG_HASH_HEAD >> 2);
  while (head-- > 0)
    {
      hash += strcache2_get_unaligned_16bits (str);
      tmp   = (strcache2_get_unaligned_16bits (str + 2) << 11) ^ hash;
      hash  = (hash << 16) ^ tmp;
      str  += 2 * sizeof (uint16_t);
      hash += hash >> 11;
    }

  /* tail BIG_HASH_TAIL bytes (minus the odd ones) */
  str += (len - BIG_HASH_HEAD - BIG_HASH_TAIL) & ~3U;
  head = (BIG_HASH_TAIL >> 2);
  while (head-- > 0)
    {
      hash += strcache2_get_unaligned_16bits (str);
      tmp   = (strcache2_get_unaligned_16bits (str + 2) << 11) ^ hash;
      hash  = (hash << 16) ^ tmp;
      str  += 2 * sizeof (uint16_t);
      hash += hash >> 11;
    }

  /* force "avalanching" of final 127 bits. */
  hash ^= hash << 3;
  hash += hash >> 5;
  hash ^= hash << 4;
  hash += hash >> 17;
  hash ^= hash << 25;
  hash += hash >> 6;

  return hash;
}
#endif /* BIG_HASH_SIZE */

MY_INLINE unsigned int
strcache2_case_sensitive_hash (const char *str, unsigned int len)
{
#if 1
  /* Paul Hsieh hash SuperFast function:
     http://www.azillionmonkeys.com/qed/hash.html

     This performs very good and as a sligtly better distribution than
     STRING_N_HASH_1 on a typical kBuild run.

     It is also 37% faster than return_STRING_N_HASH_1 when running the
     two 100 times over typical kBuild strings that end up here (did a
     fprintf here and built kBuild). Compiler was 32-bit gcc 4.0.1, darwin,
     with -O2.

     FIXME: A path for well aligned data should be added to speed up
            execution on alignment sensitive systems.  */
  unsigned int rem;
  uint32_t hash;
  uint32_t tmp;

  assert (sizeof (uint8_t) == sizeof (char));

# ifdef BIG_HASH_SIZE
  /* long string? */
#  if 0 /*BIG_HASH_SIZE > 128*/
  if (MY_PREDICT_FALSE(len >= BIG_HASH_SIZE))
#  else
  if (len >= BIG_HASH_SIZE)
#  endif
    return strcache2_case_sensitive_hash_big (str, len);
# endif

  /* short string: main loop, walking on 2 x uint16_t */
  hash = len;
  rem = len & 3;
  len >>= 2;
  while (len > 0)
    {
      hash += strcache2_get_unaligned_16bits (str);
      tmp   = (strcache2_get_unaligned_16bits (str + 2) << 11) ^ hash;
      hash  = (hash << 16) ^ tmp;
      str  += 2 * sizeof (uint16_t);
      hash += hash >> 11;
      len--;
    }

  /* the remainder */
  switch (rem)
    {
      case 3:
        hash += strcache2_get_unaligned_16bits (str);
        hash ^= hash << 16;
        hash ^= str[sizeof (uint16_t)] << 18;
        hash += hash >> 11;
        break;
      case 2:
        hash += strcache2_get_unaligned_16bits (str);
        hash ^= hash << 11;
        hash += hash >> 17;
        break;
      case 1:
        hash += *str;
        hash ^= hash << 10;
        hash += hash >> 1;
        break;
    }

  /* force "avalanching" of final 127 bits. */
  hash ^= hash << 3;
  hash += hash >> 5;
  hash ^= hash << 4;
  hash += hash >> 17;
  hash ^= hash << 25;
  hash += hash >> 6;

  return hash;

#elif 1
  /* Note! This implementation is 18% faster than return_STRING_N_HASH_1
           when running the two 100 times over typical kBuild strings that
           end up here (did a fprintf here and built kBuild).
           Compiler was 32-bit gcc 4.0.1, darwin, with -O2. */

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

#elif 1
# if 0
  /* This is SDBM.  This specific form/unroll was benchmarked to be 28% faster
     than return_STRING_N_HASH_1.  (Now the weird thing is that putting the (ch)
     first in the assignment made it noticably slower.)

     However, it is noticably slower in practice, most likely because of more
     collisions.  Hrmpf.  */

#  define UPDATE_HASH(ch) hash = (hash << 6) + (hash << 16) - hash + (ch)
  unsigned int hash = 0;

# else
 /* This is DJB2.  This specific form/unroll was benchmarked to be 27%
    fast than return_STRING_N_HASH_1.

    Ditto.  */

#  define UPDATE_HASH(ch) hash = (hash << 5) + hash + (ch)
  unsigned int hash = 5381;
# endif


  while (len >= 4)
    {
      UPDATE_HASH (str[0]);
      UPDATE_HASH (str[1]);
      UPDATE_HASH (str[2]);
      UPDATE_HASH (str[3]);
      str += 4;
      len -= 4;
    }
  switch (len)
    {
      default:
      case 0:
        return hash;
      case 1:
        UPDATE_HASH (str[0]);
        return hash;
      case 2:
        UPDATE_HASH (str[0]);
        UPDATE_HASH (str[1]);
        return hash;
      case 3:
        UPDATE_HASH (str[0]);
        UPDATE_HASH (str[1]);
        UPDATE_HASH (str[2]);
        return hash;
    }
#endif
}

MY_INLINE unsigned int
strcache2_case_insensitive_hash (const char *str, unsigned int len)
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

MY_INLINE int
strcache2_memcmp_inline_short (const char *xs, const char *ys, unsigned int length)
{
  if (length <= 8)
    {
      /* short string compare - ~50% of the kBuild calls. */
      assert ( !((size_t)ys & 3) );
      if (!((size_t)xs & 3))
        {
          /* aligned */
          int result;
          switch (length)
            {
              default: /* memcmp for longer strings */
                  return memcmp (xs, ys, length);
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
    }

  /* memcmp for longer strings */
  return memcmp (xs, ys, length);
}

MY_INLINE int
strcache2_memcmp_inlined (const char *xs, const char *ys, unsigned int length)
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

MY_INLINE int
strcache2_is_equal (struct strcache2 *cache, struct strcache2_entry const *entry,
                    const char *str, unsigned int length, unsigned int hash)
{
  assert (!cache->case_insensitive);

  /* the simple stuff first. */
  if (   entry->hash != hash
      || entry->length != length)
      return 0;

#if 0
  return memcmp (str, entry + 1, length) == 0;
#elif 1
  return strcache2_memcmp_inlined (str, (const char *)(entry + 1), length) == 0;
#else
  return strcache2_memcmp_inline_short (str, (const char *)(entry + 1), length) == 0;
#endif
}

MY_INLINE int
strcache2_is_iequal (struct strcache2 *cache, struct strcache2_entry const *entry,
                     const char *str, unsigned int length, unsigned int hash)
{
  assert (cache->case_insensitive);

  /* the simple stuff first. */
  if (   entry->hash != hash
      || entry->length != length)
      return 0;

#if defined(_MSC_VER) || defined(__OS2__)
  return _memicmp (entry + 1, str, length) == 0;
#else
  return strncasecmp ((const char *)(entry + 1), str, length) == 0;
#endif
}

static void
strcache2_rehash (struct strcache2 *cache)
{
  unsigned int src = cache->hash_size;
  struct strcache2_entry **src_tab = cache->hash_tab;
  struct strcache2_entry **dst_tab;
#ifndef STRCACHE2_USE_MASK
  unsigned int hash_shift;
#endif

  /* Allocate a new hash table twice the size of the current. */
  cache->hash_size <<= 1;
#ifdef STRCACHE2_USE_MASK
  cache->hash_mask <<= 1;
  cache->hash_mask |= 1;
#else
  for (hash_shift = 1; (1U << hash_shift) < cache->hash_size; hash_shift++)
    /* nothing */;
  cache->hash_div = strcache2_find_prime (hash_shift);
#endif
  cache->rehash_count <<= 1;
  cache->hash_tab = dst_tab = (struct strcache2_entry **)
    xmalloc (cache->hash_size * sizeof (struct strcache2_entry *));
  memset (dst_tab, '\0', cache->hash_size * sizeof (struct strcache2_entry *));

  /* Copy the entries from the old to the new hash table. */
  cache->collision_count = 0;
  while (src-- > 0)
    {
      struct strcache2_entry *entry = src_tab[src];
      while (entry)
        {
          struct strcache2_entry *next = entry->next;
          unsigned int dst = STRCACHE2_MOD_IT (cache, entry->hash);
          if ((entry->next = dst_tab[dst]) != 0)
            cache->collision_count++;
          dst_tab[dst] = entry;

          entry = next;
        }
    }

  /* That's it, just free the old table and we're done. */
  free (src_tab);
}

static struct strcache2_seg *
strcache2_new_seg (struct strcache2 *cache, unsigned int minlen)
{
  struct strcache2_seg *seg;
  size_t size;
  size_t off;

  size = cache->def_seg_size;
  if (size < (size_t)minlen + sizeof (struct strcache2_seg) + STRCACHE2_ENTRY_ALIGNMENT)
    {
      size = (size_t)minlen * 2;
      size = (size + 0xfff) & ~(size_t)0xfff;
    }

  seg = xmalloc (size);
  seg->start = (char *)(seg + 1);
  seg->size  = size - sizeof (struct strcache2_seg);
  off = (size_t)seg->start & (STRCACHE2_ENTRY_ALIGNMENT - 1);
  if (off)
    {
      seg->start += off;
      seg->size  -= off;
    }
  assert (seg->size > minlen);
  seg->cursor = seg->start;
  seg->avail  = seg->size;

  seg->next = cache->seg_head;
  cache->seg_head = seg;

  return seg;
}

/* Internal worker that enters a new string into the cache. */
static const char *
strcache2_enter_string (struct strcache2 *cache, unsigned int idx,
                        const char *str, unsigned int length,
                        unsigned int hash)
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
  assert ((size_t)entry & (STRCACHE2_ENTRY_ALIGNMENT - 1));
  seg->cursor += size;
  seg->avail -= size;

  /* Setup the entry, copy the string and insert it into the hash table. */

  entry->user = NULL;
  entry->length = length;
  entry->hash = hash;
  str_copy = (char *) memcpy (entry + 1, str, length);
  str_copy[length] = '\0';

  if ((entry->next = cache->hash_tab[idx]) != 0)
    cache->collision_count++;
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
  unsigned int hash = strcache2_case_sensitive_hash (str, length);
  unsigned int idx;

  assert (!cache->case_insensitive);

  MAKE_STATS (cache->lookup_count++);

  /* Lookup the entry in the hash table, hoping for an
     early match.  If not found, enter the string at IDX. */
  idx = STRCACHE2_MOD_IT (cache, hash);
  entry = cache->hash_tab[idx];
  if (!entry)
    return strcache2_enter_string (cache, idx, str, length, hash);
  if (strcache2_is_equal (cache, entry, str, length, hash))
    return (const char *)(entry + 1);
  MAKE_STATS (cache->collision_1st_count++);

  entry = entry->next;
  if (!entry)
    return strcache2_enter_string (cache, idx, str, length, hash);
  if (strcache2_is_equal (cache, entry, str, length, hash))
    return (const char *)(entry + 1);
  MAKE_STATS (cache->collision_2nd_count++);

  /* Loop the rest.  */
  for (;;)
    {
      entry = entry->next;
      if (!entry)
        return strcache2_enter_string (cache, idx, str, length, hash);
      if (strcache2_is_equal (cache, entry, str, length, hash))
        return (const char *)(entry + 1);
      MAKE_STATS (cache->collision_3rd_count++);
    }
  /* not reached */
}

/* The public add string interface for prehashed strings.
   Use strcache2_hash_str to calculate the hash of a string. */
const char *
strcache2_add_hashed (struct strcache2 *cache, const char *str,
                      unsigned int length, unsigned int hash)
{
  struct strcache2_entry const *entry;
  unsigned int idx;
#ifndef NDEBUG
  unsigned correct_hash;

  assert (!cache->case_insensitive);
  correct_hash = strcache2_case_sensitive_hash (str, length);
  MY_ASSERT_MSG (hash == correct_hash, ("%#x != %#x\n", hash, correct_hash));
#endif /* NDEBUG */

  MAKE_STATS (cache->lookup_count++);

  /* Lookup the entry in the hash table, hoping for an
     early match.  If not found, enter the string at IDX. */
  idx = STRCACHE2_MOD_IT (cache, hash);
  entry = cache->hash_tab[idx];
  if (!entry)
    return strcache2_enter_string (cache, idx, str, length, hash);
  if (strcache2_is_equal (cache, entry, str, length, hash))
    return (const char *)(entry + 1);
  MAKE_STATS (cache->collision_1st_count++);

  entry = entry->next;
  if (!entry)
    return strcache2_enter_string (cache, idx, str, length, hash);
  if (strcache2_is_equal (cache, entry, str, length, hash))
    return (const char *)(entry + 1);
  MAKE_STATS (cache->collision_2nd_count++);

  /* Loop the rest.  */
  for (;;)
    {
      entry = entry->next;
      if (!entry)
        return strcache2_enter_string (cache, idx, str, length, hash);
      if (strcache2_is_equal (cache, entry, str, length, hash))
        return (const char *)(entry + 1);
      MAKE_STATS (cache->collision_3rd_count++);
    }
  /* not reached */
}

/* The public lookup (case sensitive) string interface. */
const char *
strcache2_lookup (struct strcache2 *cache, const char *str, unsigned int length)
{
  struct strcache2_entry const *entry;
  unsigned int hash = strcache2_case_sensitive_hash (str, length);
  unsigned int idx;

  assert (!cache->case_insensitive);

  MAKE_STATS (cache->lookup_count++);

  /* Lookup the entry in the hash table, hoping for an
     early match. */
  idx = STRCACHE2_MOD_IT (cache, hash);
  entry = cache->hash_tab[idx];
  if (!entry)
    return NULL;
  if (strcache2_is_equal (cache, entry, str, length, hash))
    return (const char *)(entry + 1);
  MAKE_STATS (cache->collision_1st_count++);

  entry = entry->next;
  if (!entry)
    return NULL;
  if (strcache2_is_equal (cache, entry, str, length, hash))
    return (const char *)(entry + 1);
  MAKE_STATS (cache->collision_2nd_count++);

  /* Loop the rest. */
  for (;;)
    {
      entry = entry->next;
      if (!entry)
        return NULL;
      if (strcache2_is_equal (cache, entry, str, length, hash))
        return (const char *)(entry + 1);
      MAKE_STATS (cache->collision_3rd_count++);
    }
  /* not reached */
}

#if defined(HAVE_CASE_INSENSITIVE_FS)

/* The public add string interface for case insensitive strings. */
const char *
strcache2_iadd (struct strcache2 *cache, const char *str, unsigned int length)
{
  struct strcache2_entry const *entry;
  unsigned int hash = strcache2_case_insensitive_hash (str, length);
  unsigned int idx;

  assert (!cache->case_insensitive);

  MAKE_STATS (cache->lookup_count++);

  /* Lookup the entry in the hash table, hoping for an
     early match.  If not found, enter the string at IDX. */
  idx = STRCACHE2_MOD_IT (cache, hash);
  entry = cache->hash_tab[idx];
  if (!entry)
    return strcache2_enter_string (cache, idx, str, length, hash);
  if (strcache2_is_equal (cache, entry, str, length, hash))
    return (const char *)(entry + 1);
  MAKE_STATS (cache->collision_1st_count++);

  entry = entry->next;
  if (!entry)
    return strcache2_enter_string (cache, idx, str, length, hash);
  if (strcache2_is_equal (cache, entry, str, length, hash))
    return (const char *)(entry + 1);
  MAKE_STATS (cache->collision_2nd_count++);

  /* Loop the rest. */
  for (;;)
    {
      entry = entry->next;
      if (!entry)
        return strcache2_enter_string (cache, idx, str, length, hash);
      if (strcache2_is_equal (cache, entry, str, length, hash))
        return (const char *)(entry + 1);
      MAKE_STATS (cache->collision_3rd_count++);
    }
  /* not reached */
}

/* The public add string interface for prehashed case insensitive strings.
   Use strcache2_hash_istr to calculate the hash of a string. */
const char *
strcache2_iadd_hashed (struct strcache2 *cache, const char *str,
                       unsigned int length, unsigned int hash)
{
  struct strcache2_entry const *entry;
  unsigned int idx;
#ifndef NDEBUG
  unsigned correct_hash;

  assert (!cache->case_insensitive);
  correct_hash = strcache2_case_insensitive_hash (str, length);
  MY_ASSERT_MSG (hash == correct_hash, ("%#x != %#x\n", hash, correct_hash));
#endif /* NDEBUG */

  MAKE_STATS (cache->lookup_count++);

  /* Lookup the entry in the hash table, hoping for an
     early match.  If not found, enter the string at IDX. */
  idx = STRCACHE2_MOD_IT (cache, hash);
  entry = cache->hash_tab[idx];
  if (!entry)
    return strcache2_enter_string (cache, idx, str, length, hash);
  if (strcache2_is_equal (cache, entry, str, length, hash))
    return (const char *)(entry + 1);
  MAKE_STATS (cache->collision_1st_count++);

  entry = entry->next;
  if (!entry)
    return strcache2_enter_string (cache, idx, str, length, hash);
  if (strcache2_is_equal (cache, entry, str, length, hash))
    return (const char *)(entry + 1);
  MAKE_STATS (cache->collision_2nd_count++);

  /* Loop the rest. */
  for (;;)
    {
      entry = entry->next;
      if (!entry)
        return strcache2_enter_string (cache, idx, str, length, hash);
      if (strcache2_is_equal (cache, entry, str, length, hash))
        return (const char *)(entry + 1);
      MAKE_STATS (cache->collision_3rd_count++);
    }
  /* not reached */
}

/* The public lookup (case insensitive) string interface. */
const char *
strcache2_ilookup (struct strcache2 *cache, const char *str, unsigned int length)
{
  struct strcache2_entry const *entry;
  unsigned int hash = strcache2_case_insensitive_hash (str, length);
  unsigned int idx;

  assert (!cache->case_insensitive);

  MAKE_STATS (cache->lookup_count++);

  /* Lookup the entry in the hash table, hoping for an
     early match. */
  idx = STRCACHE2_MOD_IT (cache, hash);
  entry = cache->hash_tab[idx];
  if (!entry)
    return NULL;
  if (strcache2_is_equal (cache, entry, str, length, hash))
    return (const char *)(entry + 1);
  MAKE_STATS (cache->collision_1st_count++);

  entry = entry->next;
  if (!entry)
    return NULL;
  if (strcache2_is_equal (cache, entry, str, length, hash))
    return (const char *)(entry + 1);
  MAKE_STATS (cache->collision_2nd_count++);

  /* Loop the rest. */
  for (;;)
    {
      entry = entry->next;
      if (!entry)
        return NULL;
      if (strcache2_is_equal (cache, entry, str, length, hash))
        return (const char *)(entry + 1);
      MAKE_STATS (cache->collision_3rd_count++);
    }
  /* not reached */
}

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
    ? strcache2_case_insensitive_hash (str, entry->length)
    : strcache2_case_sensitive_hash (str, entry->length);
  if (hash != entry->hash)
    {
      fprintf (stderr,
               "strcache2[%s]: corrupt entry %p, hash#1: %x, expected %x;\nstring: %s\n",
               cache->name, (void *)entry, hash, entry->hash, str);
      return -1;
    }

  return 0;
}


/* Calculates the case sensitive hash values of the string.
   The first hash is returned, the other is put at HASH2P. */
unsigned int strcache2_hash_str (const char *str, unsigned int length, unsigned int *hash2p)
{
  *hash2p = 1;
  return    strcache2_case_sensitive_hash (str, length);
}

/* Calculates the case insensitive hash values of the string.
   The first hash is returned, the other is put at HASH2P. */
unsigned int strcache2_hash_istr (const char *str, unsigned int length, unsigned int *hash2p)
{
  *hash2p = 1;
  return    strcache2_case_insensitive_hash (str, length);
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
#ifdef STRCACHE2_USE_MASK
  cache->hash_mask = (1U << hash_shift) - 1U;
#else
  cache->hash_div = strcache2_find_prime(hash_shift);
#endif
  cache->count = 0;
  cache->collision_count = 0;
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
  unsigned int  str_count = 0;
  unsigned long str_total_len = 0;
  unsigned int  str_avg_len;
  unsigned int  str_min_len = ~0U;
  unsigned int  str_max_len = 0;
  unsigned int  idx;
  unsigned int  rehashes;
  unsigned int  chain_depths[32];

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
  memset (chain_depths, '\0', sizeof (chain_depths));
  idx = cache->hash_size;
  while (idx-- > 0)
    {
      struct strcache2_entry const *entry = cache->hash_tab[idx];
      unsigned int depth = 0;
      for (; entry != 0; entry = entry->next, depth++)
        {
          unsigned int length = entry->length;
          str_total_len += length;
          if (length > str_max_len)
            str_max_len = length;
          if (length < str_min_len)
            str_min_len = length;
          str_count++;
        }
      chain_depths[depth >= 32 ? 31 : depth]++;
    }
  str_avg_len = cache->count ? str_total_len / cache->count : 0;
  printf (_("%s  %u strings: total len = %lu / max = %u / avg = %u / min = %u\n"),
          prefix, cache->count, str_total_len, str_max_len, str_avg_len, str_min_len);
  if (str_count != cache->count)
    printf (_("%s  string count mismatch! cache->count=%u, actual count is %u\n"), prefix,
            cache->count, str_count);

  /* Hash statistics. */
  idx = cache->init_size;
  rehashes = 0;
  while (idx < cache->hash_size)
    {
      idx *= 2;
      rehashes++;
    }

#ifdef STRCACHE2_USE_MASK
  printf (_("%s  hash size = %u  mask = %#x  rehashed %u times  lookups = %lu\n"),
          prefix, cache->hash_size, cache->hash_mask, rehashes, cache->lookup_count);
#else
  printf (_("%s  hash size = %u  div = %#x  rehashed %u times  lookups = %lu\n"),
          prefix, cache->hash_size, cache->hash_div, rehashes, cache->lookup_count);
#endif
  if (cache->lookup_count)
    printf (_("%s  hash collisions 1st = %lu (%u%%)  2nd = %lu (%u%%)  3rd = %lu (%u%%)\n"),
            prefix,
            cache->collision_1st_count,  (unsigned int)((100.0 * cache->collision_1st_count) / cache->lookup_count),
            cache->collision_2nd_count,  (unsigned int)((100.0 * cache->collision_2nd_count) / cache->lookup_count),
            cache->collision_3rd_count,  (unsigned int)((100.0 * cache->collision_3rd_count) / cache->lookup_count));
  printf (_("%s  hash insert collisions = %u (%u%%)\n"),
          prefix, cache->collision_count,(unsigned int)((100.0 * cache->collision_count) / cache->count));
  printf (_("%s  %5u (%u%%) empty hash table slots\n"),
          prefix, chain_depths[0],       (unsigned int)((100.0 * chain_depths[0])  / cache->hash_size));
  printf (_("%s  %5u (%u%%) occupied hash table slots\n"),
          prefix, chain_depths[1],       (unsigned int)((100.0 * chain_depths[1])  / cache->hash_size));
  for (idx = 2; idx < 32; idx++)
    {
      unsigned strs_at_this_depth = chain_depths[idx];
      unsigned i;
      for (i = idx + 1; i < 32; i++)
        strs_at_this_depth += chain_depths[i];
      if (strs_at_this_depth)
        printf (_("%s  %5u (%2u%%) with %u string%s chained on; %5u (%2u%%) strings at depth %u.\n"),
                prefix, chain_depths[idx], (unsigned int)((100.0 * chain_depths[idx]) / (cache->count - cache->collision_count)),
                idx - 1, idx == 2 ? " " : "s",
                strs_at_this_depth,        (unsigned int)((100.0 * strs_at_this_depth) / cache->count), idx - 1);
    }
}

/* Print statistics for all string caches. */
void
strcache2_print_stats_all (const char *prefix)
{
  struct strcache2 *cur;
  for (cur = strcache_head; cur; cur = cur->next)
    strcache2_print_stats (cur, prefix);
}

