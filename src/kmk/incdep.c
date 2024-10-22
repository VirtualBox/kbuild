#ifdef CONFIG_WITH_INCLUDEDEP
/* $Id: incdep.c 3628 2024-10-22 23:34:56Z knut.osmundsen@oracle.com $ */
/** @file
 * incdep - Simple dependency files.
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

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#ifdef __OS2__
# define INCL_BASE
# define INCL_ERRORS
#endif
#ifdef KBUILD_OS_WINDOWS
# ifdef KMK
#  define INCDEP_USE_KFSCACHE
# endif
#endif

#include "makeint.h"

#if !defined(WINDOWS32) && !defined(__OS2__)
# define HAVE_PTHREAD
#endif

#include <assert.h>

#include <glob.h>

#include "filedef.h"
#include "dep.h"
#include "job.h"
#include "commands.h"
#include "variable.h"
#include "rule.h"
#include "debug.h"
#include "strcache2.h"

#ifdef HAVE_FCNTL_H
# include <fcntl.h>
#else
# include <sys/file.h>
#endif

#ifdef WINDOWS32
# include <io.h>
# include <process.h>
# include <Windows.h>
# define PARSE_IN_WORKER
#endif

#ifdef INCDEP_USE_KFSCACHE
# include "nt/kFsCache.h"
extern PKFSCACHE g_pFsCache; /* dir-nt-bird.c for now */
#endif

#ifdef __OS2__
# include <os2.h>
# include <sys/fmutex.h>
#endif

#ifdef HAVE_PTHREAD
# include <pthread.h>
#endif

#ifdef __APPLE__
# include <malloc/malloc.h>
# define PARSE_IN_WORKER
#endif

#if defined(__gnu_linux__) || defined(__linux__)
# define PARSE_IN_WORKER
#endif


/*******************************************************************************
* Defined Constants And Macros                                                 *
********************************************************************************/
/* Custom assertion macro */
#if (!defined(NDEBUG) || 0) && defined(_MSC_VER)
# define MY_ASSERT(expr) do { \
    if (!!(expr)) { /* likely */ } \
    else { fprintf (stderr, "assertion failed on line %u in incdep.c: %s\n", __LINE__, #expr); __debugbreak(); } \
  } while (0)
# undef assert
# define assert(expr)     MY_ASSERT(expr)
#else
# define MY_ASSERT(expr)  ((void)0)
#endif


/*******************************************************************************
*   Structures and Typedefs                                                    *
*******************************************************************************/
struct incdep_worker_data
{
    int worker_tid;                         /* -1 for main, index for the others. */
    unsigned int done_count;                /* workers increment this */
    unsigned int flushed_count;             /* main thread: flushed done count. */
    unsigned int todo_count;                /* main thread: queued on todo */
};

struct incdep_variable_in_set
{
    struct incdep_variable_in_set *next;
    /* the parameters */
    struct strcache2_entry *name_entry;     /* dep strcache - WRONG */
    const char *value;                      /* xmalloc'ed */
    unsigned int value_length;
    int duplicate_value;                    /* 0 */
    enum variable_origin origin;
    int recursive;
    struct variable_set *set;
    const floc *flocp;                      /* NILF */
};

struct incdep_variable_def
{
    struct incdep_variable_def *next;
    /* the parameters */
    const floc *flocp;                      /* NILF */
    struct strcache2_entry *name_entry;     /* dep strcache - WRONG */
    char *value;                            /* xmalloc'ed, free it */
    unsigned int value_length;
    enum variable_origin origin;
    enum variable_flavor flavor;
    int target_var;
};

struct incdep_recorded_file
{
    struct incdep_recorded_file *next;

    /* the parameters */
    struct strcache2_entry *filename_entry; /* dep strcache; converted to a nameseq record. */
    struct dep *deps;                       /* All the names are dep strcache entries. */
    const floc *flocp;                     /* NILF */
};


/* per dep file structure. */
struct incdep
{
  struct incdep *next;
  char *file_base;
  char *file_end;

  int worker_tid;
#ifdef PARSE_IN_WORKER
  unsigned int err_line_no;
  const char *err_msg;

  struct incdep_variable_in_set *recorded_variables_in_set_head;
  struct incdep_variable_in_set *recorded_variables_in_set_tail;

  struct incdep_variable_def *recorded_variable_defs_head;
  struct incdep_variable_def *recorded_variable_defs_tail;

  struct incdep_recorded_file *recorded_file_head;
  struct incdep_recorded_file *recorded_file_tail;
#endif
#ifdef INCDEP_USE_KFSCACHE
  /** Pointer to the fs cache object for this file (it exists and is a file). */
  PKFSOBJ pFileObj;
#else
  char name[1];
#endif
};


/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/

/* mutex protecting the globals and an associated condition/event. */
#ifdef HAVE_PTHREAD
static pthread_mutex_t incdep_mtx;
static pthread_cond_t  incdep_cond_todo;
static pthread_cond_t  incdep_cond_done;

#elif defined (WINDOWS32)
static CRITICAL_SECTION incdep_mtx;
static HANDLE incdep_hev_todo;
static HANDLE incdep_hev_done;
static int volatile incdep_hev_todo_waiters;
static int volatile incdep_hev_done_waiters;

#elif defined (__OS2__)
static _fmutex incdep_mtx;
static HEV incdep_hev_todo;
static HEV incdep_hev_done;
static int volatile incdep_hev_todo_waiters;
static int volatile incdep_hev_done_waiters;
#endif

/* flag indicating whether the threads, lock and event/condvars has
   been initialized or not. */
static int incdep_initialized;

/* the list of files that needs reading. */
static struct incdep * volatile incdep_head_todo  = NULL;
static struct incdep * volatile incdep_tail_todo  = NULL;
static unsigned int volatile    incdep_count_todo = 0;

/* the number of files that are currently being read. */
static int volatile incdep_num_reading = 0;

/* the list of files that have been read. */
static struct incdep * volatile incdep_head_done  = NULL;
static struct incdep * volatile incdep_tail_done  = NULL;
static unsigned int volatile    incdep_count_done = 0;


/* The handles to the worker threads. */
#ifdef HAVE_PTHREAD
# define INCDEP_MAX_THREADS 1
static pthread_t incdep_threads[INCDEP_MAX_THREADS];

#elif defined (WINDOWS32)
# define INCDEP_MAX_THREADS 2
static HANDLE incdep_threads[INCDEP_MAX_THREADS];

#elif defined (__OS2__)
# define INCDEP_MAX_THREADS 2
static TID incdep_threads[INCDEP_MAX_THREADS];
#endif

static struct alloccache incdep_rec_caches[INCDEP_MAX_THREADS];
static struct alloccache incdep_dep_caches[INCDEP_MAX_THREADS];
static struct strcache2 incdep_dep_strcaches[INCDEP_MAX_THREADS];
static struct strcache2 incdep_var_strcaches[INCDEP_MAX_THREADS];
static unsigned incdep_num_threads;

/* flag indicating whether the worker threads should terminate or not. */
static int volatile incdep_terminate;

#ifdef __APPLE__
/* malloc zone for the incdep threads. */
static malloc_zone_t *incdep_zone;
#endif

/* Worker specific data, the extra entry is for the main thread.
   TODO: Move all parallel arrays in here to avoid unnecessary cacheline
         sharing between worker threads. */
static struct incdep_worker_data incdep_worker_data[INCDEP_MAX_THREADS + 1];


/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/
static void incdep_flush_it (floc *);
static void eval_include_dep_file (struct incdep *, floc *);
static void incdep_commit_recorded_file (const char *filename, struct dep *deps,
                                         const floc *flocp);


/* xmalloc wrapper.
   For working around multithreaded performance problems found on Darwin,
   Linux (glibc), and possibly other systems. */
static void *
incdep_xmalloc (struct incdep *cur, size_t size)
{
  void *ptr;

#ifdef __APPLE__
  if (cur && cur->worker_tid != -1)
    {
      ptr = malloc_zone_malloc (incdep_zone, size);
      if (!ptr)
        O (fatal, NILF, _("virtual memory exhausted"));
    }
  else
    ptr = xmalloc (size);
#else
  ptr = xmalloc (size);
#endif

  (void)cur;
  return ptr;
}

#if 0
/* cmalloc wrapper */
static void *
incdep_xcalloc (struct incdep *cur, size_t size)
{
  void *ptr;

#ifdef __APPLE__
  if (cur && cur->worker_tid != -1)
    ptr = malloc_zone_calloc (incdep_zone, size, 1);
  else
    ptr = calloc (size, 1);
#else
  ptr = calloc (size, 1);
#endif
  if (!ptr)
    fatal (NILF, _("virtual memory exhausted"));

  (void)cur;
  return ptr;
}
#endif /* unused */

/* free wrapper */
static void
incdep_xfree (struct incdep *cur, void *ptr)
{
  /* free() *must* work for the allocation hacks above because
     of free_dep_chain. */
  free (ptr);
  (void)cur;
}

/* alloc a dep structure. These are allocated in bunches to save time. */
struct dep *
incdep_alloc_dep (struct incdep *cur)
{
  struct alloccache *cache;
  if (cur->worker_tid != -1)
    cache = &incdep_dep_caches[cur->worker_tid];
  else
    cache = &dep_cache;
  return alloccache_calloc (cache);
}

/* duplicates the dependency list pointed to by srcdep. */
static struct dep *
incdep_dup_dep_list (struct incdep *cur, struct dep const *srcdep)
{
  struct alloccache *cache;
  struct dep *retdep;
  struct dep *dstdep;

  if (cur->worker_tid != -1)
    cache = &incdep_dep_caches[cur->worker_tid];
  else
    cache = &dep_cache;

  if (srcdep)
    {
      retdep = dstdep = alloccache_alloc (cache);
      for (;;)
        {
          dstdep->name = srcdep->name; /* string cached */
          dstdep->includedep = srcdep->includedep;
          srcdep = srcdep->next;
          if (!srcdep)
            {
              dstdep->next = NULL;
              break;
            }
          dstdep->next = alloccache_alloc (cache);
          dstdep = dstdep->next;
        }
    }
  else
    retdep = NULL;
  return retdep;
}


/* allocate a record. */
static void *
incdep_alloc_rec (struct incdep *cur)
{
  return alloccache_alloc (&incdep_rec_caches[cur->worker_tid]);
}

/* free a record. */
static void
incdep_free_rec (struct incdep *cur, void *rec)
{
  /*alloccache_free (&incdep_rec_caches[cur->worker_tid], rec); - doesn't work of course. */
}


/* grow a cache. */
static void *
incdep_cache_allocator (void *thrd, unsigned int size)
{
  (void)thrd;
#ifdef __APPLE__
  return malloc_zone_malloc (incdep_zone, size);
#else
  return xmalloc (size);
#endif
}

/* term a cache. */
static void
incdep_cache_deallocator (void *thrd, void *ptr, unsigned int size)
{
  (void)thrd;
  (void)size;
  free (ptr);
}

/* acquires the lock */
void
incdep_lock(void)
{
#if defined (HAVE_PTHREAD) && !defined (CONFIG_WITHOUT_THREADS)
  pthread_mutex_lock (&incdep_mtx);
#elif defined (WINDOWS32)
  EnterCriticalSection (&incdep_mtx);
#elif defined (__OS2__)
  _fmutex_request (&incdep_mtx, 0);
#elif !defined(CONFIG_WITHOUT_THREADS)
# error Misconfig?
#endif
}

/* releases the lock */
void
incdep_unlock(void)
{
#if defined (HAVE_PTHREAD) && !defined (CONFIG_WITHOUT_THREADS)
  pthread_mutex_unlock (&incdep_mtx);
#elif defined(WINDOWS32)
  LeaveCriticalSection (&incdep_mtx);
#elif defined(__OS2__)
  _fmutex_release (&incdep_mtx);
#endif
}

/* signals the main thread that there is stuff todo. caller owns the lock. */
static void
incdep_signal_done (void)
{
#if defined (HAVE_PTHREAD) && !defined (CONFIG_WITHOUT_THREADS)
  pthread_cond_broadcast (&incdep_cond_done);
#elif defined (WINDOWS32)
  if (incdep_hev_done_waiters)
    SetEvent (incdep_hev_done);
#elif defined (__OS2__)
  if (incdep_hev_done_waiters)
    DosPostEventSem (incdep_hev_done);
#endif
}

/* waits for a reader to finish reading. caller owns the lock. */
static void
incdep_wait_done (void)
{
#if defined (HAVE_PTHREAD) && !defined (CONFIG_WITHOUT_THREADS)
  pthread_cond_wait (&incdep_cond_done, &incdep_mtx);

#elif defined (WINDOWS32)
  ResetEvent (incdep_hev_done);
  incdep_hev_done_waiters++;
  incdep_unlock ();
  WaitForSingleObject (incdep_hev_done, INFINITE);
  incdep_lock ();
  incdep_hev_done_waiters--;

#elif defined (__OS2__)
  ULONG ulIgnore;
  DosResetEventSem (incdep_hev_done, &ulIgnore);
  incdep_hev_done_waiters++;
  incdep_unlock ();
  DosWaitEventSem (incdep_hev_done, SEM_INDEFINITE_WAIT);
  incdep_lock ();
  incdep_hev_done_waiters--;
#endif
}

/* signals the worker threads. caller owns the lock. */
static void
incdep_signal_todo (void)
{
#if defined (HAVE_PTHREAD) && !defined (CONFIG_WITHOUT_THREADS)
  pthread_cond_broadcast (&incdep_cond_todo);
#elif defined(WINDOWS32)
  if (incdep_hev_todo_waiters)
    SetEvent (incdep_hev_todo);
#elif defined(__OS2__)
  if (incdep_hev_todo_waiters)
    DosPostEventSem (incdep_hev_todo);
#endif
}

/* waits for stuff to arrive in the todo list. caller owns the lock. */
static void
incdep_wait_todo (void)
{
#if defined (HAVE_PTHREAD) && !defined (CONFIG_WITHOUT_THREADS)
  pthread_cond_wait (&incdep_cond_todo, &incdep_mtx);

#elif defined (WINDOWS32)
  ResetEvent (incdep_hev_todo);
  incdep_hev_todo_waiters++;
  incdep_unlock ();
  WaitForSingleObject (incdep_hev_todo, INFINITE);
  incdep_lock ();
  incdep_hev_todo_waiters--;

#elif defined (__OS2__)
  ULONG ulIgnore;
  DosResetEventSem (incdep_hev_todo, &ulIgnore);
  incdep_hev_todo_waiters++;
  incdep_unlock ();
  DosWaitEventSem (incdep_hev_todo, SEM_INDEFINITE_WAIT);
  incdep_lock ();
  incdep_hev_todo_waiters--;
#endif
}

/* Reads a dep file into memory. */
static int
incdep_read_file (struct incdep *cur, floc *f)
{
#ifdef INCDEP_USE_KFSCACHE
  size_t const cbFile = (size_t)cur->pFileObj->Stats.st_size;

  assert(cur->pFileObj->fHaveStats);
  cur->file_base = incdep_xmalloc (cur, cbFile + 1);
  if (cur->file_base)
    {
      if (kFsCacheFileSimpleOpenReadClose (g_pFsCache, cur->pFileObj, 0, cur->file_base, cbFile))
        {
          cur->file_end = cur->file_base + cbFile;
          cur->file_base[cbFile] = '\0';
          return 0;
        }
      incdep_xfree (cur, cur->file_base);
    }
  OSS (error, f, "%s/%s: error reading file", cur->pFileObj->pParent->Obj.pszName, cur->pFileObj->pszName);

#else /* !INCDEP_USE_KFSCACHE */
  int fd;
  struct stat st;

  errno = 0;
# ifdef O_BINARY
  fd = open (cur->name, O_RDONLY | O_BINARY, 0);
# else
  fd = open (cur->name, O_RDONLY, 0);
# endif
  if (fd < 0)
    {
      /* ignore non-existing dependency files. */
      int err = errno;
      if (err == ENOENT || stat (cur->name, &st) != 0)
        return 1;
      OSS (error, f, "%s: %s", cur->name, strerror (err));
      return -1;
    }
# ifdef KBUILD_OS_WINDOWS /* fewer kernel calls */
  if (!birdStatOnFdJustSize (fd, &st.st_size))
# else
  if (!fstat (fd, &st))
# endif
    {
      cur->file_base = incdep_xmalloc (cur, st.st_size + 1);
      if (read (fd, cur->file_base, st.st_size) == st.st_size)
        {
          close (fd);
          cur->file_end = cur->file_base + st.st_size;
          cur->file_base[st.st_size] = '\0';
          return 0;
        }

      /* bail out */

      OSS (error, f, "%s: read: %s", cur->name, strerror (errno));
      incdep_xfree (cur, cur->file_base);
    }
  else
    OSS (error, f, "%s: fstat: %s", cur->name, strerror (errno));

  close (fd);
#endif /* !INCDEP_USE_KFSCACHE */
  cur->file_base = cur->file_end = NULL;
  return -1;
}

/* Free the incdep structure. */
static void
incdep_freeit (struct incdep *cur)
{
#ifdef PARSE_IN_WORKER
  assert (!cur->recorded_variables_in_set_head);
  assert (!cur->recorded_variable_defs_head);
  assert (!cur->recorded_file_head);
#endif

  incdep_xfree (cur, cur->file_base);
#ifdef INCDEP_USE_KFSCACHE
  /** @todo release object ref some day... */
#endif
  cur->next = NULL;
  free (cur);
}

/* A worker thread. */
void
incdep_worker (int thrd)
{
  struct incdep_worker_data *thrd_data = &incdep_worker_data[thrd];
  thrd_data->worker_tid = thrd;

  incdep_lock ();

  while (!incdep_terminate)
   {
      /* get job from the todo list. */

      struct incdep *tmp;
      struct incdep *cur = incdep_head_todo;
      if (!cur)
        {
          incdep_wait_todo ();
          continue;
        }
      if (cur->next)
        {
          assert (incdep_count_todo > 1);
          assert (cur != incdep_tail_todo);
          incdep_head_todo = cur->next;
        }
      else
        {
          assert (incdep_count_todo == 1);
          assert (cur == incdep_tail_todo);
          incdep_head_todo = incdep_tail_todo = NULL;
        }
      incdep_count_todo--;
      incdep_num_reading++;

      /* read the file. */

      incdep_unlock ();
      cur->worker_tid = thrd;

      incdep_read_file (cur, NILF);
#ifdef PARSE_IN_WORKER
      eval_include_dep_file (cur, NILF);
#endif

      cur->worker_tid = -1;
      incdep_lock ();

      /* insert finished job into the done list. */

      incdep_num_reading--;
      cur->next = NULL;
      tmp = incdep_tail_done;
      if (tmp)
        {
          tmp->next = cur;
          assert (incdep_count_done > 0);
        }
      else
        {
          assert (!incdep_head_done);
          assert (incdep_count_done == 0);
          incdep_head_done = cur;
        }
      incdep_tail_done = cur;
      incdep_count_done++;

      thrd_data->done_count++;

      incdep_signal_done ();
   }

  incdep_unlock ();
}

/* Thread library specific thread functions wrapping incdep_wroker. */
#ifdef HAVE_PTHREAD
static void *
incdep_worker_pthread (void *thrd)
{
  incdep_worker ((size_t)thrd);
  return NULL;
}

#elif defined (WINDOWS32)
static unsigned __stdcall
incdep_worker_windows (void *thrd)
{
  incdep_worker ((size_t)thrd);
  return 0;
}

#elif defined (__OS2__)
static void
incdep_worker_os2 (void *thrd)
{
  incdep_worker ((size_t)thrd);
}
#endif

/* Checks if threads are enabled or not.

   This is a special hack so that is possible to disable the threads when in a
   debian fakeroot environment.  Thus, in addition to the KMK_THREADS_DISABLED
   and KMK_THREADS_ENABLED environment variable check we also check for signs
   of fakeroot.  */
static int
incdep_are_threads_enabled (void)
{
#if defined (CONFIG_WITHOUT_THREADS)
  return 0;
#endif

  /* Generic overrides. */
  if (getenv ("KMK_THREADS_DISABLED"))
    {
      O (message, 1, "Threads disabled (environment)");
      return 0;
    }
  if (getenv ("KMK_THREADS_ENABLED"))
    return 1;

#if defined (__gnu_linux__) || defined (__linux__) || defined(__GLIBC__)
  /* Try detect fakeroot. */
  if (getenv ("FAKEROOTKEY")
   || getenv ("FAKEROOTUID")
   || getenv ("FAKEROOTGID")
   || getenv ("FAKEROOTEUID")
   || getenv ("FAKEROOTEGID")
   || getenv ("FAKEROOTSUID")
   || getenv ("FAKEROOTSGID")
   || getenv ("FAKEROOTFUID")
   || getenv ("FAKEROOTFGID")
   || getenv ("FAKEROOTDONTTRYCHOWN")
   || getenv ("FAKEROOT_FD_BASE")
   || getenv ("FAKEROOT_DB_SEARCH_PATHS"))
    {
      O (message, 1, "Threads disabled (fakeroot)");
      return 0;
    }

  /* LD_PRELOAD could indicate undetected debian fakeroot or some
     other ingenius library which cannot deal correctly with threads. */
  if (getenv ("LD_PRELOAD"))
    {
      O (message, 1, "Threads disabled (LD_PRELOAD)");
      return 0;
    }

#elif defined(__APPLE__) \
   || defined(__sun__) || defined(__SunOS__) || defined(__sun) || defined(__SunOS) \
   || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__) \
   || defined(__HAIKU__)
  /* No broken preload libraries known to be in common use on these platforms... */

#elif defined(_MSC_VER) || defined(_WIN32) || defined(__OS2__)
  /* No preload mess to care about. */

#else
# error "Add your self to the appropriate case above and send a patch to bird."
#endif
  return 1;
}

/* Creates the the worker threads. */
static void
incdep_init (floc *f)
{
  unsigned i;
#if defined (HAVE_PTHREAD) && !defined (CONFIG_WITHOUT_THREADS)
  int rc;
  pthread_attr_t attr;

#elif defined (WINDOWS32)
  unsigned tid;
  uintptr_t hThread;

#elif defined (__OS2__)
  int rc;
  int tid;
#endif
  (void)f;

  /* heap hacks */

#ifdef __APPLE__
  incdep_zone = malloc_create_zone (0, 0);
  if (!incdep_zone)
    incdep_zone = malloc_default_zone ();
#endif


  /* create the mutex and two condition variables / event objects. */

#if defined (HAVE_PTHREAD) && !defined (CONFIG_WITHOUT_THREADS)
  rc = pthread_mutex_init (&incdep_mtx, NULL);
  if (rc)
    ON (fatal, f, _("pthread_mutex_init failed: err=%d"), rc);
  rc = pthread_cond_init (&incdep_cond_todo, NULL);
  if (rc)
    ON (fatal, f, _("pthread_cond_init failed: err=%d"), rc);
  rc = pthread_cond_init (&incdep_cond_done, NULL);
  if (rc)
    ON (fatal, f, _("pthread_cond_init failed: err=%d"), rc);

#elif defined (WINDOWS32)
  InitializeCriticalSection (&incdep_mtx);
  incdep_hev_todo = CreateEvent (NULL, TRUE /*bManualReset*/, FALSE /*bInitialState*/, NULL);
  if (!incdep_hev_todo)
    ON (fatal, f, _("CreateEvent failed: err=%d"), GetLastError());
  incdep_hev_done = CreateEvent (NULL, TRUE /*bManualReset*/, FALSE /*bInitialState*/, NULL);
  if (!incdep_hev_done)
    ON (fatal, f, _("CreateEvent failed: err=%d"), GetLastError());
  incdep_hev_todo_waiters = 0;
  incdep_hev_done_waiters = 0;

#elif defined (__OS2__)
  _fmutex_create (&incdep_mtx, 0);
  rc = DosCreateEventSem (NULL, &incdep_hev_todo, 0, FALSE);
  if (rc)
    ON (fatal, f, _("DosCreateEventSem failed: rc=%d"), rc);
  rc = DosCreateEventSem (NULL, &incdep_hev_done, 0, FALSE);
  if (rc)
    ON (fatal, f, _("DosCreateEventSem failed: rc=%d"), rc);
  incdep_hev_todo_waiters = 0;
  incdep_hev_done_waiters = 0;
#endif

  /* create the worker threads and associated per thread data. */

  incdep_terminate = 0;
  if (incdep_are_threads_enabled())
    {
      incdep_num_threads = sizeof (incdep_threads) / sizeof (incdep_threads[0]);
      if (incdep_num_threads + 1 > job_slots)
        incdep_num_threads = job_slots <= 1 ? 1 : job_slots - 1;
      for (i = 0; i < incdep_num_threads; i++)
        {
          /* init caches */
          unsigned rec_size = sizeof (struct incdep_variable_in_set);
          if (rec_size < sizeof (struct incdep_variable_def))
            rec_size = sizeof (struct incdep_variable_def);
          if (rec_size < sizeof (struct incdep_recorded_file))
            rec_size = sizeof (struct incdep_recorded_file);
          alloccache_init (&incdep_rec_caches[i], rec_size, "incdep rec",
                           incdep_cache_allocator, (void *)(size_t)i);
          alloccache_init (&incdep_dep_caches[i], sizeof(struct dep), "incdep dep",
                           incdep_cache_allocator, (void *)(size_t)i);
          strcache2_init (&incdep_dep_strcaches[i],
                          "incdep dep", /* name */
                          65536,        /* hash size */
                          0,            /* default segment size*/
#ifdef HAVE_CASE_INSENSITIVE_FS
                          1,            /* case insensitive */
#else
                          0,            /* case insensitive */
#endif
                          0);           /* thread safe */

          strcache2_init (&incdep_var_strcaches[i],
                          "incdep var", /* name */
                          32768,        /* hash size */
                          0,            /* default segment size*/
                          0,            /* case insensitive */
                          0);           /* thread safe */

          /* create the thread. */
#if defined (HAVE_PTHREAD) && !defined (CONFIG_WITHOUT_THREADS)
          rc = pthread_attr_init (&attr);
          if (rc)
            ON (fatal, f, _("pthread_attr_init failed: err=%d"), rc);
          /*rc = pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_JOINABLE); */
          rc = pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_DETACHED);
          if (rc)
            ON (fatal, f, _("pthread_attr_setdetachstate failed: err=%d"), rc);
          rc = pthread_create (&incdep_threads[i], &attr,
                               incdep_worker_pthread, (void *)(size_t)i);
          if (rc)
            ON (fatal, f, _("pthread_mutex_init failed: err=%d"), rc);
          pthread_attr_destroy (&attr);

#elif defined (WINDOWS32)
          tid = 0;
          hThread = _beginthreadex (NULL, 128*1024, incdep_worker_windows,
                                    (void *)i, 0, &tid);
          if (hThread == 0 || hThread == ~(uintptr_t)0)
            ON (fatal, f, _("_beginthreadex failed: err=%d"), errno);
          incdep_threads[i] = (HANDLE)hThread;

#elif defined (__OS2__)
          tid = _beginthread (incdep_worker_os2, NULL, 128*1024, (void *)i);
          if (tid <= 0)
            ON (fatal, f, _("_beginthread failed: err=%d"), errno);
          incdep_threads[i] = tid;
#endif
        }
    }
  else
    incdep_num_threads = 0;

  incdep_initialized = 1;
}

/* Flushes outstanding work and terminates the worker threads.
   This is called from snap_deps(). */
void
incdep_flush_and_term (void)
{
  unsigned i;
  unsigned total_done = 0;

  if (!incdep_initialized)
    return;

  /* flush any out standing work */

  incdep_flush_it (NILF);

  /* tell the threads to terminate */

  incdep_lock ();
  incdep_terminate = 1;
  incdep_signal_todo ();
  incdep_unlock ();

  /* wait for the threads to quit */

  for (i = 0; i < incdep_num_threads; i++)
    {
      /* more later? */

      /* terminate or join up the allocation caches. */
      alloccache_term (&incdep_rec_caches[i], incdep_cache_deallocator, (void *)(size_t)i);
      alloccache_join (&dep_cache, &incdep_dep_caches[i]);
      strcache2_term (&incdep_dep_strcaches[i]);
      strcache2_term (&incdep_var_strcaches[i]);

      /* accounting */
      total_done += incdep_worker_data[i].done_count;
    }
  incdep_num_threads = 0;

  /* sanity check */
  if (total_done != incdep_worker_data[INCDEP_MAX_THREADS].flushed_count)
    fprintf (stderr, "kmk/incdep: warning: total_done=%#x does not equal flushed_count=%#x!\n",
             total_done, incdep_worker_data[INCDEP_MAX_THREADS].flushed_count);
  if (total_done != incdep_worker_data[INCDEP_MAX_THREADS].todo_count)
    fprintf (stderr, "kmk/incdep: warning: total_done=%#x does not equal todo_count=%#x!\n",
             total_done, incdep_worker_data[INCDEP_MAX_THREADS].todo_count);

  /* destroy the lock and condition variables / event objects. */

  /* later */

  incdep_initialized = 0;
}

#ifdef PARSE_IN_WORKER
/* Flushes a strcache entry returning the actual string cache entry.
   The input is freed! */
static const char *
incdep_flush_strcache_entry (struct strcache2_entry *entry)
{
  if (!entry->user)
    entry->user = (void *) strcache2_add_hashed_file (&file_strcache,
                                                      (const char *)(entry + 1),
                                                      entry->length, entry->hash);
  return (const char *)entry->user;
}

/* Flushes the recorded instructions. */
static void
incdep_flush_recorded_instructions (struct incdep *cur)
{
  struct incdep_variable_in_set *rec_vis;
  struct incdep_variable_def *rec_vd;
  struct incdep_recorded_file *rec_f;

  /* Display saved error. */

  if (cur->err_msg)
#ifdef INCDEP_USE_KFSCACHE
    OSSNS (error, NILF, "%s/%s(%d): %s", cur->pFileObj->pParent->Obj.pszName, cur->pFileObj->pszName,
           cur->err_line_no, cur->err_msg);
#else
    OSNS (error,NILF, "%s(%d): %s", cur->name, cur->err_line_no, cur->err_msg);
#endif


  /* define_variable_in_set */

  rec_vis = cur->recorded_variables_in_set_head;
  cur->recorded_variables_in_set_head = cur->recorded_variables_in_set_tail = NULL;
  if (rec_vis)
    do
      {
        void *free_me = rec_vis;
        unsigned int name_length = rec_vis->name_entry->length;
        define_variable_in_set (incdep_flush_strcache_entry (rec_vis->name_entry),
                                name_length,
                                rec_vis->value,
                                rec_vis->value_length,
                                rec_vis->duplicate_value,
                                rec_vis->origin,
                                rec_vis->recursive,
                                rec_vis->set,
                                rec_vis->flocp);
        rec_vis = rec_vis->next;
        incdep_free_rec (cur, free_me);
      }
    while (rec_vis);

  /* do_variable_definition */

  rec_vd = cur->recorded_variable_defs_head;
  cur->recorded_variable_defs_head = cur->recorded_variable_defs_tail = NULL;
  if (rec_vd)
    do
      {
        void *free_me = rec_vd;
        do_variable_definition_2 (rec_vd->flocp,
                                  incdep_flush_strcache_entry (rec_vd->name_entry),
                                  rec_vd->value,
                                  rec_vd->value_length,
                                  0,
                                  rec_vd->value,
                                  rec_vd->origin,
                                  rec_vd->flavor,
                                  rec_vd->target_var);
        rec_vd = rec_vd->next;
        incdep_free_rec (cur, free_me);
      }
    while (rec_vd);

  /* record_files */

  rec_f = cur->recorded_file_head;
  cur->recorded_file_head = cur->recorded_file_tail = NULL;
  if (rec_f)
    do
      {
        void *free_me = rec_f;
        struct dep *dep;

        for (dep = rec_f->deps; dep; dep = dep->next)
          dep->name = incdep_flush_strcache_entry ((struct strcache2_entry *)dep->name);

        incdep_commit_recorded_file (incdep_flush_strcache_entry (rec_f->filename_entry),
                                     rec_f->deps,
                                     rec_f->flocp);

        rec_f = rec_f->next;
        incdep_free_rec (cur, free_me);
      }
    while (rec_f);
}
#endif /* PARSE_IN_WORKER */

/* Record / issue a warning about a misformed dep file. */
static void
incdep_warn (struct incdep *cur, unsigned int line_no, const char *msg)
{
  if (cur->worker_tid == -1)
#ifdef INCDEP_USE_KFSCACHE
    OSSNS (error,NILF, "%s/%s(%d): %s", cur->pFileObj->pParent->Obj.pszName, cur->pFileObj->pszName, line_no, msg);
#else
    OSNS (error, NILF, "%s(%d): %s", cur->name, line_no, msg);
#endif
#ifdef PARSE_IN_WORKER
  else
    {
      cur->err_line_no = line_no;
      cur->err_msg = msg;
    }
#endif
}

/* Dependency or file strcache allocation / recording. */
static const char *
incdep_dep_strcache (struct incdep *cur, const char *str, int len)
{
  const char *ret;
  if (cur->worker_tid == -1)
    {
      /* Make sure the string is terminated before we hand it to
         strcache_add_len so it does have to make a temporary copy
         of it on the stack. */
      char ch = str[len];
      ((char *)str)[len] = '\0';
      ret = strcache_add_len (str, len);
      ((char *)str)[len] = ch;
    }
  else
    {
      /* Add it out the strcache of the thread. */
      ret = strcache2_add (&incdep_dep_strcaches[cur->worker_tid], str, len);
      ret = (const char *)strcache2_get_entry(&incdep_dep_strcaches[cur->worker_tid], ret);
    }
  return ret;
}

/* Variable name allocation / recording. */
static const char *
incdep_var_strcache (struct incdep *cur, const char *str, int len)
{
  const char *ret;
  if (cur->worker_tid == -1)
    {
      /* XXX: we're leaking this memory now! This will be fixed later. */
      ret = xmalloc (len + 1);
      memcpy ((char *)ret, str, len);
      ((char *)ret)[len] = '\0';
    }
  else
    {
      /* Add it out the strcache of the thread. */
      ret = strcache2_add (&incdep_var_strcaches[cur->worker_tid], str, len);
      ret = (const char *)strcache2_get_entry(&incdep_var_strcaches[cur->worker_tid], ret);
    }
  return ret;
}

/* Record / perform a variable definition in a set.
   The NAME is in the string cache.
   The VALUE is on the heap.
   The DUPLICATE_VALUE is always 0. */
static void
incdep_record_variable_in_set (struct incdep *cur,
                               const char *name, unsigned int name_length,
                               const char *value,
                               unsigned int value_length,
                               int duplicate_value,
                               enum variable_origin origin,
                               int recursive,
                               struct variable_set *set,
                               const floc *flocp)
{
  assert (!duplicate_value);
  if (cur->worker_tid == -1)
    define_variable_in_set (name, name_length, value, value_length,
                            duplicate_value, origin, recursive, set, flocp);
#ifdef PARSE_IN_WORKER
  else
    {
      struct incdep_variable_in_set *rec =
        (struct incdep_variable_in_set *)incdep_alloc_rec (cur);
      rec->name_entry = (struct strcache2_entry *)name;
      rec->value = value;
      rec->value_length = value_length;
      rec->duplicate_value = duplicate_value;
      rec->origin = origin;
      rec->recursive = recursive;
      rec->set = set;
      rec->flocp = flocp;

      rec->next = NULL;
      if (cur->recorded_variables_in_set_tail)
        cur->recorded_variables_in_set_tail->next = rec;
      else
        cur->recorded_variables_in_set_head = rec;
      cur->recorded_variables_in_set_tail = rec;
    }
#endif
}

/* Record / perform a variable definition. The VALUE should be disposed of. */
static void
incdep_record_variable_def (struct incdep *cur,
                            const floc *flocp,
                            const char *name,
                            unsigned int name_length,
                            char *value,
                            unsigned int value_length,
                            enum variable_origin origin,
                            enum variable_flavor flavor,
                            int target_var)
{
  if (cur->worker_tid == -1)
    do_variable_definition_2 (flocp, name, value, value_length, 0, value,
                              origin, flavor, target_var);
#ifdef PARSE_IN_WORKER
  else
    {
      struct incdep_variable_def *rec =
        (struct incdep_variable_def *)incdep_alloc_rec (cur);
      rec->flocp = flocp;
      rec->name_entry = (struct strcache2_entry *)name;
      rec->value = value;
      rec->value_length = value_length;
      rec->origin = origin;
      rec->flavor = flavor;
      rec->target_var = target_var;

      rec->next = NULL;
      if (cur->recorded_variable_defs_tail)
        cur->recorded_variable_defs_tail->next = rec;
      else
        cur->recorded_variable_defs_head = rec;
      cur->recorded_variable_defs_tail = rec;
    }
#else
  (void)name_length;
#endif
}

/* Similar to record_files in read.c, only much much simpler. */
static void
incdep_commit_recorded_file (const char *filename, struct dep *deps,
                             const floc *flocp)
{
  struct file *f;

  /* Perform some validations. */
  if (filename[0] == '.'
      && (   streq(filename, ".POSIX")
          || streq(filename, ".EXPORT_ALL_VARIABLES")
          || streq(filename, ".INTERMEDIATE")
          || streq(filename, ".LOW_RESOLUTION_TIME")
          || streq(filename, ".NOTPARALLEL")
          || streq(filename, ".ONESHELL")
          || streq(filename, ".PHONY")
          || streq(filename, ".PRECIOUS")
          || streq(filename, ".SECONDARY")
          || streq(filename, ".SECONDTARGETEXPANSION")
          || streq(filename, ".SILENT")
          || streq(filename, ".SHELLFLAGS")
          || streq(filename, ".SUFFIXES")
         )
     )
    {
      OS (error, flocp, _("reserved filename '%s' used in dependency file, ignored"), filename);
      return;
    }

  /* Lookup or create an entry in the database. */
  f = enter_file (filename);
  if (f->double_colon)
    {
      OS (error, flocp, _("dependency file '%s' has a double colon entry already, ignoring"), filename);
      return;
    }
  f->is_target = 1;

  /* Append dependencies. */
  deps = enter_prereqs (deps, NULL);
  if (deps)
    {
      struct dep *last = f->deps;
      if (!last)
        f->deps = deps;
      else
        {
          while (last->next)
            last = last->next;
          last->next = deps;
        }
    }
}

/* Record a file.*/
static void
incdep_record_file (struct incdep *cur,
                    const char *filename,
                    struct dep *deps,
                    const floc *flocp)
{
  if (cur->worker_tid == -1)
    incdep_commit_recorded_file (filename, deps, flocp);
#ifdef PARSE_IN_WORKER
  else
    {
      struct incdep_recorded_file *rec =
        (struct incdep_recorded_file *) incdep_alloc_rec (cur);

      rec->filename_entry = (struct strcache2_entry *)filename;
      rec->deps = deps;
      rec->flocp = flocp;

      rec->next = NULL;
      if (cur->recorded_file_tail)
        cur->recorded_file_tail->next = rec;
      else
        cur->recorded_file_head = rec;
      cur->recorded_file_tail = rec;
    }
#endif
}

/* Counts slashes backwards from SLASH, stopping at START. */
static size_t incdep_count_slashes_backwards(const char *slash, const char *start)
{
  size_t slashes = 1;
  assert (*slash == '\\');
  while ((uintptr_t)slash > (uintptr_t)start && slash[0 - slashes] == '\\')
    slashes++;
  return slashes;
}

/* Whitespace cannot be escaped at the end of a line, there has to be
   some stuff following it other than a line continuation slash.

   So, we look ahead and makes sure that there is something non-whitespaced
   following this allegedly escaped whitespace.

   This code ASSUMES the file content is zero terminated! */
static int incdep_verify_escaped_whitespace(const char *ws)
{
  char ch;

  assert(ws[-1] == '\\');
  assert(ISBLANK((unsigned int)ws[0]));

  /* If the character following the '\ ' sequence is not a whitespace,
     another escape character or null terminator, we're good. */
  ws += 2;
  ch = *ws;
  if (ch != '\\' && !ISSPACE((unsigned int)ch) && ch != '\0')
    return 1;

  /* Otherwise we'll have to parse forward till we hit the end of the
     line/file or something. */
  while ((ch = *ws++) != '\0')
    {
      if (ch == '\\')
        {
           /* escaped newline? */
           ch = *ws;
           if (ch == '\n')
             ws++;
           else  if (ch == '\r' && ws[1] == '\n')
             ws += 2;
           else
             return 1;
        }
      else if (ISBLANK((unsigned int)ch))
      { /* contine */ }
      else if (!ISSPACE((unsigned int)ch))
        return 1;
      else
        return 0; /* newline; all trailing whitespace will be ignored. */
    }

  return 0;
}

/* Unescapes the next filename and returns cached copy.

   Modifies the input string that START points to.

   When NEXTP is not NULL, ASSUME target filename and that END isn't entirely
   accurate in case the filename ends with a trailing backslash.  There can be
   more than one filename in a this case.  NEXTP will be set to the first
   character after then filename.

   When NEXTP is NULL, ASSUME exactly one dependency filename and that END is
   accurately deliminating the string.
   */
static const char *
incdep_unescape_and_cache_filename(struct incdep *curdep, char *start, const char *end,
                                   int const is_dep, const char **nextp, unsigned int *linenop)
{
  unsigned const esc_mask = MAP_BLANK        /*  ' ' + '\t' */
                          | MAP_COLON        /* ':' */
                          | MAP_COMMENT      /* '#' */
                          | MAP_EQUALS       /* '=' */
                          | MAP_SEMI         /* ';' */
                          | (  is_dep
                             ? MAP_PIPE      /* '|' */
                             : MAP_PERCENT); /* '%' */
  unsigned const all_esc_mask = esc_mask | MAP_BLANK | MAP_NEWLINE;
  unsigned const stop_mask = nextp ? MAP_BLANK | MAP_NEWLINE | (!is_dep ? MAP_COLON : 0) : 0;
  char volatile *src;
  char volatile *dst;

  /*
   * Skip forward to the first escaped character so we can avoid unnecessary shifting.
   */
#if 1
  src = start;
  dst = start;
#elif 1
  static const char s_szStop[] = "\n\r\t ";

  src = memchr(start, '$', end - start);
  dst = memchr(start, '\\', end - start);
  if (src && ((uintptr_t)src < (uintptr_t)dst || dst == NULL))
    dst = src;
  else if (dst && ((uintptr_t)dst < (uintptr_t)src || src == NULL))
    src = dst;
  else
    {
      assert(src == NULL && dst == NULL);
      if (nextp)
        {
           int i = sizeof(s_szStop);
           while (i-- > 0)
             {
               char *stop = memchr(start, s_szStop[i], end - start);
               if (stop)
                 end = stop;
             }
            *nextp = end;
        }
      return incdep_dep_strcache (curdep, start, end - start);
    }
  if (nextp)
    {
      char *stop = src;
      int i = sizeof(s_szStop);
      while (i-- > 0)
        {
          char *stop2 = memchr(start, s_szStop[i], stop - start);
          if (stop2)
            stop = stop2;
        }
      if (stop != src)
        {
          *nextp = stop;
          return incdep_dep_strcache (curdep, start, stop - start);
        }
    }
#endif

  /*
   * Copy char-by-char, undoing escaping as we go along.
   */
  while ((uintptr_t)src < (uintptr_t)end)
    {
      const char ch = *src++;
      if (ch != '\\' && ch != '$')
        {
          if (!STOP_SET (ch, stop_mask)
#ifdef HAVE_DOS_PATHS
              || (    ch == ':'
                   && (uintptr_t)src < (uintptr_t)end
                   && (*src == '/' || *src == '\\')
                   && (dst - start) == 1
                   && isalpha ((unsigned char)*start))
#endif
             )
            *dst++ = ch;
          else
            {
              src--;
              break;
            }
        }
      else
        {
          char ch2 = *src++;  /* No bounds checking to handle "/dir/file\ : ..."  when end points at " :". */
          if (ch == '$')
            {
              if (ch2 != '$') /* $$ -> $ - Ignores secondary expansion! */
                  src--;
              *dst++ = ch;
            }
          else
            {
              unsigned int ch2_map;

              /* Eat all the slashes and see what's at the end of them as that's all
                 that's relevant.  If there is an escapable char, we'll emit half of
                 the slashes. */
              size_t const max_slashes = src - start - 1;
              size_t slashes = 1;
              while (ch2 == '\\')
                {
                  slashes++;
                  ch2 = *src++;
                }

              /* Is it escapable? */
              ch2_map = stopchar_map[(unsigned char)ch2];
              if (ch2_map & all_esc_mask)
                {
                  /* Non-whitespace is simple: Slash slashes, output or stop. */
                  if (!(ch2_map & (MAP_BLANK | MAP_NEWLINE)))
                    {
                      assert(ch2_map & esc_mask);
                      while (slashes >= 2)
                        {
                          *dst++ = '\\';
                          slashes -= 2;
                        }
                      if (slashes || !(stop_mask & ch2_map))
                        *dst++ = ch2;
                      else
                        {
                          src--;
                          break;
                        }
                    }
                  /* Escaped blanks or newlines.

                     We have to pretent that we've already replaced any escaped newlines
                     and associated whitespace with a single space here.  We also have to
                     pretend trailing whitespace doesn't exist when IS_DEP is non-zero.
                     This makes for pretty interesting times... */
                  else
                    {
                      char ch3;

                      /* An Escaped blank is interesting because it is striped unconditionally
                         at the end of a line, regardless of how many escaped newlines may
                         following it.  We join the escaped newline handling if we fine one
                         following us. */
                      if (ch2_map & MAP_BLANK)
                        {
                          /* skip whitespace and check for escaped newline. */
                          volatile char * const src_saved = src;
                          while ((ch3 = *src) != '\0' && ISBLANK(ch3))
                            src++;
                          if (ch3 == '\\' && src[1] == '\n')
                            src += 2; /* Escaped blank & newline joins into single space. */
                          else if (ch3 == '\\' && src[1] == '\r' && src[2] == '\n')
                            src += 3; /* -> Join the escaped newline code below on the next line. */
                          else if (STOP_SET(ch3, stop_mask & MAP_NEWLINE))
                            { /* last thing on the line, no blanks to escape. */
                              while (slashes-- > 0)
                                *dst++ = '\\';
                              break;
                            }
                          else
                            {
                              src = src_saved;
                              while (slashes >= 2)
                                {
                                  *dst++ = '\\';
                                  slashes -= 2;
                                }
                              if (slashes)
                                {
                                  *dst++ = ch2;
                                  continue;
                                }
                              assert (nextp || (uintptr_t)src >= (uintptr_t)end);
                              break;
                            }
                        }
                      /* Escaped newlines get special treatment as they an any adjacent whitespace
                         gets reduced to a single space, including subsequent escaped newlines.
                         In addition, if this is the final dependency/file and there is no
                         significant new characters following this escaped newline, the replacement
                         space will also be stripped and we won't have anything to escape, meaning
                         that the slashes will remain as is.  Finally, none of this space stuff can
                         be stop characters, unless of course a newline isn't escaped. */
                      else
                        {
                          assert (ch2_map & MAP_NEWLINE);
                          if (ch2 == '\r' && *src == '\n')
                            src++;
                        }

                      /* common space/newline code */
                      for (;;)
                        {
                          if (linenop)
                            *linenop += 1;
                          while ((uintptr_t)src < (uintptr_t)end && ISBLANK(*src))
                            src++;
                          if ((uintptr_t)src >= (uintptr_t)end)
                            {
                              ch3 = '\0';
                              break;
                            }
                          ch3 = *src;
                          if (ch3 != '\\')
                            break;
                          ch3 = src[1];
                          if (ch3 == '\n')
                            src += 2;
                          else if (ch3 == '\r' && src[2] == '\n')
                            src += 3;
                          else
                              break;
                        }

                      if (is_dep && STOP_SET(ch3, stop_mask | MAP_NUL))
                        { /* last thing on the line, no blanks to escape. */
                          while (slashes-- > 0)
                            *dst++ = '\\';
                          break;
                        }
                      while (slashes >= 2)
                        {
                          *dst++ = '\\';
                          slashes -= 2;
                        }
                      if (slashes)
                        *dst++ = ' ';
                      else
                        {
                          assert (nextp || (uintptr_t)src >= (uintptr_t)end);
                          break;
                        }
                    }
                }
              /* Just output the slash if non-escapable character: */
              else
                {
                  while (slashes-- > 0)
                    *dst++ = '\\';
                  src--;
                }
            }
        }
    }

  if (nextp)
    *nextp = (const char *)src;
  return incdep_dep_strcache(curdep, start, dst - start);
}

/* no nonsense dependency file including.

   Because nobody wants bogus dependency files to break their incremental
   builds with hard to comprehend error messages, this function does not
   use the normal eval routine but does all the parsing itself. This isn't,
   as much work as it sounds, because the necessary feature set is very
   limited.

   eval_include_dep_file groks:

   define var
   endef

   var [|:|?|>]= value [\]

   [\]
   file: [deps] [\]

   */
static void
eval_include_dep_file (struct incdep *curdep, floc *f)
{
  unsigned line_no = 1;
  const char *file_end = curdep->file_end;
  const char *cur = curdep->file_base;
  const char *endp;

  /* if no file data, just return immediately. */
  if (!cur)
    return;

  /* now parse the file. */
  while ((uintptr_t)cur < (uintptr_t)file_end)
    {
      /* skip empty lines */
      while ((uintptr_t)cur < (uintptr_t)file_end && ISSPACE (*cur) && *cur != '\n')
        ++cur;
      if ((uintptr_t)cur >= (uintptr_t)file_end)
        break;
      if (*cur == '#')
        {
          cur = memchr (cur, '\n', file_end - cur);
          if (!cur)
            break;
        }
      if (*cur == '\\')
        {
          unsigned eol_len = (file_end - cur > 1 && cur[1] == '\n') ? 2
                           : (file_end - cur > 2 && cur[1] == '\r' && cur[2] == '\n') ? 3
                           : (file_end - cur == 1) ? 1 : 0;
           if (eol_len)
             {
               cur += eol_len;
               line_no++;
               continue;
             }
        }
      if (*cur == '\n')
        {
          cur++;
          line_no++;
          continue;
        }

      /* define var
         ...
         endef */
      if (strneq (cur, "define ", 7))
        {
          const char *var;
          unsigned var_len;
          const char *value_start;
          const char *value_end;
          char *value;
          unsigned value_len;
          int found_endef = 0;

          /* extract the variable name. */
          cur += 7;
          while (ISBLANK (*cur))
            ++cur;
          value_start = endp = memchr (cur, '\n', file_end - cur);
          if (!endp)
              endp = cur;
          while (endp > cur && ISSPACE (endp[-1]))
            --endp;
          var_len = endp - cur;
          if (!var_len)
          {
              incdep_warn (curdep, line_no, "bogus define statement.");
              break;
          }
          var = incdep_var_strcache (curdep, cur, var_len);

          /* find the end of the variable. */
          cur = value_end = value_start = value_start + 1;
          ++line_no;
          while ((uintptr_t)cur < (uintptr_t)file_end)
            {
              /* check for endef, don't bother with skipping leading spaces. */
              if (   file_end - cur >= 5
                  && strneq (cur, "endef", 5))
                {
                  endp = cur + 5;
                  while ((uintptr_t)endp < (uintptr_t)file_end && ISSPACE (*endp) && *endp != '\n')
                    endp++;
                  if ((uintptr_t)endp >= (uintptr_t)file_end || *endp == '\n')
                    {
                      found_endef = 1;
                      cur = (uintptr_t)endp >= (uintptr_t)file_end ? file_end : endp + 1;
                      break;
                    }
                }

              /* skip a line ahead. */
              cur = value_end = memchr (cur, '\n', file_end - cur);
              if (cur != NULL)
                ++cur;
              else
                cur = value_end = file_end;
              ++line_no;
            }

          if (!found_endef)
            {
              incdep_warn (curdep, line_no, "missing endef, dropping the rest of the file.");
              break;
            }
          value_len = value_end - value_start;
          if (memchr (value_start, '\0', value_len))
            {
              incdep_warn (curdep, line_no, "'\\0' in define, dropping the rest of the file.");
              break;
            }

          /* make a copy of the value, converting \r\n to \n, and define it. */
          value = incdep_xmalloc (curdep, value_len + 1);
          endp = memchr (value_start, '\r', value_len);
          if (endp)
            {
              const char *src = value_start;
              char *dst = value;
              for (;;)
                {
                  size_t len = endp - src;
                  memcpy (dst, src, len);
                  dst += len;
                  src = endp;
                  if ((uintptr_t)src + 1 < (uintptr_t)file_end && src[1] == '\n')
                      src++; /* skip the '\r' */
                  if (src >= value_end)
                    break;
                  endp = memchr (endp + 1, '\r', src - value_end);
                  if (!endp)
                    endp = value_end;
                }
              value_len = dst - value;
            }
          else
            memcpy (value, value_start, value_len);
          value [value_len] = '\0';

          incdep_record_variable_in_set (curdep,
                                         var, var_len, value, value_len,
                                         0 /* don't duplicate */, o_file,
                                         0 /* defines are recursive but this is faster */,
                                         NULL /* global set */, f);
        }

      /* file: deps
         OR
         variable [:]= value */
      else
        {
          const char *equalp;
          const char *eol;

          /* Look for a colon or an equal sign.  In the assignment case, we
             require it to be on the same line as the variable name to simplify
             the code.  Because of clang, we cannot make the same assumptions
             with file dependencies.  So, start with the equal. */

          assert (*cur != '\n');
          eol = memchr (cur, '\n', file_end - cur);
          if (!eol)
            eol = file_end;
          equalp = memchr (cur, '=', eol - cur);
          if (equalp && equalp != cur && (ISSPACE(equalp[-1]) || equalp[-1] != '\\'))
            {
              /* An assignment of some sort. */
              const char *var;
              unsigned var_len;
              const char *value_start;
              const char *value_end;
              char *value;
              unsigned value_len;
              unsigned multi_line = 0;
              enum variable_flavor flavor;

              /* figure the flavor first. */
              flavor = f_recursive;
              if (equalp > cur)
                {
                  if (equalp[-1] == ':')
                    flavor = f_simple;
                  else if (equalp[-1] == '?')
                    flavor = f_conditional;
                  else if (equalp[-1] == '+')
                    flavor = f_append;
                  else if (equalp[-1] == '>')
                    flavor = f_prepend;
                }

              /* extract the variable name. */
              endp = flavor == f_recursive ? equalp : equalp - 1;
              while (endp > cur && ISBLANK (endp[-1]))
                --endp;
              var_len = endp - cur;
              if (!var_len)
                {
                  incdep_warn (curdep, line_no, "empty variable. (includedep)");
                  break;
                }
              if (   memchr (cur, '$', var_len)
                  || memchr (cur, ' ', var_len)
                  || memchr (cur, '\t', var_len))
                {
                  incdep_warn (curdep, line_no, "fancy variable name. (includedep)");
                  break;
                }
              var = incdep_var_strcache (curdep, cur, var_len);

              /* find the start of the value. */
              cur = equalp + 1;
              while ((uintptr_t)cur < (uintptr_t)file_end && ISBLANK (*cur))
                cur++;
              value_start = cur;

              /* find the end of the value / line (this isn't 101% correct). */
              value_end = cur;
              while ((uintptr_t)cur < (uintptr_t)file_end)
                {
                  endp = value_end = memchr (cur, '\n', file_end - cur);
                  if (!value_end)
                    value_end = file_end;
                  if (value_end - 1 >= cur && value_end[-1] == '\r')
                    --value_end;
                  if (value_end - 1 < cur || value_end[-1] != '\\')
                    {
                      cur = endp ? endp + 1 : file_end;
                      break;
                    }
                  --value_end;
                  if (value_end - 1 >= cur && value_end[-1] == '\\')
                    {
                      incdep_warn (curdep, line_no, "fancy escaping! (includedep)");
                      cur = NULL;
                      break;
                    }
                  if (!endp)
                    {
                      cur = file_end;
                      break;
                    }

                  cur = endp + 1;
                  ++multi_line;
                  ++line_no;
                }
              if (!cur)
                break;
              ++line_no;

              /* make a copy of the value, converting \r\n to \n, and define it. */
              value_len = value_end - value_start;
              value = incdep_xmalloc (curdep, value_len + 1);
              if (!multi_line)
                  memcpy (value, value_start, value_len);
              else
                {
                  /* unescape it */
                  const char *src = value_start;
                  char *dst = value;
                  while (src < value_end)
                    {
                      const char *nextp;

                      endp = memchr (src, '\n', value_end - src);
                      if (!endp)
                        nextp = endp = value_end;
                      else
                        nextp = endp + 1;
                      if (endp > src && endp[-1] == '\r')
                        --endp;
                      if (endp > src && endp[-1] == '\\')
                        --endp;

                      if (src != value_start)
                        *dst++ = ' ';
                      memcpy (dst, src, endp - src);
                      dst += endp - src;
                      src = nextp;
                    }
                  value_len = dst - value;
                }
              value [value_len] = '\0';

              /* do the definition */
              if (flavor == f_recursive
               || (   flavor == f_simple
                   && !memchr (value, '$', value_len)))
                incdep_record_variable_in_set (curdep,
                                               var, var_len, value, value_len,
                                               0 /* don't duplicate */, o_file,
                                               flavor == f_recursive /* recursive */,
                                               NULL /* global set */, f);
              else
                incdep_record_variable_def (curdep,
                                            f, var, var_len, value, value_len,
                                            o_file, flavor, 0 /* not target var */);
            }
          else
            {
              /* Expecting: file: dependencies */

              int unescape_filename = 0;
              const char *filename;
              const char *fnnext;
              const char *fnend;
              const char *colonp;
              struct dep *deps = 0;
              struct dep **nextdep = &deps;
              struct dep *dep;


              /* Locate the next file colon.  If it's not within the bounds of
                 the current line, check that all new line chars are escaped. */

              colonp = memchr (cur, ':', file_end - cur);
              while (   colonp
                     && (   (   colonp != cur
                             && colonp[-1] == '\\'
                             && incdep_count_slashes_backwards (&colonp[-1], cur) & 1)
#ifdef HAVE_DOS_PATHS
                         || (   colonp + 1 < file_end
                             && (colonp[1] == '/' || colonp[1] == '\\')
                             && colonp > cur
                             && isalpha ((unsigned char)colonp[-1])
                             && (   colonp == cur + 1
                                 || ISBLANK ((unsigned char)colonp[-2])))
#endif
                        )
                    )
                  colonp = memchr (colonp + 1, ':', file_end - (colonp + 1));
              if (!colonp)
                {
                  incdep_warn (curdep, line_no, "no colon.");
                  break;
                }

              if ((uintptr_t)colonp < (uintptr_t)eol)
                  unescape_filename = memchr (cur, '\\', colonp - cur) != NULL
                                   || memchr (cur, '$', colonp - cur) != NULL;
              else if (memchr (eol, '=', colonp - eol))
                {
                  incdep_warn (curdep, line_no, "multi line assignment / dependency confusion.");
                  break;
                }
              else
                {
                  const char *sol = cur;
                  do
                    {
                      char *eol2 = (char *)eol - 1;
                      if ((uintptr_t)eol2 >= (uintptr_t)sol && *eol2 == '\r') /* DOS line endings. */
                        eol2--;
                      if ((uintptr_t)eol2 < (uintptr_t)sol || *eol2 != '\\')
                          incdep_warn (curdep, line_no, "no colon.");
                      else if (eol2 != sol && eol2[-1] == '\\')
                          incdep_warn (curdep, line_no, "fancy EOL escape. (includedep)");
                      else
                        {
                          line_no++;
                          sol = eol + 1;
                          eol = memchr (sol, '\n', colonp - sol);
                          continue;
                        }
                      sol = NULL;
                      break;
                    }
                  while (eol != NULL);
                  if (!sol)
                    break;
                  unescape_filename = 1;
                }

              /* Extract the first filename after trimming and basic checks. */
              fnend = colonp;
              while ((uintptr_t)fnend > (uintptr_t)cur && ISBLANK (fnend[-1]))
                --fnend;
              if (cur == fnend)
                {
                  incdep_warn (curdep, line_no, "empty filename.");
                  break;
                }
              fnnext = cur;
              if (!unescape_filename)
                {
                  while (fnnext != fnend && !ISBLANK (*fnnext))
                    fnnext++;
                  filename = incdep_dep_strcache (curdep, cur, fnnext - cur);
                }
              else
                filename = incdep_unescape_and_cache_filename (curdep, (char *)fnnext, fnend, 0, &fnnext, NULL);

              /* parse any dependencies. */
              cur = colonp + 1;
              while ((uintptr_t)cur < (uintptr_t)file_end)
                {
                  const char *dep_file;

                  /* skip blanks and count lines. */
                  char ch = 0;
                  while ((uintptr_t)cur < (uintptr_t)file_end && ISSPACE ((ch = *cur)) && ch != '\n')
                    ++cur;
                  if ((uintptr_t)cur >= (uintptr_t)file_end)
                    break;
                  if (ch == '\n')
                    {
                      cur++;
                      line_no++;
                      break;
                    }

                  /* continuation + eol? */
                  if (ch == '\\')
                    {
                      unsigned eol_len = (file_end - cur > 1 && cur[1] == '\n') ? 2
                                       : (file_end - cur > 2 && cur[1] == '\r' && cur[2] == '\n') ? 3
                                       : (file_end - cur == 1) ? 1 : 0;
                      if (eol_len)
                        {
                          cur += eol_len;
                          line_no++;
                          continue;
                        }
                    }

                  /* find the end of the filename and cache it */
                  dep_file = NULL;
                  endp = cur;
                  for (;;)
                    if ((uintptr_t)endp < (uintptr_t)file_end)
                      {
                         ch = *endp;
                         if (ch != '\\' && ch != '$' )
                           {
                             if (!ISSPACE (ch))
                               endp++;
                             else
                               {
                                 dep_file = incdep_dep_strcache(curdep, cur, endp - cur);
                                 break;
                               }
                           }
                         else
                           {
                             /* potential escape sequence, let the unescaper do the rest. */
                             dep_file = incdep_unescape_and_cache_filename (curdep, (char *)cur, file_end, 1, &endp, &line_no);
                             break;
                           }
                      }
                    else
                      {
                        dep_file = incdep_dep_strcache(curdep, cur, endp - cur);
                        break;
                      }

                  /* add it to the list. */
                  *nextdep = dep = incdep_alloc_dep (curdep);
                  dep->includedep = 1;
                  dep->name = dep_file;
                  nextdep = &dep->next;

                  cur = endp;
                }

              /* enter the file with its dependencies. */
              incdep_record_file (curdep, filename, deps, f);

              /* More files? Record them with the same dependency list. */
              if ((uintptr_t)fnnext < (uintptr_t)fnend)
                for (;;)
                  {
                    const char *filename_prev = filename;
                    while (fnnext != fnend && ISBLANK (*fnnext))
                      fnnext++;
                    if (fnnext == fnend)
                      break;
                    if (*fnnext == '\\')
                      {
                        if (fnnext[1] == '\n')
                          {
                            line_no++;
                            fnnext += 2;
                            continue;
                          }
                        if (fnnext[1] == '\r' && fnnext[2] == '\n')
                          {
                            line_no++;
                            fnnext += 3;
                            continue;
                          }
                      }

                    if (!unescape_filename)
                      {
                        const char *fnstart = fnnext;
                        while (fnnext != fnend && !ISBLANK (*fnnext))
                          fnnext++;
                        filename = incdep_dep_strcache (curdep, fnstart, fnnext - fnstart);
                      }
                    else
                      filename = incdep_unescape_and_cache_filename (curdep, (char *)fnnext, fnend, 0, &fnnext, NULL);
                    if (filename != filename_prev) /* clang optimization, it emit two targets for each dependency list. */
                      incdep_record_file (curdep, filename, incdep_dup_dep_list (curdep, deps), f);
                  }
            }
        }
    }

  /* free the file data */
  incdep_xfree (curdep, curdep->file_base);
  curdep->file_base = curdep->file_end = NULL;
}

/* Flushes the incdep todo and done lists. */
static void
incdep_flush_it (floc *f)
{
  struct incdep_worker_data *thrd_data = &incdep_worker_data[INCDEP_MAX_THREADS];

  incdep_lock ();
  for (;;)
    {
      struct incdep *cur   = incdep_head_done;
      unsigned int   count = incdep_count_done;

      /* if the done list is empty, grab a todo list entry. */
      if (!cur)
        {
          cur = incdep_head_todo;
          if (cur)
            {
              if (cur->next)
                {
                  assert (incdep_count_todo > 1);
                  assert (cur != incdep_tail_todo);
                  incdep_head_todo = cur->next;
                }
              else
                {
                  assert (incdep_count_todo == 1);
                  assert (cur == incdep_tail_todo);
                  incdep_head_todo = incdep_tail_todo = NULL;
                }
              incdep_count_todo--;
              thrd_data->todo_count--;
              incdep_unlock ();

              incdep_read_file (cur, f);
              eval_include_dep_file (cur, f);
              incdep_freeit (cur);

              incdep_lock ();
              continue;
            }
        }

      /* if the todo list and done list are empty we're either done
         or will have to wait for the thread(s) to finish. */
      if (!cur && !incdep_num_reading)
          break; /* done */
      if (!cur)
        {
          while (!incdep_head_done)
            incdep_wait_done ();
          cur = incdep_head_done;
        }

      /* we grab the entire done list and work thru it. */
      incdep_head_done = incdep_tail_done = NULL;
      incdep_count_done = 0;

      incdep_unlock ();

      while (cur)
        {
          struct incdep *next = cur->next;
          assert (count > 0);
#ifdef PARSE_IN_WORKER
          incdep_flush_recorded_instructions (cur);
#else
          eval_include_dep_file (cur, f);
#endif
          incdep_freeit (cur);
          thrd_data->flushed_count++;
          count--;
          cur = next;
        }
      assert (count == 0);

      incdep_lock ();
    } /* outer loop */
  incdep_unlock ();
}


/* splits up a list of file names and feeds it to eval_include_dep_file,
   employing threads to try speed up the file reading. */
void
eval_include_dep (const char *names, floc *f, enum incdep_op op)
{
  struct incdep_worker_data *thrd_data = &incdep_worker_data[INCDEP_MAX_THREADS];
  struct incdep *head = 0;
  struct incdep *tail = 0;
  struct incdep *cur;
  unsigned int count = 0;
  const char *names_iterator = names;
  const char *name;
  unsigned int name_len;

#if 0 /* sanity / debug */
  {
    struct incdep *check, *checktail;
    unsigned items;
    for (items = 0, check = checktail = incdep_head_todo; check; check = check->next)
      checktail = check, items++;
    MY_ASSERT (items == incdep_count_todo);
    MY_ASSERT (checktail == incdep_tail_todo);
  }
#endif

  /* loop through NAMES, creating a todo list out of them. */

  while ((name = find_next_token (&names_iterator, &name_len)) != 0)
    {
#ifdef INCDEP_USE_KFSCACHE
       KFSLOOKUPERROR enmError;
       PKFSOBJ pFileObj = kFsCacheLookupWithLengthA (g_pFsCache, name, name_len, &enmError);
       if (!pFileObj)
         continue;
       if (pFileObj->bObjType != KFSOBJ_TYPE_FILE)
         {
           kFsCacheObjRelease (g_pFsCache, pFileObj);
           continue;
         }

       cur = xmalloc (sizeof (*cur));            /* not incdep_xmalloc here */
       cur->pFileObj = pFileObj;
#else
       cur = xmalloc (sizeof (*cur) + name_len); /* not incdep_xmalloc here */
       memcpy (cur->name, name, name_len);
       cur->name[name_len] = '\0';
#endif

       cur->file_base = cur->file_end = NULL;
       cur->worker_tid = -1;
#ifdef PARSE_IN_WORKER
       cur->err_line_no = 0;
       cur->err_msg = NULL;
       cur->recorded_variables_in_set_head = NULL;
       cur->recorded_variables_in_set_tail = NULL;
       cur->recorded_variable_defs_head = NULL;
       cur->recorded_variable_defs_tail = NULL;
       cur->recorded_file_head = NULL;
       cur->recorded_file_tail = NULL;
#endif

       cur->next = NULL;
       if (tail)
         tail->next = cur;
       else
         head = cur;
       tail = cur;
       count++;
    }

#ifdef ELECTRIC_HEAP
  if (1)
#else
  if (op == incdep_read_it)
#endif
    {
      /* work our way thru the files directly */

      cur = head;
      while (cur)
        {
          struct incdep *next = cur->next;
          incdep_read_file (cur, f);
          eval_include_dep_file (cur, f);
          incdep_freeit (cur);
          cur = next;
        }
    }
  else
    {
      if (head)
        {
          struct incdep *tmp;

          /* initialize the worker threads and related stuff the first time around. */

          if (!incdep_initialized)
            incdep_init (f);

          /* queue the files and notify the worker threads. */

          incdep_lock ();

          tmp = incdep_tail_todo;
          if (tmp)
            {
              assert (incdep_count_todo > 0);
              assert (incdep_head_todo != NULL);
              tmp->next = head;
            }
          else
            {
              assert (incdep_count_todo == 0);
              assert (incdep_head_todo == NULL);
              incdep_head_todo = head;
            }
          incdep_tail_todo = tail;
          incdep_count_todo += count;
          thrd_data->todo_count += count;

#if 0 /* sanity / debug */
          {
            struct incdep *check, *checktail;
            unsigned items;
            for (items = 0, check = checktail = incdep_head_todo; check; check = check->next)
              checktail = check, items++;
            MY_ASSERT (items == incdep_count_todo);
            MY_ASSERT (checktail == incdep_tail_todo);
          }
#endif

          incdep_signal_todo ();
          incdep_unlock ();
        }

        /* flush the todo queue if we're requested to do so. */

        if (op == incdep_flush)
          incdep_flush_it (f);
    }
}

#endif /* CONFIG_WITH_INCLUDEDEP */

