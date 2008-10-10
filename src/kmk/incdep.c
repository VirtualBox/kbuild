#ifdef CONFIG_WITH_INCLUDEDEP
/* $Id: incdep.c 1825 2008-10-10 21:04:14Z knut.osmundsen@oracle.com $ */
/** @file
 * incdep - Simple dependency files.
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


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#ifdef __OS2__
# define INCL_BASE
# define INCL_ERRORS
#endif

#include "make.h"

#if !defined(WINDOWS32) && !defined(__OS2__)
# define HAVE_PTHREAD
#endif

#include <assert.h>

#include <glob.h>

#include "dep.h"
#include "filedef.h"
#include "job.h"
#include "commands.h"
#include "variable.h"
#include "rule.h"
#include "debug.h"
#include "hash.h"

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

#ifdef __OS2__
# include <os2.h>
# include <sys/fmutex.h>
#endif

#ifdef HAVE_PTHREAD
# include <pthread.h>
#endif


/*******************************************************************************
*   Structures and Typedefs                                                    *
*******************************************************************************/

struct incdep_variable_in_set
{
    struct incdep_variable_in_set *next;
    /* the parameters */
    char *name;                     /* xmalloc'ed -> strcache */
    unsigned int name_length;
    const char *value;              /* xmalloc'ed */
    unsigned int value_length;
    int duplicate_value;            /* 0 */
    enum variable_origin origin;
    int recursive;
    struct variable_set *set;
    const struct floc *flocp;       /* NILF */
};

struct incdep_variable_def
{
    struct incdep_variable_def *next;
    /* the parameters */
    const struct floc *flocp;       /* NILF */
    char *name;                     /* xmalloc'ed -> strcache */
    unsigned int name_length;       /* (not an actual parameter) */
    char *value;                    /* xmalloc'ed, free it */
    unsigned int value_length;
    enum variable_origin origin;
    enum variable_flavor flavor;
    int target_var;
};

struct incdep_recorded_files
{
    struct incdep_recorded_files *next;

    /* the parameters */
    struct nameseq *filenames;      /* only one file? its name needs be strcache'ed */
    const char *pattern;            /* NULL */
    const char *pattern_percent;    /* NULL */
    struct dep *deps;               /* names need to be strcache'ed */
    unsigned int cmds_started;      /* 0 */
    char *commands;                 /* NULL */
    unsigned int commands_idx;      /* 0 */
    int two_colon;                  /* 0 */
    const struct floc *flocp;       /* NILF */
};


/* per dep file structure. */
struct incdep
{
  struct incdep *next;
  char *file_base;
  char *file_end;

  int is_worker;
#ifdef PARSE_IN_WORKER
  unsigned int err_line_no;
  const char *err_msg;

  struct incdep_variable_in_set *recorded_variables_in_set_head;
  struct incdep_variable_in_set *recorded_variables_in_set_tail;

  struct incdep_variable_def *recorded_variable_defs_head;
  struct incdep_variable_def *recorded_variable_defs_tail;

  struct incdep_recorded_files *recorded_files_head;
  struct incdep_recorded_files *recorded_files_tail;
#endif

  char name[1];
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
static fmutex incdep_mtx;
static HEV incdep_hev_todo;
static HEV incdep_hev_done;
static int volatile incdep_hev_todo_waiters;
static int volatile incdep_hev_done_waiters;
#endif

/* flag indicating whether the threads, lock and event/condvars has
   been initialized or not. */
static int incdep_initialized;

/* the list of files that needs reading. */
static struct incdep * volatile incdep_head_todo;
static struct incdep * volatile incdep_tail_todo;

/* the number of files that are currently being read. */
static int volatile incdep_num_reading;

/* the list of files that have been read. */
static struct incdep * volatile incdep_head_done;
static struct incdep * volatile incdep_tail_done;

/* The handles to the worker threads. */
#ifdef HAVE_PTHREAD
static pthread_t incdep_threads[2];
#elif defined (WINDOWS32)
static HANDLE incdep_threads[2];
#elif defined (__OS2__)
static TID incdep_threads[2];
#endif
static unsigned incdep_num_threads;

/* flag indicating whether the worker threads should terminate or not. */
static int volatile incdep_terminate;


/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/
static void incdep_flush_it (struct floc *);
static void eval_include_dep_file (struct incdep *, struct floc *, int);



/* acquires the lock */
void
incdep_lock(void)
{
#ifdef HAVE_PTHREAD
  pthread_mutex_lock (&incdep_mtx);
#elif defined (WINDOWS32)
  EnterCriticalSection (&incdep_mtx);
#elif defined (__OS2__)
  _fmutex_request (&incdep_mtx, 0)
#endif
}

/* releases the lock */
void
incdep_unlock(void)
{
#ifdef HAVE_PTHREAD
  pthread_mutex_unlock (&incdep_mtx);
#elif defined(WINDOWS32)
  LeaveCriticalSection (&incdep_mtx);
#elif defined(__OS2__)
  _fmutex_release (&incdep_mtx)
#endif
}

/* signals the main thread that there is stuff todo. caller owns the lock. */
static void
incdep_signal_done (void)
{
#ifdef HAVE_PTHREAD
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
#ifdef HAVE_PTHREAD
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
#ifdef HAVE_PTHREAD
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
#ifdef HAVE_PTHREAD
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
incdep_read_file (struct incdep *cur, struct floc *f)
{
  int fd;
  struct stat st;

  errno = 0;
#ifdef O_BINARY
  fd = open (cur->name, O_RDONLY | O_BINARY, 0);
#else
  fd = open (cur->name, O_RDONLY, 0);
#endif
  if (fd < 0)
    {
      /* ignore non-existing dependency files. */
      int err = errno;
      if (err == ENOENT || stat (cur->name, &st) != 0)
        return 1;
      error (f, "%s: %s", cur->name, strerror (err));
      return -1;
    }
  if (!fstat (fd, &st))
    {
      cur->file_base = xmalloc (st.st_size + 1);
      if (read (fd, cur->file_base, st.st_size) == st.st_size)
        {
          close (fd);
          cur->file_end = cur->file_base + st.st_size;
          cur->file_base[st.st_size] = '\0';
          return 0;
        }

      /* bail out */

      error (f, "%s: read: %s", cur->name, strerror (errno));
      free (cur->file_base);
    }
  else
    error (f, "%s: fstat: %s", cur->name, strerror (errno));

  close (fd);
  cur->file_base = cur->file_end = NULL;
  return -1;
}

/* Free the incdep structure. */
static void
incdep_free (struct incdep *cur)
{
#ifdef PARSE_IN_WORKER
  assert (!cur->recorded_variables_in_set_head);
  assert (!cur->recorded_variable_defs_head);
  assert (!cur->recorded_files_head);
#endif

  free (cur->file_base);
  free (cur);
}

/* A worker thread. */
void
incdep_worker (void)
{
  incdep_lock ();

  while (!incdep_terminate)
   {
      /* get job from the todo list. */

      struct incdep *cur = incdep_head_todo;
      if (!cur)
        {
          incdep_wait_todo ();
          continue;
        }
      if (cur->next)
        incdep_head_todo = cur->next;
      else
        incdep_head_todo = incdep_tail_todo = NULL;
      incdep_num_reading++;

      /* read the file. */

      incdep_unlock ();
      incdep_read_file (cur, NILF);
#ifdef PARSE_IN_WORKER
      eval_include_dep_file (cur, NILF, 1 /* is_worker */);
#endif
      incdep_lock ();

      /* insert finished job into the done list. */

      incdep_num_reading--;
      cur->next = NULL;
      if (incdep_tail_done)
        incdep_tail_done->next = cur;
      else
        incdep_head_done = cur;
      incdep_tail_done = cur;

      incdep_signal_done ();
   }

  incdep_unlock ();
}

/* Thread library specific thread functions wrapping incdep_wroker. */
#ifdef HAVE_PTHREAD
static void *
incdep_worker_pthread (void *ignore)
{
  incdep_worker ();
  (void)ignore;
  return NULL;
}

#elif defined (WINDOWS32)
static unsigned __stdcall
incdep_worker_windows (void *ignore)
{
  incdep_worker ();
  (void)ignore;
  return 0;
}

#elif defined (__OS2__)
static void
incdep_worker_os2 (void *ignore)
{
  incdep_worker ();
  (void)ignore;
}
#endif

/* Creates the the worker threads. */
static void
incdep_init (struct floc *f)
{
  unsigned i;
#ifdef HAVE_PTHREAD
  int rc;
  pthread_attr_t attr;

#elif defined (WINDOWS32)
  unsigned tid;
  uintptr_t hThread;

#elif defined (__OS2__)
  int rc;
  int tid;
#endif

  /* create the mutex and two condition variables / event objects. */

#ifdef HAVE_PTHREAD
  rc = pthread_mutex_init (&incdep_mtx, NULL);
  if (rc)
    fatal (f, _("pthread_mutex_init failed: err=%d"), rc);
  rc = pthread_cond_init (&incdep_cond_todo, NULL);
  if (rc)
    fatal (f, _("pthread_cond_init failed: err=%d"), rc);
  rc = pthread_cond_init (&incdep_cond_done, NULL);
  if (rc)
    fatal (f, _("pthread_cond_init failed: err=%d"), rc);

#elif defined (WINDOWS32)
  InitializeCriticalSection (&incdep_mtx);
  incdep_hev_todo = CreateEvent (NULL, TRUE /*bManualReset*/, FALSE /*bInitialState*/, NULL);
  if (!incdep_hev_todo)
    fatal (f, _("CreateEvent failed: err=%d"), GetLastError());
  incdep_hev_done = CreateEvent (NULL, TRUE /*bManualReset*/, FALSE /*bInitialState*/, NULL);
  if (!incdep_hev_done)
    fatal (f, _("CreateEvent failed: err=%d"), GetLastError());
  incdep_hev_todo_waiters = 0;
  incdep_hev_done_waiters = 0;

#elif defined (__OS2__)
  _fmutex_create (&incdep_mtx, 0)
  rc = DosCreateEventSem (NULL, &incdep_hev_todo, 0, FALSE);
  if (rc)
    fatal (f, _("DosCreateEventSem failed: rc=%d"), rc);
  rc = DosCreateEventSem (NULL, &incdep_hev_done, 0, FALSE);
  if (rc)
    fatal (f, _("DosCreateEventSem failed: rc=%d"), rc);
  incdep_hev_todo_waiters = 0;
  incdep_hev_done_waiters = 0;
#endif

  /* create the worker threads. */

  incdep_terminate = 0;
  incdep_num_threads = sizeof (incdep_threads) / sizeof (incdep_threads[0]);
  if (incdep_num_threads + 1 > job_slots)
    incdep_num_threads = job_slots <= 1 ? 1 : job_slots - 1;
  for (i = 0; i < incdep_num_threads; i++)
    {
#ifdef HAVE_PTHREAD
      rc = pthread_attr_init (&attr);
      if (rc)
        fatal (f, _("pthread_attr_init failed: err=%d"), rc);
      /*rc = pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_JOINABLE); */
      rc = pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_DETACHED);
      if (rc)
        fatal (f, _("pthread_attr_setdetachstate failed: err=%d"), rc);
      rc = pthread_create (&incdep_threads[i], &attr,
                           incdep_worker_pthread, f);
      if (rc)
        fatal (f, _("pthread_mutex_init failed: err=%d"), rc);
      pthread_attr_destroy (&attr);

#elif defined (WINDOWS32)
      tid = 0;
      hThread = _beginthreadex (NULL, 128*1024, incdep_worker_windows,
                                NULL, 0, &tid);
      if (hThread == 0 || hThread == ~(uintptr_t)0)
        fatal (f, _("_beginthreadex failed: err=%d"), errno);
      incdep_threads[i] = (HANDLE)hThread;

#elif defined (__OS2__)
      tid = _beginthread (incdep_worker_os2, NULL, 128*1024, NULL);
      if (tid <= 0)
        fatal (f, _("_beginthread failed: err=%d"), errno);
      incdep_threads[i] = tid;
#endif
    }

  incdep_initialized = 1;
}

/* Flushes outstanding work and terminates the worker threads.
   This is called from snap_deps(). */
void
incdep_flush_and_term (void)
{
  unsigned i;

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
      /* later? */
    }
  incdep_num_threads = 0;

  /* destroy the lock and condition variables / event objects. */

  /* later */

  incdep_initialized = 0;
}

#ifdef PARSE_IN_WORKER
/* Flushes the recorded instructions. */
static void
incdep_flush_recorded_instructions (struct incdep *cur)
{
  struct incdep_variable_in_set *rec_vis;
  struct incdep_variable_def *rec_vd;
  struct incdep_recorded_files *rec_f;

  /* define_variable_in_set */

  rec_vis = cur->recorded_variables_in_set_head;
  cur->recorded_variables_in_set_head = cur->recorded_variables_in_set_tail = NULL;
  if (rec_vis)
    do
      {
        void *free_me = rec_vis;
        define_variable_in_set (strcache_add_len (rec_vis->name, rec_vis->name_length),
                                rec_vis->name_length,
                                rec_vis->value,
                                rec_vis->value_length,
                                rec_vis->duplicate_value,
                                rec_vis->origin,
                                rec_vis->recursive,
                                rec_vis->set,
                                rec_vis->flocp);
        free (rec_vis->name);
        rec_vis = rec_vis->next;
        free (free_me);
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
                                  strcache_add_len(rec_vd->name, rec_vd->name_length),
                                  rec_vd->value,
                                  rec_vd->value_length,
                                  0,
                                  rec_vd->value,
                                  rec_vd->origin,
                                  rec_vd->flavor,
                                  rec_vd->target_var);
        free (rec_vd->name);
        rec_vd = rec_vd->next;
        free (free_me);
      }
    while (rec_vd);

  /* record_files */

  rec_f = cur->recorded_files_head;
  cur->recorded_files_head = cur->recorded_files_tail = NULL;
  if (rec_f)
    do
      {
        void *free_me = rec_f;
        struct dep *dep;
        const char *newname;

        for (dep = rec_f->deps; dep; dep = dep->next)
          {
            newname = strcache_add (dep->name);
            free ((char *)dep->name);
            dep->name = newname;
          }

        newname = strcache_add (rec_f->filenames->name);
        free ((char *)rec_f->filenames->name);
        rec_f->filenames->name = newname;

        record_files (rec_f->filenames,
                      rec_f->pattern,
                      rec_f->pattern_percent,
                      rec_f->deps,
                      rec_f->cmds_started,
                      rec_f->commands,
                      rec_f->commands_idx,
                      rec_f->two_colon,
                      rec_f->flocp);

        rec_f = rec_f->next;
        free (free_me);
      }
    while (rec_f);
}
#endif /* PARSE_IN_WORKER */

/* Record / issue a warning about a misformed dep file. */
static void
incdep_warn (struct incdep *cur, unsigned int line_no, const char *msg)
{
  if (!cur->is_worker)
    error (NILF, "%s(%d): %s", cur->name, line_no, msg);
#ifdef PARSE_IN_WORKER
  else
    {
      cur->err_line_no = line_no;
      cur->err_msg = msg;
    }
#endif
}

/* Record / execute a strcache add. */
static const char *
incdep_record_strcache (struct incdep *cur, const char *str, int len)
{
  const char *ret;
  if (!cur->is_worker)
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
      /* Duplicate the string. The other recorders knows which arguments
         needs to be added to the string cache later. */
      char *newstr = xmalloc (len + 1);
      memcpy (newstr, str, len);
      newstr[len] = '\0';
      ret = newstr;
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
                               const struct floc *flocp)
{
  assert (!duplicate_value);
  if (!cur->is_worker)
    define_variable_in_set (name, name_length, value, value_length,
                            duplicate_value, origin, recursive, set, flocp);
#ifdef PARSE_IN_WORKER
  else
    {
      struct incdep_variable_in_set *rec = xmalloc (sizeof (*rec));
      rec->name = (char *)name;
      rec->name_length = name_length;
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
                            const struct floc *flocp,
                            const char *name,
                            unsigned int name_length,
                            char *value,
                            unsigned int value_length,
                            enum variable_origin origin,
                            enum variable_flavor flavor,
                            int target_var)
{
  if (!cur->is_worker)
    do_variable_definition_2 (flocp, name, value, value_length, 0, value,
                              origin, flavor, target_var);
#ifdef PARSE_IN_WORKER
  else
    {
      struct incdep_variable_def *rec = xmalloc (sizeof (*rec));
      rec->flocp = flocp;
      rec->name = (char *)name;
      rec->name_length = name_length;
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

/* Record files.*/
static void
incdep_record_files (struct incdep *cur,
                     struct nameseq *filenames, const char *pattern,
                     const char *pattern_percent, struct dep *deps,
                     unsigned int cmds_started, char *commands,
                     unsigned int commands_idx, int two_colon,
                     const struct floc *flocp)
{
  if (!cur->is_worker)
    record_files (filenames, pattern, pattern_percent, deps, cmds_started,
                  commands, commands_idx, two_colon, flocp);
#ifdef PARSE_IN_WORKER
  else
    {
      struct incdep_recorded_files *rec = xmalloc (sizeof (*rec));

      rec->filenames = filenames;
      rec->pattern = pattern;
      rec->pattern_percent = pattern_percent;
      rec->deps = deps;
      rec->cmds_started = cmds_started;
      rec->commands = commands;
      rec->commands_idx = commands_idx;
      rec->two_colon = two_colon;
      rec->flocp = flocp;

      rec->next = NULL;
      if (cur->recorded_files_tail)
        cur->recorded_files_tail->next = rec;
      else
        cur->recorded_files_head = rec;
      cur->recorded_files_tail = rec;
    }
#endif
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
eval_include_dep_file (struct incdep *curdep, struct floc *f, int is_worker)
{
  unsigned line_no = 1;
  const char *file_end = curdep->file_end;
  const char *cur = curdep->file_base;
  const char *endp;

  /* if no file data, just return immediately. */
  if (!cur)
    return;
  curdep->is_worker = is_worker;

  /* now parse the file. */
  while (cur < file_end)
    {
      /* skip empty lines */
      while (cur < file_end && isspace ((unsigned char)*cur) && *cur != '\n')
        ++cur;
      if (cur >= file_end)
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
          while (isblank ((unsigned char)*cur))
            ++cur;
          value_start = endp = memchr (cur, '\n', file_end - cur);
          if (!endp)
              endp = cur;
          while (endp > cur && isspace ((unsigned char)endp[-1]))
            --endp;
          var_len = endp - cur;
          if (!var_len)
          {
              incdep_warn (curdep, line_no, "bogus define statement.");
              break;
          }
          var = incdep_record_strcache (curdep, cur, var_len);

          /* find the end of the variable. */
          cur = value_end = value_start = value_start + 1;
          ++line_no;
          while (cur < file_end)
            {
              /* check for endef, don't bother with skipping leading spaces. */
              if (   file_end - cur >= 5
                  && strneq (cur, "endef", 5))
                {
                  endp = cur + 5;
                  while (endp < file_end && isspace ((unsigned char)*endp) && *endp != '\n')
                    endp++;
                  if (endp >= file_end || *endp == '\n')
                    {
                      found_endef = 1;
                      cur = endp >= file_end ? file_end : endp + 1;
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
          value = xmalloc (value_len + 1);
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
                  if (src + 1 < file_end && src[1] == '\n')
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
          const char *colonp;
          const char *equalp;

          /* Look for a colon and an equal sign, optimize for colon.
             Only one file is support and the colon / equal must be on
             the same line. */
          colonp = memchr (cur, ':', file_end - cur);
#ifdef HAVE_DOS_PATHS
          while (   colonp
                 && colonp + 1 < file_end
                 && (colonp[1] == '/' || colonp[1] == '\\')
                 && colonp > cur
                 && isalpha ((unsigned char)colonp[-1])
                 && (   colonp == cur + 1
                     || strchr (" \t(", colonp[-2]) != 0))
              colonp = memchr (colonp + 1, ':', file_end - (colonp + 1));
#endif
          endp = NULL;
          if (   !colonp
              ||  (endp = memchr (cur, '\n', colonp - cur)))
            {
              colonp = NULL;
              equalp = memchr (cur, '=', (endp ? endp : file_end) - cur);
              if (   !equalp
                  || (!endp && memchr (cur, '\n', equalp - cur)))
                {
                  incdep_warn (curdep, line_no, "no colon.");
                  break;
                }
            }
          else
            equalp = memchr (cur, '=', (colonp + 2 <= file_end
                                        ? colonp + 2 : file_end) - cur);
          if (equalp)
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
              while (endp > cur && isblank ((unsigned char)endp[-1]))
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
              var = incdep_record_strcache (curdep, cur, var_len);

              /* find the start of the value. */
              cur = equalp + 1;
              while (cur < file_end && isblank ((unsigned char)*cur))
                cur++;
              value_start = cur;

              /* find the end of the value / line (this isn't 101% correct). */
              value_end = cur;
              while (cur < file_end)
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
              value = xmalloc (value_len + 1);
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
              /* file: dependencies */

              struct nameseq *filenames = 0;
              struct dep *deps = 0;
              struct dep **nextdep = &deps;
              struct dep *dep;

              /* extract the filename, ASSUME a single one. */
              endp = colonp;
              while (endp > cur && isblank ((unsigned char)endp[-1]))
                --endp;
              if (cur == endp)
                {
                  incdep_warn (curdep, line_no, "empty filename.");
                  break;
                }
              if (   memchr (cur, '$', endp - cur)
                  || memchr (cur, ' ', endp - cur)
                  || memchr (cur, '\t', endp - cur))
                {
                  incdep_warn (curdep, line_no, "multiple / fancy file name. (includedep)");
                  break;
                }
              filenames = xmalloc (sizeof (struct nameseq));
              memset (filenames, 0, sizeof (*filenames));
              filenames->name = incdep_record_strcache (curdep, cur, endp - cur);

              /* parse any dependencies. */
              cur = colonp + 1;
              while (cur < file_end)
                {
                  /* skip blanks and count lines. */
                  while (cur < file_end && isspace ((unsigned char)*cur) && *cur != '\n')
                    ++cur;
                  if (cur >= file_end)
                    break;
                  if (*cur == '\n')
                    {
                      cur++;
                      line_no++;
                      break;
                    }

                  /* continuation + eol? */
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

                  /* find the end of the filename */
                  endp = cur;
                  while (endp < file_end && !isspace ((unsigned char)*endp))
                    ++endp;

                  /* add it to the list. */
                  *nextdep = dep = alloc_dep ();
                  dep->name = incdep_record_strcache (curdep, cur, endp - cur);
                  nextdep = &dep->next;

                  cur = endp;
                }

              /* enter the file with its dependencies. */
              incdep_record_files (curdep,
                                   filenames, NULL, NULL, deps, 0, NULL, 0, 0, f);
            }
        }
    }

  /* free the file data */
  if (is_worker)
    {
      free (curdep->file_base);
      curdep->file_base = curdep->file_end = NULL;
    }
}

/* Flushes the incdep todo and done lists. */
static void
incdep_flush_it (struct floc *f)
{
  incdep_lock ();
  for (;;)
    {
      struct incdep *cur = incdep_head_done;

      /* if the done list is empty, grab a todo list entry. */
      if (!cur && incdep_head_todo)
        {
          cur = incdep_head_todo;
          if (cur->next)
            incdep_head_todo = cur->next;
          else
            incdep_head_todo = incdep_tail_todo = NULL;
          incdep_unlock ();

          incdep_read_file (cur, f);
          eval_include_dep_file (cur, f, 0);
          incdep_free (cur);

          incdep_lock ();
          continue;
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
      incdep_unlock ();

      while (cur)
        {
          struct incdep *next = cur->next;
#ifdef PARSE_IN_WORKER
          incdep_flush_recorded_instructions (cur);
#else
          eval_include_dep_file (cur, f, 0);
#endif
          incdep_free (cur);
          cur = next;
        }

      incdep_lock ();
    } /* outer loop */
  incdep_unlock ();
}


/* splits up a list of file names and feeds it to eval_include_dep_file,
   employing threads to try speed up the file reading. */
void
eval_include_dep (const char *names, struct floc *f, enum incdep_op op)
{
  struct incdep *head = 0;
  struct incdep *tail = 0;
  struct incdep *cur;
  const char *names_iterator = names;
  const char *name;
  unsigned int name_len;

  /* loop through NAMES, creating a todo list out of them. */

  while ((name = find_next_token (&names_iterator, &name_len)) != 0)
    {
       cur = xmalloc (sizeof (*cur) + name_len);
       cur->file_base = cur->file_end = NULL;
       memcpy (cur->name, name, name_len);
       cur->name[name_len] = '\0';
       cur->is_worker = 0;
#ifdef PARSE_IN_WORKER
       cur->err_line_no = 0;
       cur->err_msg = NULL;
       cur->recorded_variables_in_set_head = NULL;
       cur->recorded_variables_in_set_tail = NULL;
       cur->recorded_variable_defs_head = NULL;
       cur->recorded_variable_defs_tail = NULL;
       cur->recorded_files_head = NULL;
       cur->recorded_files_tail = NULL;
#endif

       cur->next = NULL;
       if (tail)
         tail->next = cur;
       else
         head = cur;
       tail = cur;
    }

  if (op == incdep_read_it)
    {
      /* work our way thru the files directly */

      cur = head;
      while (cur)
        {
          struct incdep *next = cur->next;
          incdep_read_file (cur, f);
          eval_include_dep_file (cur, f, 0); /* eats cur */
          cur = next;
        }
    }
  else
    {
      /* initialize the worker threads and related stuff the first time around. */

      if (!incdep_initialized)
        incdep_init (f);

      /* queue the files and notify the worker threads. */

      incdep_lock ();

      if (incdep_tail_todo)
        incdep_tail_todo->next = head;
      else
        incdep_head_todo = head;
      incdep_tail_todo = tail;

      incdep_signal_todo ();
      incdep_unlock ();

      /* flush the todo queue if we're requested to do so. */

      if (op == incdep_flush)
        incdep_flush_it (f);
    }
}

#endif /* CONFIG_WITH_INCLUDEDEP */

