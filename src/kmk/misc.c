/* Miscellaneous generic support functions for GNU Make.
Copyright (C) 1988-2016 Free Software Foundation, Inc.
This file is part of GNU Make.

GNU Make is free software; you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation; either version 3 of the License, or (at your option) any later
version.

GNU Make is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program.  If not, see <http://www.gnu.org/licenses/>.  */

#include "makeint.h"
#include "filedef.h"
#include "dep.h"
#include "debug.h"

/* GNU make no longer supports pre-ANSI89 environments.  */

#include <stdarg.h>

#ifdef HAVE_FCNTL_H
# include <fcntl.h>
#else
# include <sys/file.h>
#endif

#if defined (CONFIG_WITH_VALUE_LENGTH) || defined (CONFIG_WITH_ALLOC_CACHES)
# include <assert.h>
#endif
#ifdef CONFIG_WITH_PRINT_STATS_SWITCH
# ifdef __APPLE__
#  include <malloc/malloc.h>
# endif
# if defined(__GLIBC__) || defined(HAVE_MALLINFO)
#  include <malloc.h>
# endif
#endif
#if defined (CONFIG_WITH_NANOTS) || defined (CONFIG_WITH_PRINT_TIME_SWITCH)
# ifdef WINDOWS32
#  include <Windows.h>
# endif
#endif

/* All bcopy calls in this file can be replaced by memcpy and save a tick or two. */
#ifdef CONFIG_WITH_OPTIMIZATION_HACKS
# undef bcopy
# if defined(__GNUC__) && defined(CONFIG_WITH_OPTIMIZATION_HACKS)
#  define bcopy(src, dst, size)   __builtin_memcpy ((dst), (src), (size))
# else
#  define bcopy(src, dst, size)   memcpy ((dst), (src), (size))
# endif
#endif


/* Compare strings *S1 and *S2.
   Return negative if the first is less, positive if it is greater,
   zero if they are equal.  */

int
alpha_compare (const void *v1, const void *v2)
{
  const char *s1 = *((char **)v1);
  const char *s2 = *((char **)v2);

  if (*s1 != *s2)
    return *s1 - *s2;
  return strcmp (s1, s2);
}

/* Discard each backslash-newline combination from LINE.
   Backslash-backslash-newline combinations become backslash-newlines.
   This is done by copying the text at LINE into itself.  */

#ifndef CONFIG_WITH_VALUE_LENGTH
void
collapse_continuations (char *line)
#else
char *
collapse_continuations (char *line, unsigned int linelen)
#endif
{
  char *in, *out, *p;

#ifndef CONFIG_WITH_VALUE_LENGTH
  in = strchr (line, '\n');
  if (in == 0)
    return;
#else
  assert (strlen (line) == linelen);
  in = memchr (line, '\n', linelen);
  if (in == 0)
      return line + linelen;
  if (in == line || in[-1] != '\\')
    {
      do
        {
          unsigned int off_in = in - line;
          if (off_in == linelen)
            return in;
          in = memchr (in + 1, '\n', linelen - off_in - 1);
          if (in == 0)
            return line + linelen;
        }
      while (in[-1] != '\\');
    }
#endif

  out = in;
  while (out > line && out[-1] == '\\')
    --out;

  while (*in != '\0')
    {
      /* BS_WRITE gets the number of quoted backslashes at
         the end just before IN, and BACKSLASH gets nonzero
         if the next character is quoted.  */
      unsigned int backslash = 0;
      unsigned int bs_write = 0;
      for (p = in - 1; p >= line && *p == '\\'; --p)
        {
          if (backslash)
            ++bs_write;
          backslash = !backslash;

          /* It should be impossible to go back this far without exiting,
             but if we do, we can't get the right answer.  */
          if (in == out - 1)
            abort ();
        }

      /* Output the appropriate number of backslashes.  */
      while (bs_write-- > 0)
        *out++ = '\\';

      /* Skip the newline.  */
      ++in;

      if (backslash)
        {
          /* Backslash/newline handling:
             In traditional GNU make all trailing whitespace, consecutive
             backslash/newlines, and any leading non-newline whitespace on the
             next line is reduced to a single space.
             In POSIX, each backslash/newline and is replaced by a space.  */
          while (ISBLANK (*in))
            ++in;
          if (! posix_pedantic)
            while (out > line && ISBLANK (out[-1]))
              --out;
          *out++ = ' ';
        }
      else
        /* If the newline isn't quoted, put it in the output.  */
        *out++ = '\n';

      /* Now copy the following line to the output.
         Stop when we find backslashes followed by a newline.  */
      while (*in != '\0')
        if (*in == '\\')
          {
            p = in + 1;
            while (*p == '\\')
              ++p;
            if (*p == '\n')
              {
                in = p;
                break;
              }
            while (in < p)
              *out++ = *in++;
          }
        else
          *out++ = *in++;
    }

  *out = '\0';
#ifdef CONFIG_WITH_VALUE_LENGTH
  assert (strchr (line, '\0') == out);
  return out;
#endif
}

/* Print N spaces (used in debug for target-depth).  */

void
print_spaces (unsigned int n)
{
  while (n-- > 0)
    putchar (' ');
}


/* Return a string whose contents concatenate the NUM strings provided
   This string lives in static, re-used memory.  */

const char *
concat (unsigned int num, ...)
{
  static unsigned int rlen = 0;
  static char *result = NULL;
  unsigned int ri = 0;
  va_list args;

  va_start (args, num);

  while (num-- > 0)
    {
      const char *s = va_arg (args, const char *);
      unsigned int l = xstrlen (s);

      if (l == 0)
        continue;

      if (ri + l > rlen)
        {
          rlen = ((rlen ? rlen : 60) + l) * 2;
          result = xrealloc (result, rlen);
        }

      memcpy (result + ri, s, l);
      ri += l;
    }

  va_end (args);

  /* Get some more memory if we don't have enough space for the
     terminating '\0'.   */
  if (ri == rlen)
    {
      rlen = (rlen ? rlen : 60) * 2;
      result = xrealloc (result, rlen);
    }

  result[ri] = '\0';

  return result;
}


#ifndef HAVE_STRERROR
#undef  strerror
char *
strerror (int errnum)
{
  extern int errno, sys_nerr;
#ifndef __DECC
  extern char *sys_errlist[];
#endif
  static char buf[] = "Unknown error 12345678901234567890";

  if (errno < sys_nerr)
    return sys_errlist[errnum];

  sprintf (buf, _("Unknown error %d"), errnum);
  return buf;
}
#endif

/* Like malloc but get fatal error if memory is exhausted.  */
/* Don't bother if we're using dmalloc; it provides these for us.  */

#if !defined(HAVE_DMALLOC_H) && !defined(ELECTRIC_HEAP) /* bird */

#undef xmalloc
#undef xcalloc
#undef xrealloc
#undef xstrdup

void *
xmalloc (unsigned int size)
{
  /* Make sure we don't allocate 0, for pre-ISO implementations.  */
  void *result = malloc (size ? size : 1);
  if (result == 0)
    OUT_OF_MEM();

#ifdef CONFIG_WITH_MAKE_STATS
  make_stats_allocations++;
  if (make_expensive_statistics)
    make_stats_allocated += SIZE_OF_HEAP_BLOCK (result);
  else
    make_stats_allocated += size;
#endif
  return result;
}


void *
xcalloc (unsigned int size)
{
  /* Make sure we don't allocate 0, for pre-ISO implementations.  */
  void *result = calloc (size ? size : 1, 1);
  if (result == 0)
    OUT_OF_MEM();

#ifdef CONFIG_WITH_MAKE_STATS
  make_stats_allocations++;
  if (make_expensive_statistics)
    make_stats_allocated += SIZE_OF_HEAP_BLOCK (result);
  else
    make_stats_allocated += size;
#endif
  return result;
}


void *
xrealloc (void *ptr, unsigned int size)
{
  void *result;
#ifdef CONFIG_WITH_MAKE_STATS
  if (make_expensive_statistics && ptr != NULL)
    make_stats_allocated -= SIZE_OF_HEAP_BLOCK (ptr);
  if (ptr)
    make_stats_reallocations++;
  else
    make_stats_allocations++;
#endif

  /* Some older implementations of realloc() don't conform to ISO.  */
  if (! size)
    size = 1;
  result = ptr ? realloc (ptr, size) : malloc (size);
  if (result == 0)
    OUT_OF_MEM();

#ifdef CONFIG_WITH_MAKE_STATS
  if (make_expensive_statistics)
    make_stats_allocated += SIZE_OF_HEAP_BLOCK (result);
  else
    make_stats_allocated += size;
#endif
  return result;
}


char *
xstrdup (const char *ptr)
{
  char *result;

#ifdef HAVE_STRDUP
  result = strdup (ptr);
#else
  result = malloc (strlen (ptr) + 1);
#endif

  if (result == 0)
    OUT_OF_MEM();

#ifdef CONFIG_WITH_MAKE_STATS
  make_stats_allocations++;
  if (make_expensive_statistics)
    make_stats_allocated += SIZE_OF_HEAP_BLOCK (result);
  else
    make_stats_allocated += strlen (ptr) + 1;
#endif
#ifdef HAVE_STRDUP
  return result;
#else
  return strcpy (result, ptr);
#endif
}

#endif  /* HAVE_DMALLOC_H */

char *
xstrndup (const char *str, unsigned int length)
{
  char *result;

#if defined(HAVE_STRNDUP) && !defined(KMK)
  result = strndup (str, length);
  if (result == 0)
    OUT_OF_MEM();
#else
  result = xmalloc (length + 1);
  if (length > 0)
    strncpy (result, str, length);
  result[length] = '\0';
#endif

  return result;
}


#ifndef CONFIG_WITH_OPTIMIZATION_HACKS /* This is really a reimplemntation of
   memchr, only slower. It's been replaced by a macro in the header file. */

/* Limited INDEX:
   Search through the string STRING, which ends at LIMIT, for the character C.
   Returns a pointer to the first occurrence, or nil if none is found.
   Like INDEX except that the string searched ends where specified
   instead of at the first null.  */

char *
lindex (const char *s, const char *limit, int c)
{
  while (s < limit)
    if (*s++ == c)
      return (char *)(s - 1);

  return 0;
}
#endif /* CONFIG_WITH_OPTIMIZATION_HACKS */

/* Return the address of the first whitespace or null in the string S.  */

char *
end_of_token (const char *s)
{
#if 0 /* @todo def KMK */
    for (;;)
      {
        unsigned char ch0, ch1, ch2, ch3;

        ch0 = *s;
        if (MY_PREDICT_FALSE(MY_IS_BLANK_OR_EOS(ch0)))
          return (char *)s;
        ch1 = s[1];
        if (MY_PREDICT_FALSE(MY_IS_BLANK_OR_EOS(ch1)))
          return (char *)s + 1;
        ch2 = s[2];
        if (MY_PREDICT_FALSE(MY_IS_BLANK_OR_EOS(ch2)))
          return (char *)s + 2;
        ch3 = s[3];
        if (MY_PREDICT_FALSE(MY_IS_BLANK_OR_EOS(ch3)))
          return (char *)s + 3;

        s += 4;
      }

#else
  END_OF_TOKEN (s);
  return (char *)s;
#endif
}

/* Return the address of the first nonwhitespace or null in the string S.  */

char *
next_token (const char *s)
{
#if 0 /* @todo def KMK */
  for (;;)
    {
      unsigned char ch0, ch1, ch2, ch3;

      ch0 = *s;
      if (MY_PREDICT_FALSE(!MY_IS_BLANK(ch0)))
          return (char *)s;
      ch1 = s[1];
      if (MY_PREDICT_TRUE(!MY_IS_BLANK(ch1)))
        return (char *)s + 1;
      ch2 = s[2];
      if (MY_PREDICT_FALSE(!MY_IS_BLANK(ch2)))
        return (char *)s + 2;
      ch3 = s[3];
      if (MY_PREDICT_TRUE(!MY_IS_BLANK(ch3)))
        return (char *)s + 3;

      s += 4;
    }

#else  /* !KMK */
  NEXT_TOKEN (s);
  return (char *)s;
#endif /* !KMK */
}

/* Find the next token in PTR; return the address of it, and store the length
   of the token into *LENGTHPTR if LENGTHPTR is not nil.  Set *PTR to the end
   of the token, so this function can be called repeatedly in a loop.  */

char *
find_next_token (const char **ptr, unsigned int *lengthptr)
{
#ifdef KMK
  const char *p = *ptr;
  const char *e;

  /* skip blanks */
# if 0 /* a moderate version */
  for (;; p++)
    {
      unsigned char ch = *p;
      if (!MY_IS_BLANK(ch))
        {
          if (!ch)
            return NULL;
          break;
        }
    }

# else  /* (too) big unroll */
  for (;; p += 4)
    {
      unsigned char ch0, ch1, ch2, ch3;

      ch0 = *p;
      if (MY_PREDICT_FALSE(!MY_IS_BLANK(ch0)))
        {
          if (!ch0)
            return NULL;
          break;
        }
      ch1 = p[1];
      if (MY_PREDICT_TRUE(!MY_IS_BLANK(ch1)))
        {
          if (!ch1)
            return NULL;
          p += 1;
          break;
        }
      ch2 = p[2];
      if (MY_PREDICT_FALSE(!MY_IS_BLANK(ch2)))
        {
          if (!ch2)
            return NULL;
          p += 2;
          break;
        }
      ch3 = p[3];
      if (MY_PREDICT_TRUE(!MY_IS_BLANK(ch3)))
        {
          if (!ch3)
            return NULL;
          p += 3;
          break;
        }
    }
# endif

  /* skip ahead until EOS or blanks. */
# if 0 /* a moderate version */
  for (e = p + 1;  ; e++)
    {
      unsigned char ch = *e;
      if (MY_PREDICT_FALSE(MY_IS_BLANK_OR_EOS(ch)))
        break;
    }
# else  /* (too) big unroll */
  for (e = p + 1;  ; e += 4)
    {
      unsigned char ch0, ch1, ch2, ch3;

      ch0 = *e;
      if (MY_PREDICT_FALSE(MY_IS_BLANK_OR_EOS(ch0)))
        break;
      ch1 = e[1];
      if (MY_PREDICT_FALSE(MY_IS_BLANK_OR_EOS(ch1)))
        {
          e += 1;
          break;
        }
      ch2 = e[2];
      if (MY_PREDICT_FALSE(MY_IS_BLANK_OR_EOS(ch2)))
        {
          e += 2;
          break;
        }
      ch3 = e[3];
      if (MY_PREDICT_FALSE(MY_IS_BLANK_OR_EOS(ch3)))
        {
          e += 3;
          break;
        }
    }
# endif
  *ptr = e;

  if (lengthptr != 0)
    *lengthptr = e - p;

  return (char *)p;

#else
  const char *p = next_token (*ptr);

  if (*p == '\0')
    return 0;

  *ptr = end_of_token (p);
  if (lengthptr != 0)
    *lengthptr = *ptr - p;

  return (char *)p;
#endif
}
#ifdef KMK

/* Same as find_next_token with two exception:
      - The string ends at EOS or '\0'.
      - We keep track of $() and ${}, allowing functions to be used. */

char *
find_next_token_eos (const char **ptr, const char *eos, unsigned int *lengthptr)
{
  const char *p = *ptr;
  const char *e;
  int level = 0;

  /* skip blanks */
  for (; p != eos; p++)
    {
      unsigned char ch = *p;
      if (!MY_IS_BLANK(ch))
        {
          if (!ch)
            return NULL;
          break;
        }
    }
  if (p == eos)
    return NULL;

  /* skip ahead until EOS or blanks. */
  for (e = p; e != eos; e++)
    {
      unsigned char ch = *e;
      if (MY_IS_BLANK_OR_EOS(ch))
        {
          if (!ch || level == 0)
            break;
        }
      else if (ch == '$')
        {
          if (&e[1] != eos && (e[1] == '(' || e[1] == '{'))
            {
              level++;
              e++;
            }
        }
      else if ((ch == ')' || ch == '}') &&  level > 0)
        level--;
    }

  *ptr = e;
  if (lengthptr != 0)
    *lengthptr = e - p;

  return (char *)p;
}

#endif /* KMK */


/* Copy a chain of 'struct dep'.  For 2nd expansion deps, dup the name.  */

struct dep *
copy_dep_chain (const struct dep *d)
{
  struct dep *firstnew = 0;
  struct dep *lastnew = 0;

  while (d != 0)
    {
#ifndef CONFIG_WITH_ALLOC_CACHES
      struct dep *c = xmalloc (sizeof (struct dep));
#else
      struct dep *c = alloccache_alloc(&dep_cache);
#endif
      memcpy (c, d, sizeof (struct dep));

      /** @todo KMK: Check if we need this duplication! */
      if (c->need_2nd_expansion)
        c->name = xstrdup (c->name);

      c->next = 0;
      if (firstnew == 0)
        firstnew = lastnew = c;
      else
        lastnew = lastnew->next = c;

      d = d->next;
    }

  return firstnew;
}

/* Free a chain of struct nameseq.
   For struct dep chains use free_dep_chain.  */

void
free_ns_chain (struct nameseq *ns)
{
  while (ns != 0)
    {
      struct nameseq *t = ns;
      ns = ns->next;
#ifndef CONFIG_WITH_ALLOC_CACHES
      free_ns (t);
#else
      alloccache_free (&nameseq_cache, t);
#endif
    }
}


#ifdef CONFIG_WITH_ALLOC_CACHES

void
free_dep_chain (struct dep *d)
{
  while (d != 0)
    {
      struct dep *tofree = d;
      d = d->next;
      alloccache_free (&dep_cache, tofree);
    }
}

void
free_goal_chain (struct goaldep *g)
{
  while (g != 0)
    {
      struct goaldep *tofree = g;
      g = g->next;
      alloccache_free (&dep_cache, tofree);
    }
}

#endif /* CONFIG_WITH_ALLOC_CACHES */


#if !HAVE_STRCASECMP && !HAVE_STRICMP && !HAVE_STRCMPI
/* If we don't have strcasecmp() (from POSIX), or anything that can substitute
   for it, define our own version.  */

int
strcasecmp (const char *s1, const char *s2)
{
  while (1)
    {
      int c1 = (int) *(s1++);
      int c2 = (int) *(s2++);

      if (isalpha (c1))
        c1 = tolower (c1);
      if (isalpha (c2))
        c2 = tolower (c2);

      if (c1 != '\0' && c1 == c2)
        continue;

      return (c1 - c2);
    }
}
#endif

#if !HAVE_STRNCASECMP && !HAVE_STRNICMP && !HAVE_STRNCMPI
/* If we don't have strncasecmp() (from POSIX), or anything that can
   substitute for it, define our own version.  */

int
strncasecmp (const char *s1, const char *s2, int n)
{
  while (n-- > 0)
    {
      int c1 = (int) *(s1++);
      int c2 = (int) *(s2++);

      if (isalpha (c1))
        c1 = tolower (c1);
      if (isalpha (c2))
        c2 = tolower (c2);

      if (c1 != '\0' && c1 == c2)
        continue;

      return (c1 - c2);
    }

  return 0;
}
#endif

#ifdef  GETLOADAVG_PRIVILEGED

#ifdef POSIX

/* Hopefully if a system says it's POSIX.1 and has the setuid and setgid
   functions, they work as POSIX.1 says.  Some systems (Alpha OSF/1 1.2,
   for example) which claim to be POSIX.1 also have the BSD setreuid and
   setregid functions, but they don't work as in BSD and only the POSIX.1
   way works.  */

#undef HAVE_SETREUID
#undef HAVE_SETREGID

#else   /* Not POSIX.  */

/* Some POSIX.1 systems have the seteuid and setegid functions.  In a
   POSIX-like system, they are the best thing to use.  However, some
   non-POSIX systems have them too but they do not work in the POSIX style
   and we must use setreuid and setregid instead.  */

#undef HAVE_SETEUID
#undef HAVE_SETEGID

#endif  /* POSIX.  */

#ifndef HAVE_UNISTD_H
extern int getuid (), getgid (), geteuid (), getegid ();
extern int setuid (), setgid ();
#ifdef HAVE_SETEUID
extern int seteuid ();
#else
#ifdef  HAVE_SETREUID
extern int setreuid ();
#endif  /* Have setreuid.  */
#endif  /* Have seteuid.  */
#ifdef HAVE_SETEGID
extern int setegid ();
#else
#ifdef  HAVE_SETREGID
extern int setregid ();
#endif  /* Have setregid.  */
#endif  /* Have setegid.  */
#endif  /* No <unistd.h>.  */

/* Keep track of the user and group IDs for user- and make- access.  */
static int user_uid = -1, user_gid = -1, make_uid = -1, make_gid = -1;
#define access_inited   (user_uid != -1)
static enum { make, user } current_access;


/* Under -d, write a message describing the current IDs.  */

static void
log_access (const char *flavor)
{
  if (! ISDB (DB_JOBS))
    return;

  /* All the other debugging messages go to stdout,
     but we write this one to stderr because it might be
     run in a child fork whose stdout is piped.  */

  fprintf (stderr, _("%s: user %lu (real %lu), group %lu (real %lu)\n"),
           flavor, (unsigned long) geteuid (), (unsigned long) getuid (),
           (unsigned long) getegid (), (unsigned long) getgid ());
  fflush (stderr);
}


static void
init_access (void)
{
#ifndef VMS
  user_uid = getuid ();
  user_gid = getgid ();

  make_uid = geteuid ();
  make_gid = getegid ();

  /* Do these ever fail?  */
  if (user_uid == -1 || user_gid == -1 || make_uid == -1 || make_gid == -1)
    pfatal_with_name ("get{e}[gu]id");

  log_access (_("Initialized access"));

  current_access = make;
#endif
}

#endif  /* GETLOADAVG_PRIVILEGED */

/* Give the process appropriate permissions for access to
   user data (i.e., to stat files, or to spawn a child process).  */
void
user_access (void)
{
#ifdef  GETLOADAVG_PRIVILEGED

  if (!access_inited)
    init_access ();

  if (current_access == user)
    return;

  /* We are in "make access" mode.  This means that the effective user and
     group IDs are those of make (if it was installed setuid or setgid).
     We now want to set the effective user and group IDs to the real IDs,
     which are the IDs of the process that exec'd make.  */

#ifdef  HAVE_SETEUID

  /* Modern systems have the seteuid/setegid calls which set only the
     effective IDs, which is ideal.  */

  if (seteuid (user_uid) < 0)
    pfatal_with_name ("user_access: seteuid");

#else   /* Not HAVE_SETEUID.  */

#ifndef HAVE_SETREUID

  /* System V has only the setuid/setgid calls to set user/group IDs.
     There is an effective ID, which can be set by setuid/setgid.
     It can be set (unless you are root) only to either what it already is
     (returned by geteuid/getegid, now in make_uid/make_gid),
     the real ID (return by getuid/getgid, now in user_uid/user_gid),
     or the saved set ID (what the effective ID was before this set-ID
     executable (make) was exec'd).  */

  if (setuid (user_uid) < 0)
    pfatal_with_name ("user_access: setuid");

#else   /* HAVE_SETREUID.  */

  /* In 4BSD, the setreuid/setregid calls set both the real and effective IDs.
     They may be set to themselves or each other.  So you have two alternatives
     at any one time.  If you use setuid/setgid, the effective will be set to
     the real, leaving only one alternative.  Using setreuid/setregid, however,
     you can toggle between your two alternatives by swapping the values in a
     single setreuid or setregid call.  */

  if (setreuid (make_uid, user_uid) < 0)
    pfatal_with_name ("user_access: setreuid");

#endif  /* Not HAVE_SETREUID.  */
#endif  /* HAVE_SETEUID.  */

#ifdef  HAVE_SETEGID
  if (setegid (user_gid) < 0)
    pfatal_with_name ("user_access: setegid");
#else
#ifndef HAVE_SETREGID
  if (setgid (user_gid) < 0)
    pfatal_with_name ("user_access: setgid");
#else
  if (setregid (make_gid, user_gid) < 0)
    pfatal_with_name ("user_access: setregid");
#endif
#endif

  current_access = user;

  log_access (_("User access"));

#endif  /* GETLOADAVG_PRIVILEGED */
}

/* Give the process appropriate permissions for access to
   make data (i.e., the load average).  */
void
make_access (void)
{
#ifdef  GETLOADAVG_PRIVILEGED

  if (!access_inited)
    init_access ();

  if (current_access == make)
    return;

  /* See comments in user_access, above.  */

#ifdef  HAVE_SETEUID
  if (seteuid (make_uid) < 0)
    pfatal_with_name ("make_access: seteuid");
#else
#ifndef HAVE_SETREUID
  if (setuid (make_uid) < 0)
    pfatal_with_name ("make_access: setuid");
#else
  if (setreuid (user_uid, make_uid) < 0)
    pfatal_with_name ("make_access: setreuid");
#endif
#endif

#ifdef  HAVE_SETEGID
  if (setegid (make_gid) < 0)
    pfatal_with_name ("make_access: setegid");
#else
#ifndef HAVE_SETREGID
  if (setgid (make_gid) < 0)
    pfatal_with_name ("make_access: setgid");
#else
  if (setregid (user_gid, make_gid) < 0)
    pfatal_with_name ("make_access: setregid");
#endif
#endif

  current_access = make;

  log_access (_("Make access"));

#endif  /* GETLOADAVG_PRIVILEGED */
}

/* Give the process appropriate permissions for a child process.
   This is like user_access, but you can't get back to make_access.  */
void
child_access (void)
{
#ifdef  GETLOADAVG_PRIVILEGED

  if (!access_inited)
    abort ();

  /* Set both the real and effective UID and GID to the user's.
     They cannot be changed back to make's.  */

#ifndef HAVE_SETREUID
  if (setuid (user_uid) < 0)
    pfatal_with_name ("child_access: setuid");
#else
  if (setreuid (user_uid, user_uid) < 0)
    pfatal_with_name ("child_access: setreuid");
#endif

#ifndef HAVE_SETREGID
  if (setgid (user_gid) < 0)
    pfatal_with_name ("child_access: setgid");
#else
  if (setregid (user_gid, user_gid) < 0)
    pfatal_with_name ("child_access: setregid");
#endif

  log_access (_("Child access"));

#endif  /* GETLOADAVG_PRIVILEGED */
}

#ifdef NEED_GET_PATH_MAX
unsigned int
get_path_max (void)
{
  static unsigned int value;

  if (value == 0)
    {
      long int x = pathconf ("/", _PC_PATH_MAX);
      if (x > 0)
        value = x;
      else
        return MAXPATHLEN;
    }

  return value;
}
#endif

#ifdef CONFIG_WITH_PRINT_STATS_SWITCH
/* Print heap statistics if supported by the platform. */
void
print_heap_stats (void)
{
  /* Darwin / Mac OS X */
# ifdef __APPLE__
  malloc_statistics_t s;

  malloc_zone_statistics (NULL, &s);
  printf (_("\n# CRT Heap: %u bytes in use, in %u blocks, avg %u bytes/block\n"),
          (unsigned)s.size_in_use, (unsigned)s.blocks_in_use,
          s.blocks_in_use ? (unsigned)(s.size_in_use / s.blocks_in_use : 0));
  printf (_("#           %u bytes max in use (high water mark)\n"),
          (unsigned)s.max_size_in_use);
  printf (_("#           %u bytes reserved,  %u bytes free (estimate)\n"),
          (unsigned)s.size_allocated,
          (unsigned)(s.size_allocated - s.size_in_use));
# endif /* __APPLE__ */

  /* MSC / Windows  */
# ifdef _MSC_VER
  unsigned int blocks_used = 0;
  unsigned int bytes_used = 0;
  unsigned int blocks_avail = 0;
  unsigned int bytes_avail = 0;
  _HEAPINFO hinfo;

  memset (&hinfo, '\0', sizeof (hinfo));
  while (_heapwalk(&hinfo) == _HEAPOK)
    {
      if (hinfo._useflag == _USEDENTRY)
        {
          blocks_used++;
          bytes_used += hinfo._size;
        }
      else
        {
          blocks_avail++;
          bytes_avail += hinfo._size;
        }
    }

  printf (_("\n# CRT Heap: %u bytes in use, in %u blocks, avg %u bytes/block\n"),
          bytes_used, blocks_used, blocks_used ? bytes_used / blocks_used : 0);
  printf (_("#           %u bytes avail, in %u blocks, avg %u bytes/block\n"),
          bytes_avail, blocks_avail, blocks_avail ? bytes_avail / blocks_avail : 0);
# endif /* _MSC_VER */

  /* Darwin Libc sources indicates that something like this may be
     found in GLIBC, however, it's not in any current one...  */
# if 0 /* ??? */
  struct mstats m;

  m = mstats();
  printf (_("\n# CRT Heap: %zu blocks / %zu bytes in use,  %zu blocks / %zu bytes free\n"),
          m.chunks_used, m.bytes_used, m.chunks_free, m.bytes_free);
  printf (_("#           %zu bytes reserved\n"),
          m.bytes_total);
# endif /* ??? */

   /* XVID2/XPG mallinfo (displayed per GLIBC documentation).  */
# if defined(__GLIBC__) || defined(HAVE_MALLINFO)
  struct mallinfo m;

  m = mallinfo();
  printf (_("\n# CRT Heap: %d bytes in use,  %d bytes free\n"),
          m.uordblks, m.fordblks);

  printf (_("#           # free chunks=%d,  # fastbin blocks=%d\n"),
          m.ordblks, m.smblks);
  printf (_("#           # mapped regions=%d,  space in mapped regions=%d\n"),
          m.hblks, m.hblkhd);
  printf (_("#           non-mapped space allocated from system=%d\n"),
          m.arena);
  printf (_("#           maximum total allocated space=%d\n"),
          m.usmblks);
  printf (_("#           top-most releasable space=%d\n"),
          m.keepcost);
# endif /* __GLIBC__ || HAVE_MALLINFO */

# ifdef CONFIG_WITH_MAKE_STATS
  printf(_("#            %lu malloc calls,  %lu realloc calls\n"),
         make_stats_allocations, make_stats_reallocations);
  printf(_("#            %lu MBs alloc sum, not counting freed, add pinch of salt\n"), /* XXX: better wording */
         make_stats_allocated / (1024*1024));
# endif

  /* XXX: windows */
}
#endif /* CONFIG_WITH_PRINT_STATS_SWITCH */

#if defined(CONFIG_WITH_PRINT_TIME_SWITCH) || defined(CONFIG_WITH_KMK_BUILTIN_STATS)
/* Get a nanosecond timestamp, from a monotonic time source if
   possible.  Returns -1 after calling error() on failure.  */

big_int
nano_timestamp (void)
{
  big_int ts;
#if defined (WINDOWS32)
  static int s_state = -1;
  static LARGE_INTEGER s_freq;

  if (s_state == -1)
    s_state = QueryPerformanceFrequency (&s_freq);
  if (s_state)
    {
      LARGE_INTEGER pc;
      if (!QueryPerformanceCounter (&pc))
        {
          s_state = 0;
          return nano_timestamp ();
        }
      ts = (big_int)((long double)pc.QuadPart / (long double)s_freq.QuadPart * 1000000000);
    }
  else
    {
      /* fall back to low resolution system time. */
      LARGE_INTEGER bigint;
      FILETIME ft = {0,0};
      GetSystemTimeAsFileTime (&ft);
      bigint.u.LowPart = ft.dwLowDateTime;
      bigint.u.HighPart = ft.dwLowDateTime;
      ts = bigint.QuadPart * 100;
    }

#elif HAVE_GETTIMEOFDAY
/* FIXME: Linux and others have the realtime clock_* api, detect and use it. */
  struct timeval tv;
  if (!gettimeofday (&tv, NULL))
    ts = (big_int)tv.tv_sec * 1000000000
       + tv.tv_usec * 1000;
  else
    {
      O (error, NILF, _("gettimeofday failed"));
      ts = -1;
    }

#else
# error "PORTME"
#endif

  return ts;
}

/* Formats the elapsed time (nano seconds) in the manner easiest
   to read, with millisecond percision for larger numbers.  */

int
format_elapsed_nano (char *buf, size_t size, big_int ts)
{
  unsigned sz;
  if (ts < 1000)
    sz = sprintf (buf, "%uns", (unsigned)ts);
  else if (ts < 100000)
    sz = sprintf (buf, "%u.%03uus",
                  (unsigned)(ts / 1000),
                  (unsigned)(ts % 1000));
  else
    {
      ts /= 1000;
      if (ts < 1000)
        sz = sprintf (buf, "%uus", (unsigned)ts);
      else if (ts < 100000)
        sz = sprintf (buf, "%u.%03ums",
                      (unsigned)(ts / 1000),
                      (unsigned)(ts % 1000));
      else
        {
          ts /= 1000;
          if (ts < BIG_INT_C(60000))
            sz = sprintf (buf,
                          "%u.%03us",
                          (unsigned)(ts / 1000),
                          (unsigned)(ts % 1000));
          else
            sz = sprintf (buf,
                          "%um%u.%03us",
                          (unsigned)( ts / BIG_INT_C(60000)),
                          (unsigned)((ts % BIG_INT_C(60000)) / 1000),
                          (unsigned)((ts % BIG_INT_C(60000)) % 1000));
        }
    }
  if (sz >= size)
    ONN (fatal, NILF, _("format_elapsed_nano buffer overflow: %u written, %lu buffer"),
         sz, (unsigned long)size);
  return sz;
}
#endif /* CONFIG_WITH_PRINT_TIME_SWITCH || defined(CONFIG_WITH_KMK_BUILTIN_STATS) */

