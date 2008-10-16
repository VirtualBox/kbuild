/* $Id: strcache2.h 1870 2008-10-16 23:15:30Z knut.osmundsen@oracle.com $ */
/** @file
 * strcache - New string cache.
 */

/*
 * Copyright (c) 2006-2008 knut st. osmundsen <bird-src-spam@anduin.net>
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

#ifndef ___strcache2_h
#define ___strcache2_h

/* string cache memory segment. */
struct strcache2_seg
{
    struct strcache2_seg *next;         /* The next cache segment. */
    char *start;                        /* The first byte in the segment. */
    size_t size;                        /* The size of the segment. */
    size_t avail;                       /* The number of available bytes. */
    char *cursor;                       /* Allocation cursor. */
};

/* string cache hash table entry. */
struct strcache2_entry
{
    void *user;
    unsigned int hash1;
    unsigned int hash2;
    unsigned int length;
};

struct strcache2
{
    struct strcache2_entry **hash_tab;  /* The hash table. */
    int case_insensitive;               /* case insensitive or not. */
    unsigned int hash_mask;             /* The AND mask matching hash_size.*/
    unsigned long lookup_count;         /* The number of lookups. */
    unsigned long collision_1st_count;  /* The number of 1st level collisions. */
    unsigned long collision_2nd_count;  /* The number of 2nd level collisions. */
    unsigned long collision_3rd_count;  /* The number of 3rd level collisions. */
    unsigned int count;                 /* Number entries in the cache. */
    unsigned int rehash_count;          /* When to rehash the table. */
    unsigned int init_size;             /* The initial hash table size. */
    unsigned int hash_size;             /* The hash table size. */
    unsigned int def_seg_size;          /* The default segment size. */
    void *lock;                         /* The lock handle. */
    struct strcache2_seg *seg_head;     /* The memory segment list. */
    struct strcache2 *next;             /* The next string cache. */
    const char *name;                   /* Cache name. */
};


void strcache2_init (struct strcache2 *cache, const char *name, unsigned int size,
                     unsigned int def_seg_size, int case_insensitive, int thread_safe);
void strcache2_term (struct strcache2 *cache);
void strcache2_print_stats (struct strcache2 *cache, const char *prefix);
void strcache2_print_stats_all (const char *prefix);
const char *strcache2_add (struct strcache2 *cache, const char *str, unsigned int length);
const char *strcache2_add_hashed (struct strcache2 *cache, const char *str, unsigned int length,
                                  unsigned int hash1, unsigned int hash2);
int strcache2_is_cached (struct strcache2 *cache, const char *str);
int strcache2_verify_entry (struct strcache2 *cache, const char *str);
unsigned int strcache2_get_hash2_fallback (struct strcache2 *cache, const char *str);
unsigned int strcache2_hash_str (const char *str, unsigned int length, unsigned int *hash2p);
unsigned int strcache2_hash_istr (const char *str, unsigned int length, unsigned int *hash2p);

/* Get the hash table entry pointer. */
MY_INLINE struct strcache2_entry const *
strcache2_get_entry (struct strcache2 *cache, const char *str)
{
#ifndef NDEBUG
  strcache2_verify_entry (cache, str);
#endif
  return (struct strcache2_entry const *)str - 1;
}

/* Get the string length. */
MY_INLINE unsigned int
strcache2_get_len (struct strcache2 *cache, const char *str)
{
  return strcache2_get_entry (cache, str)->length;
}

/* Get the first hash value for the string. */
MY_INLINE unsigned int
strcache2_get_hash1 (struct strcache2 *cache, const char *str)
{
  return strcache2_get_entry (cache, str)->hash1;
}

/* Get the second hash value for the string. */
MY_INLINE unsigned int
strcache2_get_hash2 (struct strcache2 *cache, const char *str)
{
  unsigned int hash2 = strcache2_get_entry (cache, str)->hash2;
  if (!hash2)
    hash2 = strcache2_get_hash2_fallback (cache, str);
  return hash2;
}

/* Get the user value for the string. */
MY_INLINE void *
strcache2_get_user_val (struct strcache2 *cache, const char *str)
{
  return strcache2_get_entry (cache, str)->user;
}

/* Get the user value for the string. */
MY_INLINE void
strcache2_set_user_val (struct strcache2 *cache, const char *str, void *value)
{
  struct strcache2_entry *entry = (struct strcache2_entry *)str - 1;
#ifndef NDEBUG
  strcache2_verify_entry (cache, str);
#endif
  entry->user = value;
}

#endif

