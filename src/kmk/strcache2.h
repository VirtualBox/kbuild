/* $Id: strcache2.h 1914 2008-10-22 21:25:49Z knut.osmundsen@oracle.com $ */
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

#ifndef CHAR_BIT
# error "include after make.h!"
#endif

#define STRCACHE2_USE_MASK 1

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
    struct strcache2_entry *next;       /* Collision chain. */
    void *user;
    unsigned int hash;
    unsigned int length;
};

/* The entry alignment, cacheline size if it's known & sensible.

   On x86/AMD64 we assume a 64-byte cacheline size.  As it is difficult to
   guess other right now, these default 16 chars as that shouldn't cause
   much trouble, even if it not the most optimial value.  Override, or modify
   for other platforms.  */
#ifndef STRCACHE2_ENTRY_ALIGN_SHIFT
# if defined (__i386__) || defined(__x86_64__)
#  define STRCACHE2_ENTRY_ALIGN_SHIFT    6
# else
#  define STRCACHE2_ENTRY_ALIGN_SHIFT    4
# endif
#endif
#define STRCACHE2_ENTRY_ALIGNMENT       (1 << STRCACHE2_ENTRY_ALIGN_SHIFT)


struct strcache2
{
    struct strcache2_entry **hash_tab;  /* The hash table. */
    int case_insensitive;               /* case insensitive or not. */
#ifdef STRCACHE2_USE_MASK
    unsigned int hash_mask;             /* The AND mask matching hash_size.*/
#else
    unsigned int hash_div;              /* The number (prime) to mod by. */
#endif
    unsigned long lookup_count;         /* The number of lookups. */
    unsigned long collision_1st_count;  /* The number of 1st level collisions. */
    unsigned long collision_2nd_count;  /* The number of 2nd level collisions. */
    unsigned long collision_3rd_count;  /* The number of 3rd level collisions. */
    unsigned int count;                 /* Number entries in the cache. */
    unsigned int collision_count;       /* Number of entries in chains. */
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
const char *strcache2_iadd (struct strcache2 *cache, const char *str, unsigned int length);
const char *strcache2_add_hashed (struct strcache2 *cache, const char *str,
                                  unsigned int length, unsigned int hash);
const char *strcache2_iadd_hashed (struct strcache2 *cache, const char *str,
                                   unsigned int length, unsigned int hash);
const char *strcache2_lookup (struct strcache2 *cache, const char *str, unsigned int length);
const char *strcache2_ilookup (struct strcache2 *cache, const char *str, unsigned int length);
#ifdef HAVE_CASE_INSENSITIVE_FS
# define strcache2_add_file         strcache2_iadd
# define strcache2_add_hashed_file  strcache2_iadd_hashed
# define strcache2_lookup_file      strcache2_ilookup
#else
# define strcache2_add_file         strcache2_add
# define strcache2_add_hashed_file  strcache2_add_hashed
# define strcache2_lookup_file      strcache2_lookup
#endif
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
strcache2_get_hash (struct strcache2 *cache, const char *str)
{
  return strcache2_get_entry (cache, str)->hash;
}

/* Calc the pointer hash value for the string.

   This takes the string address, shift out the bits that are always zero
   due to alignment, and then returns the unsigned integer value of it.

   The results from using this is generally better than for any of the
   other hash values.  It is also sligtly faster code as it does not
   involve any memory accesses, just a right SHIFT and an optional AND. */
MY_INLINE unsigned int
strcache2_calc_ptr_hash (struct strcache2 *cache, const char *str)
{
  (void)cache;
  return (size_t)str >> STRCACHE2_ENTRY_ALIGN_SHIFT;
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

