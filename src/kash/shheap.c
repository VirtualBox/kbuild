/* $Id: shheap.c 3451 2020-09-13 11:21:43Z knut.osmundsen@oracle.com $ */
/** @file
 * The shell memory heap methods.
 */

/*
 * Copyright (c) 2009-2010 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
 *
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include "shheap.h"
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "shinstance.h"

#if K_OS == K_OS_WINDOWS && defined(SH_FORKED_MODE)
# define SHHEAP_IN_USE
#endif

#ifdef SHHEAP_IN_USE
# if K_OS == K_OS_WINDOWS
#  include <Windows.h>
# else
#  include <unistd.h>
# endif
#endif


/*******************************************************************************
*   Structures and Typedefs                                                    *
*******************************************************************************/
#ifdef SHHEAP_IN_USE
/**
 * heap memory block header.
 */
typedef struct shmemhdr
{
    size_t              magic;          /**< Magic value */
    size_t              size;           /**< The block size */
    struct shmemhdr    *next;           /**< Forward pointer. */
    struct shmemhdr    *prev;           /**< Backward pointer. */
    struct shmemhdr    *next2;          /**< Free/Shell list forward. */
    struct shmemhdr    *prev2;          /**< Free/Shell list backward. */
    struct shinstance  *psh;            /**< The shell who allocated it. */
    struct shmemchunk  *chunk;          /**< The chunk who owns this. */
} shmemhdr;

/** Free block magic (shmemhdr::magic) */
#define SHMEMHDR_MAGIC_FREE     0xbeeff00d
/** Used block magic (shmemhdr::magic) */
#define SHMEMHDR_MAGIC_USED     0xfeedface

typedef struct shmemchunk
{
    struct shmemhdr    *head;           /**< Head of the block list. */
    struct shmemhdr    *free_head;      /**< Head of the free list. */
    struct shmemchunk  *next;           /**< The next block. */
    struct shmemchunk  *prev;           /**< The previous block. */
    size_t              size;           /**< Chunk size. */
    size_t              magic;          /**< Magic value. */
    size_t              padding0;
    size_t              padding1;
} shmemchunk;

/** shmemchunk::magic */
#define SHMEMCHUNK_MAGIC        0x12345678

#endif /* K_OS_WINDOWS */


/*******************************************************************************
*   Defined Constants And Macros                                               *
*******************************************************************************/
#define SHHEAP_ALIGN(sz)        (((sz) + 31) & ~(size_t)31)
#define SHHEAP_CHUNK_ALIGN(sz)  (((sz) + 0xffff) & ~(size_t)0xffff)
#define SHHEAP_MIN_CHUNK        0x80000 //(1024*1024)
#ifdef NDEBUG
# define SHHEAP_CHECK()         do { } while (0)
# define SHHEAP_CHECK_2()       do { } while (0)
# define SHHEAP_ASSERT(expr)    do { } while (0)
# define SHHEAP_POISON_PSH(p,v) (p)
# define SHHEAP_POISON_NULL(v)  NULL
#else
# define SHHEAP_CHECK()         shheap_check()
# define SHHEAP_CHECK_2()       shheap_check()
# define SHHEAP_ASSERT(expr)    assert(expr)
# define SHHEAP_POISON_PSH(p,v) ((shinstance *)(v))
# define SHHEAP_POISON_NULL(v)  ((void *)(v))
#endif


/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
#ifdef SHHEAP_IN_USE
/** The heap lock. */
static shmtx        g_sh_heap_mtx;
/** The heap.
 * This is a list of chunks. */
static shmemchunk  *g_sh_heap;
#endif


int shheap_init(void *phead)
{
    int rc;
#ifdef SHHEAP_IN_USE
    SHHEAP_ASSERT(SHHEAP_ALIGN(sizeof(shmemhdr)) == sizeof(shmemhdr));
    rc = shmtx_init(&g_sh_heap_mtx);
    g_sh_heap = (shmemchunk *)phead; /* non-zero on fork() */
#else
    rc = 0;
#endif
    return rc;
}

#ifdef SHHEAP_IN_USE

# if K_OS == K_OS_WINDOWS

/**
 * Get the head so the child can pass it to shheap_init() after fork().
 *
 * @returns g_sh_heap.
 */
void *shheap_get_head(void)
{
    return g_sh_heap;
}

/**
 * Copies the heap into the child process.
 *
 * @returns 0 on success, -1 and errno on failure.
 * @param   hChild      Handle to the child process.
 */
int shheap_fork_copy_to_child(void *hChild)
{
    shmemchunk *chunk;
    shmtxtmp tmp;
    int err = 0;

    shmtx_enter(&g_sh_heap_mtx, &tmp);

    for (chunk = g_sh_heap; chunk; chunk = chunk->next)
    {
        void *chld_chnk;

        chld_chnk = (shmemchunk *)VirtualAllocEx(hChild, chunk, chunk->size,
                                                 MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (chld_chnk != chunk)
        {
            err = GetLastError();
            fprintf(stderr, "shfork: VirtualAllocEx(,%p,%p,) -> %p/%d\n", chunk, chunk->size, chld_chnk, err);
            break;
        }

        if (!WriteProcessMemory(hChild, chunk, chunk, chunk->size, NULL /* pNumberOfBytesWritten */))
        {
            err = GetLastError();
            fprintf(stderr, "shfork: WriteProcessMemory(,%p,,%p,) -> %d\n", chunk, chunk->size, err);
            break;
        }
    }

    shmtx_leave(&g_sh_heap_mtx, &tmp);

    if (!err)
        return 0;
    errno = EINVAL;
    return -1;
}

# endif /* K_OS == K_OS_WINDOWS */

/**
 * Checks a heap chunk.
 * @param   chunk       The chunk to check.
 */
static void shheap_check_chunk(shmemchunk *chunk)
{
    size_t              free_count;
    struct shmemhdr    *mem;
    struct shmemhdr    *prev;

    SHHEAP_ASSERT(chunk->magic == SHMEMCHUNK_MAGIC);
    SHHEAP_ASSERT(chunk->head);
    SHHEAP_ASSERT(chunk->size == SHHEAP_CHUNK_ALIGN(chunk->size));

    free_count = 0;
    prev = NULL;
    for (mem = chunk->head; mem; mem = mem->next)
    {
        size_t size = (mem->next ? (char *)mem->next : (char *)chunk + chunk->size) - (char *)(mem + 1);
        SHHEAP_ASSERT(mem->size == size);
        SHHEAP_ASSERT(mem->prev == prev);
        if (mem->magic == SHMEMHDR_MAGIC_FREE)
            free_count++;
        else
            SHHEAP_ASSERT(mem->magic == SHMEMHDR_MAGIC_USED);
        prev = mem;
    }

    prev = NULL;
    for (mem = chunk->free_head; mem; mem = mem->next2)
    {
        size_t size = (mem->next ? (char *)mem->next : (char *)chunk + chunk->size) - (char *)(mem + 1);
        SHHEAP_ASSERT(mem->size == size);
        SHHEAP_ASSERT(mem->prev2 == prev);
        SHHEAP_ASSERT(mem->magic == SHMEMHDR_MAGIC_FREE);
        free_count--;
        prev = mem;
    }
    SHHEAP_ASSERT(free_count == 0);
}

/**
 * Checks the heap.
 */
static void shheap_check(void)
{
    shmemchunk *chunk;
    for (chunk = g_sh_heap; chunk; chunk = chunk->next)
        shheap_check_chunk(chunk);
}

/**
 * Grows the heap with another chunk carving out a block
 *
 * @returns Pointer to a used entry of size @a size1. NULL
 *          if we're out of memory
 * @param   size1       The size of the block to be returned (aligned).
 */
static shmemhdr *shheap_grow(size_t size1)
{
    shmemchunk *chunk;
    shmemhdr *used;
    shmemhdr *avail;
    size_t chunk_size;

    /* Calc the chunk size and allocate it. */
    chunk_size = SHHEAP_ALIGN(size1) + SHHEAP_ALIGN(sizeof(*chunk)) + SHHEAP_ALIGN(sizeof(*used)) * 10;
    if (chunk_size < SHHEAP_MIN_CHUNK)
        chunk_size = SHHEAP_MIN_CHUNK;
    else
        chunk_size = SHHEAP_CHUNK_ALIGN(chunk_size);

# if K_OS == K_OS_WINDOWS
    chunk = (shmemchunk *)VirtualAlloc(NULL, chunk_size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
# else
    chunk = NULL;
# endif

    if (!chunk)
        return NULL;

    used = (shmemhdr *)((char *)chunk + SHHEAP_ALIGN(sizeof(*chunk)));
    avail = (shmemhdr *)((char *)(used + 1) + size1);

    used->magic = SHMEMHDR_MAGIC_USED;
    used->size  = size1;
    used->next  = avail;
    used->prev  = NULL;
    used->next2 = SHHEAP_POISON_NULL(0x41);
    used->prev2 = SHHEAP_POISON_NULL(0x41);
    used->psh   = NULL;
    used->chunk = chunk;

    avail->magic = SHMEMHDR_MAGIC_FREE;
    avail->size  = (char *)chunk + chunk_size - (char *)(avail + 1);
    avail->next  = NULL;
    avail->prev  = used;
    avail->next2 = NULL;
    avail->prev2 = NULL;
    avail->psh   = NULL;
    avail->chunk = chunk;

    chunk->head = used;
    chunk->free_head = avail;
    chunk->size = chunk_size;
    chunk->magic = SHMEMCHUNK_MAGIC;
    chunk->prev = NULL;
    chunk->next = g_sh_heap;
    if (g_sh_heap)
        g_sh_heap->prev = chunk;
    g_sh_heap = chunk;
    chunk->padding0 = 0;
    chunk->padding1 = 0;

    SHHEAP_CHECK_2();
    return used;
}

/***
 * Splits a big memory block into two smaller, one with the
 * size @a size1.
 *
 * The one with the given size is removed from the free list
 * while the other one remains there.
 *
 * @returns The @a size1 sized block, NULL on failure.
 * @param   big         The block that is too big.
 * @param   size1       The size of the block to be returned (aligned).
 */
static shmemhdr *shheap_split(shmemhdr *big, size_t size1)
{
    shmemhdr *split;
    SHHEAP_ASSERT(SHHEAP_ALIGN(sizeof(*big)) == sizeof(*big));
    SHHEAP_ASSERT(big->magic == SHMEMHDR_MAGIC_FREE);
    SHHEAP_ASSERT(!big->next2 || big->next2->magic  == SHMEMHDR_MAGIC_FREE);
    SHHEAP_ASSERT(!big->prev2 || big->prev2->magic  == SHMEMHDR_MAGIC_FREE);

    split = (shmemhdr *)((uint8_t *)(big + 1) + size1);
    split->magic = SHMEMHDR_MAGIC_FREE;
    split->size  = big->size - size1 - sizeof(*split);
    split->next  = big->next;
    split->prev  = big;
    split->next2 = big->next2;
    split->prev2 = big->prev2;
    split->psh   = SHHEAP_POISON_NULL(0x54);
    split->chunk = big->chunk;

    if (big->next2)
        big->next2->prev2 = split;
    if (big->prev2)
        big->prev2->next2 = split;
    else
        big->chunk->free_head = split;

    big->magic = SHMEMHDR_MAGIC_USED;
    big->next2 = big->prev2 = SHHEAP_POISON_NULL(0x41);

    if (big->next)
        big->next->prev = split;
    big->next = split;
    big->size = size1;

    SHHEAP_CHECK_2();
    return big;
}

/***
 * Unlinks a free memory block.
 * @param   mem     The block to unlink.
 */
static void shheap_unlink_free(shmemhdr *mem)
{
    if (mem->next2)
        mem->next2->prev2 = mem->prev2;
    if (mem->prev2)
        mem->prev2->next2 = mem->next2;
    else
        mem->chunk->free_head = mem->next2;
    mem->magic = SHMEMHDR_MAGIC_USED;
    mem->next2 = mem->prev2 = SHHEAP_POISON_NULL(0x42);
}

#endif /* SHHEAP_IN_USE */


/** free() */
void sh_free(shinstance *psh, void *ptr)
{
#ifdef SHHEAP_IN_USE
    shmemhdr *mem;
    shmemhdr *right;
    shmemhdr *left;
    shmtxtmp tmp;

    if (ptr)
        mem = (shmemhdr *)ptr - 1;
    else
        return;

    if (mem->magic != SHMEMHDR_MAGIC_USED)
    {
        SHHEAP_ASSERT(0);
        return;
    }

    shmtx_enter(&g_sh_heap_mtx, &tmp);
    SHHEAP_CHECK();

    /* join right. */
    right = mem->next;
    if (    right
        &&  right->magic == SHMEMHDR_MAGIC_FREE)
    {
        mem->next = right->next;
        if (right->next)
            right->next->prev = mem;

        mem->next2 = right->next2;
        if (right->next2)
            right->next2->prev2 = mem;
        mem->prev2 = right->prev2;
        if (right->prev2)
            mem->prev2->next2 = mem;
        else
            mem->chunk->free_head = mem;

        mem->size += sizeof(*right) + right->size;
        mem->magic = SHMEMHDR_MAGIC_FREE;
        right->magic = ~SHMEMHDR_MAGIC_FREE;
        mem->psh = SHHEAP_POISON_NULL(0x50);
        SHHEAP_CHECK_2();
    }

    /* join left */
    left = mem->prev;
    if (    left
        &&  left->magic == SHMEMHDR_MAGIC_FREE)
    {
        left->next = mem->next;
        if (mem->next)
            mem->next->prev = left;

        if (mem->magic == SHMEMHDR_MAGIC_FREE)
        {
            if (mem->next2)
                mem->next2->prev2 = mem->prev2;
            if (mem->prev2)
                mem->prev2->next2 = mem->next2;
            else
                mem->chunk->free_head = mem->next2;
        }

        left->size += sizeof(*mem) + mem->size;
        mem->magic = ~SHMEMHDR_MAGIC_USED;
        left->psh = SHHEAP_POISON_NULL(0x51);
    }

    /* insert as free if necessary */
    else if (mem->magic == SHMEMHDR_MAGIC_USED)
    {
        mem->prev2 = NULL;
        mem->next2 = mem->chunk->free_head;
        if (mem->chunk->free_head)
            mem->chunk->free_head->prev2 = mem;
        mem->chunk->free_head = mem;
        mem->magic = SHMEMHDR_MAGIC_FREE;
        mem->psh = SHHEAP_POISON_NULL(0x52);
    }

    SHHEAP_CHECK();
    shmtx_leave(&g_sh_heap_mtx, &tmp);
#else
    if (ptr)
        free(ptr);
    (void)psh;
#endif
}

/** malloc() */
void *sh_malloc(shinstance *psh, size_t size)
{
#ifdef SHHEAP_IN_USE
    shmemchunk *chunk;
    shmemhdr *mem;
    shmtxtmp tmp;

    size = SHHEAP_ALIGN(size);
    SHHEAP_ASSERT(size);
    if (!size)
        size = SHHEAP_ALIGN(1);

    shmtx_enter(&g_sh_heap_mtx, &tmp);
    SHHEAP_CHECK();


    /* Search for fitting block */
    mem = NULL;
    chunk = g_sh_heap;
    while (chunk)
    {
        mem = chunk->free_head;
        while (mem && mem->size < size)
            mem = mem->next2;
        if (mem)
            break;
        chunk = chunk->next;
    }
    if (mem)
    {
        /* split it, or just unlink it? */
        if (mem->size - size > sizeof(*mem) * 2)
            mem = shheap_split(mem, size);
        else
            shheap_unlink_free(mem);
    }
    else
    {
        /* no block found, try grow the heap. */
        mem = shheap_grow(size);
        if (!mem)
        {
            shmtx_leave(&g_sh_heap_mtx, &tmp);
            return NULL;
        }
    }

    SHHEAP_CHECK();
    shmtx_leave(&g_sh_heap_mtx, &tmp);

    mem->psh = SHHEAP_POISON_PSH(psh, 0x53);

    return mem + 1;

#else
    (void)psh;
    return malloc(size);
#endif
}

/** calloc() */
void *sh_calloc(shinstance *psh, size_t num, size_t item_size)
{
#ifdef SHHEAP_IN_USE
    size_t size = num * item_size;
    void *pv = sh_malloc(psh, size);
    if (pv)
        pv = memset(pv, '\0', size);
    return pv;
#else
    (void)psh;
    return calloc(num, item_size);
#endif
}

/** realloc() */
void *sh_realloc(shinstance *psh, void *old, size_t new_size)
{
#ifdef SHHEAP_IN_USE
    void *pv;
    if (new_size)
    {
        if (old)
        {
            shmemhdr *hdr = (shmemhdr *)old - 1;
            if (hdr->size < new_size)
            {
                pv = sh_malloc(psh, new_size);
                if (pv)
                {
                    memcpy(pv, old, hdr->size);
                    sh_free(psh, old);
                }
            }
            else
                pv = old;
        }
        else
            pv = sh_malloc(psh, new_size);
    }
    else
    {
        sh_free(psh, old);
        pv = NULL;
    }
    return pv;
#else
    return realloc(old, new_size);
#endif
}

/** strdup() */
char *sh_strdup(shinstance *psh, const char *string)
{
    size_t len = strlen(string);
    char *ret = sh_malloc(psh, len + 1);
    if (ret)
        memcpy(ret, string, len + 1);
    return ret;
}


