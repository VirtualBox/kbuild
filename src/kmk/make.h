/* Miscellaneous global declarations and portability cruft for GNU Make.
Copyright (C) 1988, 1989, 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997,
1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006 Free Software
Foundation, Inc.
This file is part of GNU Make.

GNU Make is free software; you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation; either version 2, or (at your option) any later version.

GNU Make is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
GNU Make; see the file COPYING.  If not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.  */

/* We use <config.h> instead of "config.h" so that a compilation
   using -I. -I$srcdir will use ./config.h rather than $srcdir/config.h
   (which it would do because make.h was found in $srcdir).  */
#include <config.h>
#undef  HAVE_CONFIG_H
#define HAVE_CONFIG_H 1

/* AIX requires this to be the first thing in the file.  */
#ifndef __GNUC__
# if HAVE_ALLOCA_H
#  include <alloca.h>
# else
#  ifdef _AIX
 #pragma alloca
#  else
#   ifndef alloca /* predefined by HP cc +Olibcalls */
char *alloca ();
#   endif
#  endif
# endif
#endif


/* Specify we want GNU source code.  This must be defined before any
   system headers are included.  */

#define _GNU_SOURCE 1


#ifdef  CRAY
/* This must happen before #include <signal.h> so
   that the declaration therein is changed.  */
# define signal bsdsignal
#endif

/* If we're compiling for the dmalloc debugger, turn off string inlining.  */
#if defined(HAVE_DMALLOC_H) && defined(__GNUC__)
# define __NO_STRING_INLINES
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <stdio.h>
#include <ctype.h>
#ifdef HAVE_SYS_TIMEB_H
/* SCO 3.2 "devsys 4.2" has a prototype for `ftime' in <time.h> that bombs
   unless <sys/timeb.h> has been included first.  Does every system have a
   <sys/timeb.h>?  If any does not, configure should check for it.  */
# include <sys/timeb.h>
#endif

#if TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

#include <errno.h>

#ifndef errno
extern int errno;
#endif

#ifndef isblank
# define isblank(c)     ((c) == ' ' || (c) == '\t')
#endif

#ifdef  HAVE_UNISTD_H
# include <unistd.h>
/* Ultrix's unistd.h always defines _POSIX_VERSION, but you only get
   POSIX.1 behavior with `cc -YPOSIX', which predefines POSIX itself!  */
# if defined (_POSIX_VERSION) && !defined (ultrix) && !defined (VMS)
#  define POSIX 1
# endif
#endif

/* Some systems define _POSIX_VERSION but are not really POSIX.1.  */
#if (defined (butterfly) || defined (__arm) || (defined (__mips) && defined (_SYSTYPE_SVR3)) || (defined (sequent) && defined (i386)))
# undef POSIX
#endif

#if !defined (POSIX) && defined (_AIX) && defined (_POSIX_SOURCE)
# define POSIX 1
#endif

#ifndef RETSIGTYPE
# define RETSIGTYPE     void
#endif

#ifndef sigmask
# define sigmask(sig)   (1 << ((sig) - 1))
#endif

#ifndef HAVE_SA_RESTART
# define SA_RESTART 0
#endif

#ifdef  HAVE_LIMITS_H
# include <limits.h>
#endif
#ifdef  HAVE_SYS_PARAM_H
# include <sys/param.h>
#endif

#ifndef PATH_MAX
# ifndef POSIX
#  define PATH_MAX      MAXPATHLEN
# endif
#endif
#ifndef MAXPATHLEN
# define MAXPATHLEN 1024
#endif

#ifdef  PATH_MAX
# define GET_PATH_MAX   PATH_MAX
# define PATH_VAR(var)  char var[PATH_MAX]
#else
# define NEED_GET_PATH_MAX 1
# define GET_PATH_MAX   (get_path_max ())
# define PATH_VAR(var)  char *var = alloca (GET_PATH_MAX)
unsigned int get_path_max (void);
#endif

#if defined (KMK) || defined (CONFIG_WITH_VALUE_LENGTH) \
 || defined (CONFIG_WITH_ALLOC_CACHES)
# ifdef _MSC_VER
#  define MY_INLINE     _inline static
# elif defined(__GNUC__)
#  define MY_INLINE     static __inline__
# else
#  define MY_INLINE     static
# endif

# ifdef __GNUC__
#  define MY_PREDICT_TRUE(expr)  __builtin_expect(!!(expr), 1)
#  define MY_PREDICT_FALSE(expr) __builtin_expect(!!(expr), 0)
# else
#  define MY_PREDICT_TRUE(expr)  (expr)
#  define MY_PREDICT_FALSE(expr) (expr)
# endif
#endif

#if defined (KMK) || defined (CONFIG_WITH_VALUE_LENGTH)
# ifdef _MSC_VER
#  define MY_DBGBREAK   __debugbreak()
# elif defined(__GNUC__)
#  if defined(__i386__) || defined(__x86_64__)
#   define MY_DBGBREAK  __asm__ __volatile__ ("int3")
#  else
#   define MY_DBGBREAK  assert(0)
#  endif
# else
#  define MY_DBGBREAK   assert(0)
# endif
# ifndef NDEBUG
#  define MY_ASSERT_MSG(expr, printfargs) \
    do { if (!(expr)) { printf printfargs; MY_DBGBREAK; } } while (0)
# else
#  define MY_ASSERT_MSG(expr, printfargs)   do { } while (0)
# endif
#endif

#ifdef KMK
# include <ctype.h>
# if 1 /* See if this speeds things up (Windows is doing this anyway, so,
          we might as well try be consistent in speed + features).  */
#  if 1
#   define MY_IS_BLANK(ch)          ((ch) == ' ' || (ch) == '\t')
#   define MY_IS_BLANK_OR_EOS(ch)   ((ch) == ' ' || (ch) == '\t' || (ch) == '\0')
#  else
#   define MY_IS_BLANK(ch)          (((ch) == ' ') | ((ch) == '\t'))
#   define MY_IS_BLANK_OR_EOS(ch)   (((ch) == ' ') | ((ch) == '\t') | ((ch) == '\0'))
#  endif
#  undef isblank
#  define isblank(ch)               MY_IS_BLANK(ch)
# else
#  define MY_IS_BLANK(ch)           isblank ((unsigned char)(ch))
#  define MY_IS_BLANK_OR_EOS(ch)    (isblank ((unsigned char)(ch)) || (ch) == '\0')
# endif
#endif

#ifdef CONFIG_WITH_MAKE_STATS
extern long make_stats_allocations;
extern unsigned long make_stats_allocated;
extern unsigned long make_stats_allocated_sum;
extern unsigned long make_stats_ht_lookups;
extern unsigned long make_stats_ht_collisions;

# ifdef __APPLE__
#  include <malloc/malloc.h>
#  define SIZE_OF_HEAP_BLOCK(ptr)   malloc_size(ptr)

# elif defined(__linux__) /* glibc */
#  include <malloc.h>
#  define SIZE_OF_HEAP_BLOCK(ptr)   malloc_usable_size(ptr)

# elif defined(_MSC_VER) || defined(__OS2__)
#  define SIZE_OF_HEAP_BLOCK(ptr)   _msize(ptr)

# else
#  include <stdlib.h>
#  define SIZE_OF_HEAP_BLOCK(ptr)   0
#endif

# if defined(CONFIG_WITH_MAKE_STATS) && !defined(ELECTRIC_HEAP)
#  define free xfree
extern void xfree (void *);
# endif

# define MAKE_STATS_3(expr) do { expr; } while (0)
# define MAKE_STATS_2(expr) do { expr; } while (0)
# define MAKE_STATS(expr)   do { expr; } while (0)
#else
# define MAKE_STATS_3(expr) do { } while (0)
# define MAKE_STATS_2(expr) do { } while (0)
# define MAKE_STATS(expr)   do { } while (0)
#endif


#ifndef CHAR_BIT
# define CHAR_BIT 8
#endif

/* Nonzero if the integer type T is signed.  */
#define INTEGER_TYPE_SIGNED(t) ((t) -1 < 0)

/* The minimum and maximum values for the integer type T.
   Use ~ (t) 0, not -1, for portability to 1's complement hosts.  */
#define INTEGER_TYPE_MINIMUM(t) \
  (! INTEGER_TYPE_SIGNED (t) ? (t) 0 : ~ (t) 0 << (sizeof (t) * CHAR_BIT - 1))
#define INTEGER_TYPE_MAXIMUM(t) (~ (t) 0 - INTEGER_TYPE_MINIMUM (t))

#ifndef CHAR_MAX
# define CHAR_MAX INTEGER_TYPE_MAXIMUM (char)
#endif

#ifdef STAT_MACROS_BROKEN
# ifdef S_ISREG
#  undef S_ISREG
# endif
# ifdef S_ISDIR
#  undef S_ISDIR
# endif
#endif  /* STAT_MACROS_BROKEN.  */

#ifndef S_ISREG
# define S_ISREG(mode)  (((mode) & S_IFMT) == S_IFREG)
#endif
#ifndef S_ISDIR
# define S_ISDIR(mode)  (((mode) & S_IFMT) == S_IFDIR)
#endif

#ifdef VMS
# include <types.h>
# include <unixlib.h>
# include <unixio.h>
# include <perror.h>
/* Needed to use alloca on VMS.  */
# include <builtins.h>
#endif

#ifndef __attribute__
/* This feature is available in gcc versions 2.5 and later.  */
# if __GNUC__ < 2 || (__GNUC__ == 2 && __GNUC_MINOR__ < 5) || __STRICT_ANSI__
#  define __attribute__(x)
# endif
/* The __-protected variants of `format' and `printf' attributes
   are accepted by gcc versions 2.6.4 (effectively 2.7) and later.  */
# if __GNUC__ < 2 || (__GNUC__ == 2 && __GNUC_MINOR__ < 7)
#  define __format__ format
#  define __printf__ printf
# endif
#endif
#define UNUSED  __attribute__ ((unused))

#if defined (STDC_HEADERS) || defined (__GNU_LIBRARY__)
# include <stdlib.h>
# include <string.h>
# define ANSI_STRING 1
#else   /* No standard headers.  */
# ifdef HAVE_STRING_H
#  include <string.h>
#  define ANSI_STRING 1
# else
#  include <strings.h>
# endif
# ifdef HAVE_MEMORY_H
#  include <memory.h>
# endif
# ifdef HAVE_STDLIB_H
#  include <stdlib.h>
# else
void *malloc (int);
void *realloc (void *, int);
void free (void *);

void abort (void) __attribute__ ((noreturn));
void exit (int) __attribute__ ((noreturn));
# endif /* HAVE_STDLIB_H.  */

#endif /* Standard headers.  */

/* These should be in stdlib.h.  Make sure we have them.  */
#ifndef EXIT_SUCCESS
# define EXIT_SUCCESS 0
#endif
#ifndef EXIT_FAILURE
# define EXIT_FAILURE 1
#endif

#ifndef  ANSI_STRING

/* SCO Xenix has a buggy macro definition in <string.h>.  */
#undef  strerror
#if !defined(__DECC)
char *strerror (int errnum);
#endif

#endif  /* !ANSI_STRING.  */
#undef  ANSI_STRING

#if HAVE_INTTYPES_H
# include <inttypes.h>
#endif
#define FILE_TIMESTAMP uintmax_t

#if !defined(HAVE_STRSIGNAL)
char *strsignal (int signum);
#endif

/* ISDIGIT offers the following features:
   - Its arg may be any int or unsigned int; it need not be an unsigned char.
   - It's guaranteed to evaluate its argument exactly once.
      NOTE!  Make relies on this behavior, don't change it!
   - It's typically faster.
   POSIX 1003.2-1992 section 2.5.2.1 page 50 lines 1556-1558 says that
   only '0' through '9' are digits.  Prefer ISDIGIT to isdigit() unless
   it's important to use the locale's definition of `digit' even when the
   host does not conform to POSIX.  */
#define ISDIGIT(c) ((unsigned) (c) - '0' <= 9)

#ifndef iAPX286
# define streq(a, b) \
   ((a) == (b) || \
    (*(a) == *(b) && (*(a) == '\0' || !strcmp ((a) + 1, (b) + 1))))
# ifdef HAVE_CASE_INSENSITIVE_FS
#  define strieq(a, b) \
    ((a) == (b) \
     || (tolower((unsigned char)*(a)) == tolower((unsigned char)*(b)) \
         && (*(a) == '\0' || !strcasecmp ((a) + 1, (b) + 1))))
# else
#  define strieq(a, b) streq(a, b)
# endif
#else
/* Buggy compiler can't handle this.  */
# define streq(a, b) (strcmp ((a), (b)) == 0)
# define strieq(a, b) (strcmp ((a), (b)) == 0)
#endif
#define strneq(a, b, l) (strncmp ((a), (b), (l)) == 0)

#if (defined(__GNUC__) || defined(ENUM_BITFIELDS)) && !defined(NO_ENUM_BITFIELDS)
# define ENUM_BITFIELD(bits)    :bits
#else
# define ENUM_BITFIELD(bits)
#endif

/* Handle gettext and locales.  */

#if HAVE_LOCALE_H
# include <locale.h>
#else
# define setlocale(category, locale)
#endif

#include <gettext.h>

#define _(msgid)            gettext (msgid)
#define N_(msgid)           gettext_noop (msgid)
#define S_(msg1,msg2,num)   ngettext (msg1,msg2,num)

/* Handle other OSs.  */
#ifndef PATH_SEPARATOR_CHAR
# if defined(HAVE_DOS_PATHS)
#  define PATH_SEPARATOR_CHAR ';'
# elif defined(VMS)
#  define PATH_SEPARATOR_CHAR ','
# else
#  define PATH_SEPARATOR_CHAR ':'
# endif
#endif

/* This is needed for getcwd() and chdir(), on some W32 systems.  */
#if defined(HAVE_DIRECT_H)
# include <direct.h>
#endif

#ifdef WINDOWS32
# include <fcntl.h>
# include <malloc.h>
# define pipe(_p)        _pipe((_p), 512, O_BINARY)
# define kill(_pid,_sig) w32_kill((_pid),(_sig))

void sync_Path_environment (void);
int w32_kill (int pid, int sig); /* bird kill -> w32_kill - macro acquired () on the args. */
char *end_of_token_w32 (const char *s, char stopchar);
int find_and_set_default_shell (const char *token);

/* indicates whether or not we have Bourne shell */
extern int no_default_sh_exe;

/* is default_shell unixy? */
extern int unixy_shell;
#endif  /* WINDOWS32 */

struct floc
  {
    const char *filenm;
    unsigned long lineno;
  };
#define NILF ((struct floc *)0)

#define STRING_SIZE_TUPLE(_s) (_s), (sizeof (_s)-1)


/* We have to have stdarg.h or varargs.h AND v*printf or doprnt to use
   variadic versions of these functions.  */

#if HAVE_STDARG_H || HAVE_VARARGS_H
# if HAVE_VPRINTF || HAVE_DOPRNT
#  define USE_VARIADIC 1
# endif
#endif

#if HAVE_ANSI_COMPILER && USE_VARIADIC && HAVE_STDARG_H
void message (int prefix, const char *fmt, ...)
              __attribute__ ((__format__ (__printf__, 2, 3)));
void error (const struct floc *flocp, const char *fmt, ...)
            __attribute__ ((__format__ (__printf__, 2, 3)));
void fatal (const struct floc *flocp, const char *fmt, ...)
                   __attribute__ ((noreturn, __format__ (__printf__, 2, 3)));
#else
void message ();
void error ();
void fatal ();
#endif

void die (int) __attribute__ ((noreturn));
void log_working_directory (int);
void pfatal_with_name (const char *) __attribute__ ((noreturn));
void perror_with_name (const char *, const char *);
char *savestring (const char *, unsigned int);
char *concat (const char *, const char *, const char *);
void *xmalloc (unsigned int);
void *xrealloc (void *, unsigned int);
char *xstrdup (const char *);
#ifdef CONFIG_WITH_PRINT_STATS_SWITCH
void print_heap_stats (void);
#endif
char *find_next_token (const char **, unsigned int *);
char *next_token (const char *);
char *end_of_token (const char *);
#ifndef CONFIG_WITH_VALUE_LENGTH
void collapse_continuations (char *);
#else
char *collapse_continuations (char *, unsigned int);
#endif
#ifdef CONFIG_WITH_OPTIMIZATION_HACKS /* memchr is usually compiler intrinsic, thus faster. */
# define lindex(s, limit, c) ((char *)memchr((s), (c), (limit) - (s)))
#else
char *lindex (const char *, const char *, int);
#endif
int alpha_compare (const void *, const void *);
void print_spaces (unsigned int);
char *find_percent (char *);
const char *find_percent_cached (const char **);
FILE *open_tmpfile (char **, const char *);

#ifndef NO_ARCHIVES
int ar_name (const char *);
void ar_parse_name (const char *, char **, char **);
int ar_touch (const char *);
time_t ar_member_date (const char *);

typedef long int (*ar_member_func_t) (int desc, const char *mem, int truncated,
				      long int hdrpos, long int datapos,
				      long int size, long int date, int uid,
				      int gid, int mode, const void *arg);

long int ar_scan (const char *archive, ar_member_func_t function, const void *arg);
int ar_name_equal (const char *name, const char *mem, int truncated);
#ifndef VMS
int ar_member_touch (const char *arname, const char *memname);
#endif
#endif

int dir_file_exists_p (const char *, const char *);
int file_exists_p (const char *);
int file_impossible_p (const char *);
void file_impossible (const char *);
const char *dir_name (const char *);
void hash_init_directories (void);

void define_default_variables (void);
void set_default_suffixes (void);
void install_default_suffix_rules (void);
void install_default_implicit_rules (void);

void build_vpath_lists (void);
void construct_vpath_list (char *pattern, char *dirpath);
const char *vpath_search (const char *file, FILE_TIMESTAMP *mtime_ptr);
int gpath_search (const char *file, unsigned int len);

void construct_include_path (const char **arg_dirs);

void user_access (void);
void make_access (void);
void child_access (void);

void close_stdout (void);

char *strip_whitespace (const char **begpp, const char **endpp);

#ifdef CONFIG_WITH_ALLOC_CACHES
/* alloccache (misc.c) */

struct alloccache_free_ent
{
  struct alloccache_free_ent *next;
};

struct alloccache
{
  char *free_start;
  char *free_end;
  struct alloccache_free_ent *free_head;
  unsigned int size;
  unsigned int total_count;
  unsigned long alloc_count;
  unsigned long free_count;
  const char *name;
  struct alloccache *next;
  void *grow_arg;
  void *(*grow_alloc)(void *grow_arg, unsigned int size);
};

void alloccache_init (struct alloccache *cache, unsigned int size, const char *name,
                      void *(*grow_alloc)(void *grow_arg, unsigned int size), void *grow_arg);
void alloccache_term (struct alloccache *cache,
                      void (*term_free)(void *term_arg, void *ptr, unsigned int size), void *term_arg);
void alloccache_join (struct alloccache *cache, struct alloccache *eat);
void alloccache_print (struct alloccache *cache);
void alloccache_print_all (void);
struct alloccache_free_ent *alloccache_alloc_grow (struct alloccache *cache);

/* Allocate an item. */
MY_INLINE void *
alloccache_alloc (struct alloccache *cache)
{
  struct alloccache_free_ent *f = cache->free_head;
  if (f)
    cache->free_head = f->next;
  else if (cache->free_start != cache->free_end)
    {
      f = (struct alloccache_free_ent *)cache->free_start;
      cache->free_start += cache->size;
    }
  else
    f = alloccache_alloc_grow (cache);
  MAKE_STATS(cache->alloc_count++;);
  return f;
}

/* Allocate a cleared item. */
MY_INLINE void *
alloccache_calloc (struct alloccache *cache)
{
  void *item = alloccache_alloc (cache);
  memset (item, '\0', cache->size);
  return item;
}

/* Free an item. */
MY_INLINE void
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

/* the alloc caches */
extern struct alloccache dep_cache;
extern struct alloccache file_cache;
extern struct alloccache commands_cache;
extern struct alloccache nameseq_cache;
extern struct alloccache variable_cache;
extern struct alloccache variable_set_cache;
extern struct alloccache variable_set_list_cache;

#endif /* CONFIG_WITH_ALLOC_CACHES */


/* String caching  */
void strcache_init (void);
void strcache_print_stats (const char *prefix);
#ifndef CONFIG_WITH_STRCACHE2
int strcache_iscached (const char *str);
const char *strcache_add (const char *str);
const char *strcache_add_len (const char *str, int len);
int strcache_setbufsize (int size);
#else  /* CONFIG_WITH_STRCACHE2 */

# include "strcache2.h"
extern struct strcache2 file_strcache;
extern const char *suffixes_strcached;

# define strcache_iscached(str)     strcache2_is_cached(&file_strcache, str)
# define strcache_add(str)          strcache2_add_file(&file_strcache, str, strlen (str))
# define strcache_add_len(str, len) strcache2_add_file(&file_strcache, str, len)
# define strcache_get_len(str)      strcache2_get_len(&file_strcache, str) /* FIXME: replace this and related checks ... */

#endif /* CONFIG_WITH_STRCACHE2 */

#ifdef  HAVE_VFORK_H
# include <vfork.h>
#endif

/* We omit these declarations on non-POSIX systems which define _POSIX_VERSION,
   because such systems often declare them in header files anyway.  */

#if !defined (__GNU_LIBRARY__) && !defined (POSIX) && !defined (_POSIX_VERSION) && !defined(WINDOWS32)

long int atol ();
# ifndef VMS
long int lseek ();
# endif

#endif  /* Not GNU C library or POSIX.  */

#ifdef  HAVE_GETCWD
# if !defined(VMS) && !defined(__DECC) && !defined(_MSC_VER) /* bird: MSC */
char *getcwd ();
# endif
#else
char *getwd ();
# define getcwd(buf, len)       getwd (buf)
#endif

#if !HAVE_STRCASECMP
# if HAVE_STRICMP
#  define strcasecmp stricmp
# elif HAVE_STRCMPI
#  define strcasecmp strcmpi
# else
/* Create our own, in misc.c */
int strcasecmp (const char *s1, const char *s2);
# endif
#endif

extern const struct floc *reading_file;
extern const struct floc **expanding_var;

#if !defined(_MSC_VER) /* bird */
extern char **environ;
#endif

extern int just_print_flag, silent_flag, ignore_errors_flag, keep_going_flag;
extern int print_data_base_flag, question_flag, touch_flag, always_make_flag;
extern int env_overrides, no_builtin_rules_flag, no_builtin_variables_flag;
extern int print_version_flag, print_directory_flag, check_symlink_flag;
extern int warn_undefined_variables_flag, posix_pedantic, not_parallel;
extern int second_expansion, clock_skew_detected, rebuilding_makefiles;
#ifdef CONFIG_WITH_2ND_TARGET_EXPANSION
extern int second_target_expansion;
#endif
#ifdef CONFIG_PRETTY_COMMAND_PRINTING
extern int pretty_command_printing;
#endif
#if defined (CONFIG_WITH_MAKE_STATS) || defined (CONFIG_WITH_MINIMAL_STATS)
extern int make_expensive_statistics;
#endif


/* can we run commands via 'sh -c xxx' or must we use batch files? */
extern int batch_mode_shell;

extern char cmd_prefix;

extern unsigned int job_slots;
extern int job_fds[2];
extern int job_rfd;
#ifndef NO_FLOAT
extern double max_load_average;
#else
extern int max_load_average;
#endif

extern char *program;
extern char *starting_directory;
extern unsigned int makelevel;
extern char *version_string, *remote_description, *make_host;

extern unsigned int commands_started;

extern int handling_fatal_signal;


#ifndef MIN
#define MIN(_a,_b) ((_a)<(_b)?(_a):(_b))
#endif
#ifndef MAX
#define MAX(_a,_b) ((_a)>(_b)?(_a):(_b))
#endif

#ifdef VMS
#  define MAKE_SUCCESS 1
#  define MAKE_TROUBLE 2
#  define MAKE_FAILURE 3
#else
#  define MAKE_SUCCESS 0
#  define MAKE_TROUBLE 1
#  define MAKE_FAILURE 2
#endif

/* Set up heap debugging library dmalloc.  */

#ifdef HAVE_DMALLOC_H
#include <dmalloc.h>
#endif

#ifndef initialize_main
# ifdef __EMX__
#  define initialize_main(pargc, pargv) \
                          { _wildcard(pargc, pargv); _response(pargc, pargv); }
# else
#  define initialize_main(pargc, pargv)
# endif
#endif

#ifdef __EMX__
# if !defined chdir
#  define chdir _chdir2
# endif
# if !defined getcwd
#  define getcwd _getcwd2
# endif

/* NO_CHDIR2 causes make not to use _chdir2() and _getcwd2() instead of
   chdir() and getcwd(). This avoids some error messages for the
   make testsuite but restricts the drive letter support. */
# ifdef NO_CHDIR2
#  warning NO_CHDIR2: usage of drive letters restricted
#  undef chdir
#  undef getcwd
# endif
#endif

#ifndef initialize_main
# define initialize_main(pargc, pargv)
#endif


/* Some systems (like Solaris, PTX, etc.) do not support the SA_RESTART flag
   properly according to POSIX.  So, we try to wrap common system calls with
   checks for EINTR.  Note that there are still plenty of system calls that
   can fail with EINTR but this, reportedly, gets the vast majority of
   failure cases.  If you still experience failures you'll need to either get
   a system where SA_RESTART works, or you need to avoid -j.  */

#define EINTRLOOP(_v,_c)   while (((_v)=_c)==-1 && errno==EINTR)

/* While system calls that return integers are pretty consistent about
   returning -1 on failure and setting errno in that case, functions that
   return pointers are not always so well behaved.  Sometimes they return
   NULL for expected behavior: one good example is readdir() which returns
   NULL at the end of the directory--and _doesn't_ reset errno.  So, we have
   to do it ourselves here.  */

#define ENULLLOOP(_v,_c)   do { errno = 0; (_v) = _c; } \
                           while((_v)==0 && errno==EINTR)


#if defined(__EMX__) && defined(CONFIG_WITH_OPTIMIZATION_HACKS) /* bird: saves 40-100ms on libc. */
static inline void *__my_rawmemchr (const void *__s, int __c);
#undef strchr
#define strchr(s, c) \
  (__extension__ (__builtin_constant_p (c)				      \
		  ? ((c) == '\0'					      \
		     ? (char *) __my_rawmemchr ((s), (c))			      \
		     : __my_strchr_c ((s), ((c) & 0xff) << 8))		      \
		  : __my_strchr_g ((s), (c))))
static inline char *__my_strchr_c (const char *__s, int __c)
{
  register unsigned long int __d0;
  register char *__res;
  __asm__ __volatile__
    ("1:\n\t"
     "movb	(%0),%%al\n\t"
     "cmpb	%%ah,%%al\n\t"
     "je	2f\n\t"
     "leal	1(%0),%0\n\t"
     "testb	%%al,%%al\n\t"
     "jne	1b\n\t"
     "xorl	%0,%0\n"
     "2:"
     : "=r" (__res), "=&a" (__d0)
     : "0" (__s), "1" (__c),
       "m" ( *(struct { char __x[0xfffffff]; } *)__s)
     : "cc");
  return __res;
}

static inline char *__my_strchr_g (__const char *__s, int __c)
{
  register unsigned long int __d0;
  register char *__res;
  __asm__ __volatile__
    ("movb	%%al,%%ah\n"
     "1:\n\t"
     "movb	(%0),%%al\n\t"
     "cmpb	%%ah,%%al\n\t"
     "je	2f\n\t"
     "leal	1(%0),%0\n\t"
     "testb	%%al,%%al\n\t"
     "jne	1b\n\t"
     "xorl	%0,%0\n"
     "2:"
     : "=r" (__res), "=&a" (__d0)
     : "0" (__s), "1" (__c),
       "m" ( *(struct { char __x[0xfffffff]; } *)__s)
     : "cc");
  return __res;
}

static inline void *__my_rawmemchr (const void *__s, int __c)
{
  register unsigned long int __d0;
  register unsigned char *__res;
  __asm__ __volatile__
    ("cld\n\t"
     "repne; scasb\n\t"
     : "=D" (__res), "=&c" (__d0)
     : "a" (__c), "0" (__s), "1" (0xffffffff),
       "m" ( *(struct { char __x[0xfffffff]; } *)__s)
     : "cc");
  return __res - 1;
}

#undef memchr
#define memchr(a,b,c) __my_memchr((a),(b),(c))
static inline void *__my_memchr (__const void *__s, int __c, size_t __n)
{
  register unsigned long int __d0;
  register unsigned char *__res;
  if (__n == 0)
    return NULL;
  __asm__ __volatile__
    ("repne; scasb\n\t"
     "je	1f\n\t"
     "movl	$1,%0\n"
     "1:"
     : "=D" (__res), "=&c" (__d0)
     : "a" (__c), "0" (__s), "1" (__n),
       "m" ( *(struct { __extension__ char __x[__n]; } *)__s)
     : "cc");
  return __res - 1;
}

#endif /* __EMX__ (bird) */

#ifdef CONFIG_WITH_IF_CONDITIONALS
extern int expr_eval_if_conditionals(char *line, const struct floc *flocp);
extern char *expr_eval_to_string(char *o, char *expr);
#endif

#ifdef KMK
extern char *abspath(const char *name, char *apath);
extern char *func_breakpoint(char *o, char **argv, const char *funcname);
#endif
