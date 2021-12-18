/* Builtin function expansion for GNU Make.
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
#include "variable.h"
#include "dep.h"
#include "job.h"
#include "commands.h"
#include "debug.h"

#ifdef _AMIGA
#include "amiga.h"
#endif

#ifdef WINDOWS32 /* bird */
# include "pathstuff.h"
# ifdef CONFIG_NEW_WIN_CHILDREN
#  include "w32/winchildren.h"
# endif
#endif

#ifdef KMK_HELPERS
# include "kbuild.h"
#endif
#ifdef CONFIG_WITH_PRINTF
# include "kmkbuiltin.h"
#endif
#ifdef CONFIG_WITH_XARGS /* bird */
# ifdef HAVE_LIMITS_H
#  include <limits.h>
# endif
#endif
#ifdef CONFIG_WITH_COMPILER
# include "kmk_cc_exec.h"
#endif
#include <assert.h> /* bird */

#if defined (CONFIG_WITH_MATH) || defined (CONFIG_WITH_NANOTS) || defined (CONFIG_WITH_FILE_SIZE) /* bird */
# include <ctype.h>
typedef big_int math_int;
static char *math_int_to_variable_buffer (char *, math_int);
static math_int math_int_from_string (const char *str);
#endif

#ifdef CONFIG_WITH_NANOTS /* bird */
# ifdef WINDOWS32
#  include <Windows.h>
# endif
#endif

#ifdef __OS2__
# define CONFIG_WITH_OS2_LIBPATH 1
#endif
#ifdef CONFIG_WITH_OS2_LIBPATH
# define INCL_BASE
# define INCL_ERRROS
# include <os2.h>

# define QHINF_EXEINFO       1 /* NE exeinfo. */
# define QHINF_READRSRCTBL   2 /* Reads from the resource table. */
# define QHINF_READFILE      3 /* Reads from the executable file. */
# define QHINF_LIBPATHLENGTH 4 /* Gets the libpath length. */
# define QHINF_LIBPATH       5 /* Gets the entire libpath. */
# define QHINF_FIXENTRY      6 /* NE only */
# define QHINF_STE           7 /* NE only */
# define QHINF_MAPSEL        8 /* NE only */
  extern APIRET APIENTRY DosQueryHeaderInfo(HMODULE hmod, ULONG ulIndex, PVOID pvBuffer, ULONG cbBuffer, ULONG ulSubFunction);
#endif /* CONFIG_WITH_OS2_LIBPATH */

#ifdef KMK
/** Checks if the @a_cch characters (bytes) in @a a_psz equals @a a_szConst. */
# define STR_N_EQUALS(a_psz, a_cch, a_szConst) \
    ( (a_cch) == sizeof (a_szConst) - 1 && !strncmp ((a_psz), (a_szConst), sizeof (a_szConst) - 1) )

# ifdef _MSC_VER
#  include "kmkbuiltin/mscfakes.h"
# endif
# include "version_compare.h"
#endif


struct function_table_entry
  {
    union {
      char *(*func_ptr) (char *output, char **argv, const char *fname);
      gmk_func_ptr alloc_func_ptr;
    } fptr;
    const char *name;
    unsigned char len;
    unsigned char minimum_args;
    unsigned char maximum_args;
    unsigned char expand_args:1;
    unsigned char alloc_fn:1;
  };

static unsigned long
function_table_entry_hash_1 (const void *keyv)
{
  const struct function_table_entry *key = keyv;
  return_STRING_N_HASH_1 (key->name, key->len);
}

static unsigned long
function_table_entry_hash_2 (const void *keyv)
{
  const struct function_table_entry *key = keyv;
  return_STRING_N_HASH_2 (key->name, key->len);
}

static int
function_table_entry_hash_cmp (const void *xv, const void *yv)
{
  const struct function_table_entry *x = xv;
  const struct function_table_entry *y = yv;
  int result = x->len - y->len;
  if (result)
    return result;
  return_STRING_N_COMPARE (x->name, y->name, x->len);
}

static struct hash_table function_table;

#ifdef CONFIG_WITH_MAKE_STATS
long          make_stats_allocations = 0;
long          make_stats_reallocations = 0;
unsigned long make_stats_allocated = 0;
unsigned long make_stats_ht_lookups = 0;
unsigned long make_stats_ht_collisions = 0;
#endif


/* Store into VARIABLE_BUFFER at O the result of scanning TEXT and replacing
   each occurrence of SUBST with REPLACE. TEXT is null-terminated.  SLEN is
   the length of SUBST and RLEN is the length of REPLACE.  If BY_WORD is
   nonzero, substitutions are done only on matches which are complete
   whitespace-delimited words.  */

char *
subst_expand (char *o, const char *text, const char *subst, const char *replace,
              unsigned int slen, unsigned int rlen, int by_word)
{
  const char *t = text;
  const char *p;

  if (slen == 0 && !by_word)
    {
      /* The first occurrence of "" in any string is its end.  */
      o = variable_buffer_output (o, t, strlen (t));
      if (rlen > 0)
        o = variable_buffer_output (o, replace, rlen);
      return o;
    }

  do
    {
      if (by_word && slen == 0)
        /* When matching by words, the empty string should match
           the end of each word, rather than the end of the whole text.  */
        p = end_of_token (next_token (t));
      else
        {
          p = strstr (t, subst);
          if (p == 0)
            {
              /* No more matches.  Output everything left on the end.  */
              o = variable_buffer_output (o, t, strlen (t));
              return o;
            }
        }

      /* Output everything before this occurrence of the string to replace.  */
      if (p > t)
        o = variable_buffer_output (o, t, p - t);

      /* If we're substituting only by fully matched words,
         or only at the ends of words, check that this case qualifies.  */
      if (by_word
          && ((p > text && !ISSPACE (p[-1]))
              || ! STOP_SET (p[slen], MAP_SPACE|MAP_NUL)))
        /* Struck out.  Output the rest of the string that is
           no longer to be replaced.  */
        o = variable_buffer_output (o, subst, slen);
      else if (rlen > 0)
        /* Output the replacement string.  */
        o = variable_buffer_output (o, replace, rlen);

      /* Advance T past the string to be replaced.  */
      t = p + slen;
    } while (*t != '\0');

  return o;
}


/* Store into VARIABLE_BUFFER at O the result of scanning TEXT
   and replacing strings matching PATTERN with REPLACE.
   If PATTERN_PERCENT is not nil, PATTERN has already been
   run through find_percent, and PATTERN_PERCENT is the result.
   If REPLACE_PERCENT is not nil, REPLACE has already been
   run through find_percent, and REPLACE_PERCENT is the result.
   Note that we expect PATTERN_PERCENT and REPLACE_PERCENT to point to the
   character _AFTER_ the %, not to the % itself.
*/

char *
patsubst_expand_pat (char *o, const char *text,
                     const char *pattern, const char *replace,
                     const char *pattern_percent, const char *replace_percent)
{
  unsigned int pattern_prepercent_len, pattern_postpercent_len;
  unsigned int replace_prepercent_len, replace_postpercent_len;
  const char *t;
  unsigned int len;
  int doneany = 0;

  /* Record the length of REPLACE before and after the % so we don't have to
     compute these lengths more than once.  */
  if (replace_percent)
    {
      replace_prepercent_len = replace_percent - replace - 1;
      replace_postpercent_len = strlen (replace_percent);
    }
  else
    {
      replace_prepercent_len = strlen (replace);
      replace_postpercent_len = 0;
    }

  if (!pattern_percent)
    /* With no % in the pattern, this is just a simple substitution.  */
    return subst_expand (o, text, pattern, replace,
                         strlen (pattern), strlen (replace), 1);

  /* Record the length of PATTERN before and after the %
     so we don't have to compute it more than once.  */
  pattern_prepercent_len = pattern_percent - pattern - 1;
  pattern_postpercent_len = strlen (pattern_percent);

  while ((t = find_next_token (&text, &len)) != 0)
    {
      int fail = 0;

      /* Is it big enough to match?  */
      if (len < pattern_prepercent_len + pattern_postpercent_len)
        fail = 1;

      /* Does the prefix match? */
      if (!fail && pattern_prepercent_len > 0
          && (*t != *pattern
              || t[pattern_prepercent_len - 1] != pattern_percent[-2]
              || !strneq (t + 1, pattern + 1, pattern_prepercent_len - 1)))
        fail = 1;

      /* Does the suffix match? */
      if (!fail && pattern_postpercent_len > 0
          && (t[len - 1] != pattern_percent[pattern_postpercent_len - 1]
              || t[len - pattern_postpercent_len] != *pattern_percent
              || !strneq (&t[len - pattern_postpercent_len],
                          pattern_percent, pattern_postpercent_len - 1)))
        fail = 1;

      if (fail)
        /* It didn't match.  Output the string.  */
        o = variable_buffer_output (o, t, len);
      else
        {
          /* It matched.  Output the replacement.  */

          /* Output the part of the replacement before the %.  */
          o = variable_buffer_output (o, replace, replace_prepercent_len);

          if (replace_percent != 0)
            {
              /* Output the part of the matched string that
                 matched the % in the pattern.  */
              o = variable_buffer_output (o, t + pattern_prepercent_len,
                                          len - (pattern_prepercent_len
                                                 + pattern_postpercent_len));
              /* Output the part of the replacement after the %.  */
              o = variable_buffer_output (o, replace_percent,
                                          replace_postpercent_len);
            }
        }

      /* Output a space, but not if the replacement is "".  */
      if (fail || replace_prepercent_len > 0
          || (replace_percent != 0 && len + replace_postpercent_len > 0))
        {
          o = variable_buffer_output (o, " ", 1);
          doneany = 1;
        }
    }
#ifndef CONFIG_WITH_VALUE_LENGTH
  if (doneany)
    /* Kill the last space.  */
    --o;
#else
  /* Kill the last space and make sure there is a terminator there
     so that strcache_add_len doesn't have to do a lot of exacty work
     when expand_deps sends the output its way. */
  if (doneany)
    *--o = '\0';
  else
    o = variable_buffer_output (o, "\0", 1) - 1;
#endif

  return o;
}

/* Store into VARIABLE_BUFFER at O the result of scanning TEXT
   and replacing strings matching PATTERN with REPLACE.
   If PATTERN_PERCENT is not nil, PATTERN has already been
   run through find_percent, and PATTERN_PERCENT is the result.
   If REPLACE_PERCENT is not nil, REPLACE has already been
   run through find_percent, and REPLACE_PERCENT is the result.
   Note that we expect PATTERN_PERCENT and REPLACE_PERCENT to point to the
   character _AFTER_ the %, not to the % itself.
*/

char *
patsubst_expand (char *o, const char *text, char *pattern, char *replace)
{
  const char *pattern_percent = find_percent (pattern);
  const char *replace_percent = find_percent (replace);

  /* If there's a percent in the pattern or replacement skip it.  */
  if (replace_percent)
    ++replace_percent;
  if (pattern_percent)
    ++pattern_percent;

  return patsubst_expand_pat (o, text, pattern, replace,
                              pattern_percent, replace_percent);
}

#if defined (CONFIG_WITH_OPTIMIZATION_HACKS) || defined (CONFIG_WITH_VALUE_LENGTH)

/* Char map containing the valid function name characters. */
char func_char_map[256];

/* Do the hash table lookup. */

MY_INLINE const struct function_table_entry *
lookup_function_in_hash_tab (const char *s, unsigned char len)
{
    struct function_table_entry function_table_entry_key;
    function_table_entry_key.name = s;
    function_table_entry_key.len = len;

    return hash_find_item (&function_table, &function_table_entry_key);
}

/* Look up a function by name.  */

MY_INLINE const struct function_table_entry *
lookup_function (const char *s, unsigned int len)
{
  unsigned char ch;
# if 0 /* insane loop unroll */

  if (len > MAX_FUNCTION_LENGTH)
      len = MAX_FUNCTION_LENGTH + 1;

#  define X(idx) \
        if (!func_char_map[ch = s[idx]]) \
          { \
            if (ISBLANK (ch)) \
              return lookup_function_in_hash_tab (s, idx); \
            return 0; \
          }
#  define Z(idx) \
        return lookup_function_in_hash_tab (s, idx);

  switch (len)
    {
      default:
        assert (0);
      case  0: return 0;
      case  1: return 0;
      case  2: X(0); X(1); Z(2);
      case  3: X(0); X(1); X(2); Z(3);
      case  4: X(0); X(1); X(2); X(3); Z(4);
      case  5: X(0); X(1); X(2); X(3); X(4); Z(5);
      case  6: X(0); X(1); X(2); X(3); X(4); X(5); Z(6);
      case  7: X(0); X(1); X(2); X(3); X(4); X(5); X(6); Z(7);
      case  8: X(0); X(1); X(2); X(3); X(4); X(5); X(6); X(7); Z(8);
      case  9: X(0); X(1); X(2); X(3); X(4); X(5); X(6); X(7); X(8); Z(9);
      case 10: X(0); X(1); X(2); X(3); X(4); X(5); X(6); X(7); X(8); X(9); Z(10);
      case 11: X(0); X(1); X(2); X(3); X(4); X(5); X(6); X(7); X(8); X(9); X(10); Z(11);
      case 12: X(0); X(1); X(2); X(3); X(4); X(5); X(6); X(7); X(8); X(9); X(10); X(11); Z(12);
      case 13: X(0); X(1); X(2); X(3); X(4); X(5); X(6); X(7); X(8); X(9); X(10); X(11); X(12);
        if ((ch = s[12]) == '\0' || ISBLANK (ch))
          return lookup_function_in_hash_tab (s, 12);
        return 0;
    }
#  undef Z
#  undef X

# else   /* normal loop */
  const char *e = s;
  if (len > MAX_FUNCTION_LENGTH)
      len = MAX_FUNCTION_LENGTH;
  while (func_char_map[ch = *e])
    {
      if (!len--)
        return 0;
      e++;
    }
  if (ch == '\0' || ISBLANK (ch))
    return lookup_function_in_hash_tab (s, e - s);
  return 0;
# endif /* normal loop */
}

#else  /* original code */
/* Look up a function by name.  */

static const struct function_table_entry *
lookup_function (const char *s)
{
  struct function_table_entry function_table_entry_key;
  const char *e = s;
  while (STOP_SET (*e, MAP_USERFUNC))
    e++;

  if (e == s || !STOP_SET(*e, MAP_NUL|MAP_SPACE))
    return NULL;

  function_table_entry_key.name = s;
  function_table_entry_key.len = e - s;

  return hash_find_item (&function_table, &function_table_entry_key);
}
#endif /* original code */


/* Return 1 if PATTERN matches STR, 0 if not.  */

int
pattern_matches (const char *pattern, const char *percent, const char *str)
{
  unsigned int sfxlen, strlength;

  if (percent == 0)
    {
      unsigned int len = strlen (pattern) + 1;
      char *new_chars = alloca (len);
      memcpy (new_chars, pattern, len);
      percent = find_percent (new_chars);
      if (percent == 0)
        return streq (new_chars, str);
      pattern = new_chars;
    }

  sfxlen = strlen (percent + 1);
  strlength = strlen (str);

  if (strlength < (percent - pattern) + sfxlen
      || !strneq (pattern, str, percent - pattern))
    return 0;

  return !strcmp (percent + 1, str + (strlength - sfxlen));
}

#ifdef KMK
/* Return 1 if PATTERN matches STR, 0 if not.

   PATTERN is the pattern to match against.  PERCENT points to the '%' wildcard
   inside PATTERN. SFXLEN is the length of pattern following PERCENT. */

static int
pattern_matches_ex (const char *pattern, const char *percent,
                    unsigned int sfxlen,
                    const char *str, unsigned int strlength)
{
  if (strlength < (percent - pattern) + sfxlen
      || !strneq (pattern, str, percent - pattern)
      || strcmp (percent + 1, str + (strlength - sfxlen)))
    return 0;
  return 1;
}
#endif


/* Find the next comma or ENDPAREN (counting nested STARTPAREN and
   ENDPARENtheses), starting at PTR before END.  Return a pointer to
   next character.

   If no next argument is found, return NULL.
*/

static char *
find_next_argument (char startparen, char endparen,
                    const char *ptr, const char *end)
{
  int count = 0;

  for (; ptr < end; ++ptr)
    if (*ptr == startparen)
      ++count;

    else if (*ptr == endparen)
      {
        --count;
        if (count < 0)
          return NULL;
      }

    else if (*ptr == ',' && !count)
      return (char *)ptr;

  /* We didn't find anything.  */
  return NULL;
}


/* Glob-expand LINE.  The returned pointer is
   only good until the next call to string_glob.  */

static char *
string_glob (char *line)
{
  static char *result = 0;
  static unsigned int length;
  struct nameseq *chain;
  unsigned int idx;

  chain = PARSE_FILE_SEQ (&line, struct nameseq, MAP_NUL, NULL,
                          /* We do not want parse_file_seq to strip './'s.
                             That would break examples like:
                             $(patsubst ./%.c,obj/%.o,$(wildcard ./?*.c)).  */
                          PARSEFS_NOSTRIP|PARSEFS_NOCACHE|PARSEFS_EXISTS);

  if (result == 0)
    {
      length = 100;
      result = xmalloc (100);
    }

  idx = 0;
  while (chain != 0)
    {
      struct nameseq *next = chain->next;
      unsigned int len = strlen (chain->name);

      if (idx + len + 1 > length)
        {
          length += (len + 1) * 2;
          result = xrealloc (result, length);
        }
      memcpy (&result[idx], chain->name, len);
      idx += len;
      result[idx++] = ' ';

      /* Because we used PARSEFS_NOCACHE above, we have to free() NAME.  */
      free ((char *)chain->name);
#ifndef CONFIG_WITH_ALLOC_CACHES
      free (chain);
#else
      alloccache_free (&nameseq_cache, chain);
#endif
      chain = next;
    }

  /* Kill the last space and terminate the string.  */
  if (idx == 0)
    result[0] = '\0';
  else
    result[idx - 1] = '\0';

  return result;
}

/*
  Builtin functions
 */

static char *
func_patsubst (char *o, char **argv, const char *funcname UNUSED)
{
  o = patsubst_expand (o, argv[2], argv[0], argv[1]);
  return o;
}


static char *
func_join (char *o, char **argv, const char *funcname UNUSED)
{
  int doneany = 0;

  /* Write each word of the first argument directly followed
     by the corresponding word of the second argument.
     If the two arguments have a different number of words,
     the excess words are just output separated by blanks.  */
  const char *tp;
  const char *pp;
  const char *list1_iterator = argv[0];
  const char *list2_iterator = argv[1];
  do
    {
      unsigned int len1, len2;

      tp = find_next_token (&list1_iterator, &len1);
      if (tp != 0)
        o = variable_buffer_output (o, tp, len1);

      pp = find_next_token (&list2_iterator, &len2);
      if (pp != 0)
        o = variable_buffer_output (o, pp, len2);

      if (tp != 0 || pp != 0)
        {
          o = variable_buffer_output (o, " ", 1);
          doneany = 1;
        }
    }
  while (tp != 0 || pp != 0);
  if (doneany)
    /* Kill the last blank.  */
    --o;

  return o;
}


static char *
func_origin (char *o, char **argv, const char *funcname UNUSED)
{
  /* Expand the argument.  */
  struct variable *v = lookup_variable (argv[0], strlen (argv[0]));
  if (v == 0)
    o = variable_buffer_output (o, "undefined", 9);
  else
    switch (v->origin)
      {
      default:
      case o_invalid:
        abort ();
        break;
      case o_default:
        o = variable_buffer_output (o, "default", 7);
        break;
      case o_env:
        o = variable_buffer_output (o, "environment", 11);
        break;
      case o_file:
        o = variable_buffer_output (o, "file", 4);
        break;
      case o_env_override:
        o = variable_buffer_output (o, "environment override", 20);
        break;
      case o_command:
        o = variable_buffer_output (o, "command line", 12);
        break;
      case o_override:
        o = variable_buffer_output (o, "override", 8);
        break;
      case o_automatic:
        o = variable_buffer_output (o, "automatic", 9);
        break;
#ifdef CONFIG_WITH_LOCAL_VARIABLES
      case o_local:
        o = variable_buffer_output (o, "local", 5);
        break;
#endif
      }

  return o;
}

static char *
func_flavor (char *o, char **argv, const char *funcname UNUSED)
{
  struct variable *v = lookup_variable (argv[0], strlen (argv[0]));

  if (v == 0)
    o = variable_buffer_output (o, "undefined", 9);
  else
    if (v->recursive)
      o = variable_buffer_output (o, "recursive", 9);
    else
      o = variable_buffer_output (o, "simple", 6);

  return o;
}

#ifdef CONFIG_WITH_WHERE_FUNCTION
static char *
func_where (char *o, char **argv, const char *funcname UNUSED)
{
  struct variable *v = lookup_variable (argv[0], strlen (argv[0]));
  char buf[64];

  if (v == 0)
    o = variable_buffer_output (o, "undefined", 9);
  else
    if (v->fileinfo.filenm)
      {
        o = variable_buffer_output (o, v->fileinfo.filenm, strlen(v->fileinfo.filenm));
        sprintf (buf, ":%lu", v->fileinfo.lineno);
        o = variable_buffer_output (o, buf, strlen(buf));
      }
    else
      o = variable_buffer_output (o, "no-location", 11);

  return o;
}
#endif /* CONFIG_WITH_WHERE_FUNCTION */

static char *
func_notdir_suffix (char *o, char **argv, const char *funcname)
{
  /* Expand the argument.  */
  const char *list_iterator = argv[0];
  const char *p2;
  int doneany =0;
  unsigned int len=0;

  int is_suffix = funcname[0] == 's';
  int is_notdir = !is_suffix;
  int stop = MAP_DIRSEP | (is_suffix ? MAP_DOT : 0);
#ifdef VMS
  /* For VMS list_iterator points to a comma separated list. To use the common
     [find_]next_token, create a local copy and replace the commas with
     spaces. Obviously, there is a problem if there is a ',' in the VMS filename
     (can only happen on ODS5), the same problem as with spaces in filenames,
     which seems to be present in make on all platforms. */
  char *vms_list_iterator = alloca(strlen(list_iterator) + 1);
  int i;
  for (i = 0; list_iterator[i]; i++)
    if (list_iterator[i] == ',')
      vms_list_iterator[i] = ' ';
    else
      vms_list_iterator[i] = list_iterator[i];
  vms_list_iterator[i] = list_iterator[i];
  while ((p2 = find_next_token((const char**) &vms_list_iterator, &len)) != 0)
#else
  while ((p2 = find_next_token (&list_iterator, &len)) != 0)
#endif
    {
      const char *p = p2 + len - 1;

      while (p >= p2 && ! STOP_SET (*p, stop))
        --p;

      if (p >= p2)
        {
          if (is_notdir)
            ++p;
          else if (*p != '.')
            continue;
          o = variable_buffer_output (o, p, len - (p - p2));
        }
#ifdef HAVE_DOS_PATHS
      /* Handle the case of "d:foo/bar".  */
      else if (is_notdir && p2[0] && p2[1] == ':')
        {
          p = p2 + 2;
          o = variable_buffer_output (o, p, len - (p - p2));
        }
#endif
      else if (is_notdir)
        o = variable_buffer_output (o, p2, len);

      if (is_notdir || p >= p2)
        {
#ifdef VMS
          if (vms_comma_separator)
            o = variable_buffer_output (o, ",", 1);
          else
#endif
          o = variable_buffer_output (o, " ", 1);

          doneany = 1;
        }
    }

  if (doneany)
    /* Kill last space.  */
    --o;

  return o;
}


static char *
func_basename_dir (char *o, char **argv, const char *funcname)
{
  /* Expand the argument.  */
  const char *p3 = argv[0];
  const char *p2;
  int doneany = 0;
  unsigned int len = 0;

  int is_basename = funcname[0] == 'b';
  int is_dir = !is_basename;
  int stop = MAP_DIRSEP | (is_basename ? MAP_DOT : 0) | MAP_NUL;
#ifdef VMS
  /* As in func_notdir_suffix ... */
  char *vms_p3 = alloca (strlen(p3) + 1);
  int i;
  for (i = 0; p3[i]; i++)
    if (p3[i] == ',')
      vms_p3[i] = ' ';
    else
      vms_p3[i] = p3[i];
  vms_p3[i] = p3[i];
  while ((p2 = find_next_token((const char**) &vms_p3, &len)) != 0)
#else
  while ((p2 = find_next_token (&p3, &len)) != 0)
#endif
    {
      const char *p = p2 + len - 1;
      while (p >= p2 && ! STOP_SET (*p, stop))
        --p;

      if (p >= p2 && (is_dir))
        o = variable_buffer_output (o, p2, ++p - p2);
      else if (p >= p2 && (*p == '.'))
        o = variable_buffer_output (o, p2, p - p2);
#ifdef HAVE_DOS_PATHS
      /* Handle the "d:foobar" case */
      else if (p2[0] && p2[1] == ':' && is_dir)
        o = variable_buffer_output (o, p2, 2);
#endif
      else if (is_dir)
#ifdef VMS
        {
          extern int vms_report_unix_paths;
          if (vms_report_unix_paths)
            o = variable_buffer_output (o, "./", 2);
          else
            o = variable_buffer_output (o, "[]", 2);
        }
#else
#ifndef _AMIGA
      o = variable_buffer_output (o, "./", 2);
#else
      ; /* Just a nop...  */
#endif /* AMIGA */
#endif /* !VMS */
      else
        /* The entire name is the basename.  */
        o = variable_buffer_output (o, p2, len);

#ifdef VMS
      if (vms_comma_separator)
        o = variable_buffer_output (o, ",", 1);
      else
#endif
        o = variable_buffer_output (o, " ", 1);
      doneany = 1;
    }

  if (doneany)
    /* Kill last space.  */
    --o;

  return o;
}

#if 1 /* rewrite to new MAP stuff? */
# ifdef VMS
#  define IS_PATHSEP(c) ((c) == ']')
# else
#  ifdef HAVE_DOS_PATHS
#   define IS_PATHSEP(c) ((c) == '/' || (c) == '\\')
#  else
#   define IS_PATHSEP(c) ((c) == '/')
#  endif
# endif
#endif

#ifdef CONFIG_WITH_ROOT_FUNC

/*
 $(root path)

 This is mainly for dealing with drive letters and UNC paths on Windows
 and OS/2.
 */
static char *
func_root (char *o, char **argv, const char *funcname UNUSED)
{
  const char  *paths = argv[0] ? argv[0] : "";
  int          doneany = 0;
  const char  *p;
  unsigned int len;

  while ((p = find_next_token (&paths, &len)) != 0)
    {
      const char *p2 = p;

# ifdef HAVE_DOS_PATHS
      if (   len >= 2
          && p2[1] == ':'
          && (   (p2[0] >= 'A' && p2[0] <= 'Z')
              || (p2[0] >= 'a' && p2[0] <= 'z')))
        {
          p2 += 2;
          len -= 2;
        }
      else if (len >= 4 && IS_PATHSEP(p2[0]) && IS_PATHSEP(p2[1])
               && !IS_PATHSEP(p2[2]))
        {
          /* Min recognized UNC: "//./" - find the next slash
             Typical root: "//srv/shr/" */
          /* XXX: Check if //./ needs special handling. */

          p2 += 3;
          len -= 3;
          while (len > 0 && !IS_PATHSEP(*p2))
            p2++, len--;

          if (len && IS_PATHSEP(p2[0]) && (len == 1 || !IS_PATHSEP(p2[1])))
            {
              p2++;
              len--;

              if (len) /* optional share */
                while (len > 0 && !IS_PATHSEP(*p2))
                  p2++, len--;
            }
          else
            p2 = NULL;
        }
      else if (IS_PATHSEP(*p2))
        {
          p2++;
          len--;
        }
      else
        p2 = NULL;

# elif defined (VMS) || defined (AMGIA)
      /* XXX: VMS and AMGIA */
      O (fatal, NILF, _("$(root ) is not implemented on this platform"));
# else
      if (IS_PATHSEP(*p2))
        {
          p2++;
          len--;
        }
      else
        p2 = NULL;
# endif
      if (p2 != NULL)
        {
          /* Include all subsequent path separators. */

          while (len > 0 && IS_PATHSEP(*p2))
            p2++, len--;
          o = variable_buffer_output (o, p, p2 - p);
          o = variable_buffer_output (o, " ", 1);
          doneany = 1;
        }
    }

  if (doneany)
    /* Kill last space.  */
    --o;

  return o;
}

/*
 $(notroot path)

 This is mainly for dealing with drive letters and UNC paths on Windows
 and OS/2.
 */
static char *
func_notroot (char *o, char **argv, const char *funcname UNUSED)
{
  const char  *paths = argv[0] ? argv[0] : "";
  int          doneany = 0;
  const char  *p;
  unsigned int len;

  while ((p = find_next_token (&paths, &len)) != 0)
    {
      const char *p2 = p;

# ifdef HAVE_DOS_PATHS
      if (   len >= 2
          && p2[1] == ':'
          && (   (p2[0] >= 'A' && p2[0] <= 'Z')
              || (p2[0] >= 'a' && p2[0] <= 'z')))
        {
          p2 += 2;
          len -= 2;
        }
      else if (len >= 4 && IS_PATHSEP(p2[0]) && IS_PATHSEP(p2[1])
               && !IS_PATHSEP(p2[2]))
        {
          /* Min recognized UNC: "//./" - find the next slash
             Typical root: "//srv/shr/" */
          /* XXX: Check if //./ needs special handling. */
          unsigned int saved_len = len;

          p2 += 3;
          len -= 3;
          while (len > 0 && !IS_PATHSEP(*p2))
            p2++, len--;

          if (len && IS_PATHSEP(p2[0]) && (len == 1 || !IS_PATHSEP(p2[1])))
            {
              p2++;
              len--;

              if (len) /* optional share */
                while (len > 0 && !IS_PATHSEP(*p2))
                  p2++, len--;
            }
          else
            {
              p2 = p;
              len = saved_len;
            }
        }

# elif defined (VMS) || defined (AMGIA)
      /* XXX: VMS and AMGIA */
      O (fatal, NILF, _("$(root ) is not implemented on this platform"));
# endif

      /* Exclude all subsequent / leading path separators. */

      while (len > 0 && IS_PATHSEP(*p2))
        p2++, len--;
      if (len > 0)
        o = variable_buffer_output (o, p2, len);
      else
        o = variable_buffer_output (o, ".", 1);
      o = variable_buffer_output (o, " ", 1);
      doneany = 1;
    }

  if (doneany)
    /* Kill last space.  */
    --o;

  return o;
}

#endif /* CONFIG_WITH_ROOT_FUNC */

static char *
func_addsuffix_addprefix (char *o, char **argv, const char *funcname)
{
  int fixlen = strlen (argv[0]);
  const char *list_iterator = argv[1];
  int is_addprefix = funcname[3] == 'p';
  int is_addsuffix = !is_addprefix;

  int doneany = 0;
  const char *p;
  unsigned int len;

  while ((p = find_next_token (&list_iterator, &len)) != 0)
    {
      if (is_addprefix)
        o = variable_buffer_output (o, argv[0], fixlen);
      o = variable_buffer_output (o, p, len);
      if (is_addsuffix)
        o = variable_buffer_output (o, argv[0], fixlen);
      o = variable_buffer_output (o, " ", 1);
      doneany = 1;
    }

  if (doneany)
    /* Kill last space.  */
    --o;

  return o;
}

static char *
func_subst (char *o, char **argv, const char *funcname UNUSED)
{
  o = subst_expand (o, argv[2], argv[0], argv[1], strlen (argv[0]),
                    strlen (argv[1]), 0);

  return o;
}

#ifdef CONFIG_WITH_DEFINED_FUNCTIONS

/* Used by func_firstdefined and func_lastdefined to parse the optional last
   argument.  Returns 0 if the variable name is to be returned and 1 if it's
   the variable value value. */
static int
parse_value_name_argument (const char *arg1, const char *funcname)
{
  const char *end;
  int rc;

  if (arg1 == NULL)
    return 0;

  end = strchr (arg1, '\0');
  strip_whitespace (&arg1, &end);

  if (!strncmp (arg1, "name", end - arg1))
    rc = 0;
  else if (!strncmp (arg1, "value", end - arg1))
    rc = 1;
  else
# if 1 /* FIXME: later */
    OSS (fatal, *expanding_var,
         _("second argument to `%s' function must be `name' or `value', not `%s'"),
         funcname, arg1);
# else
    {
      /* check the expanded form */
      char *exp = expand_argument (arg1, strchr (arg1, '\0'));
      arg1 = exp;
      end = strchr (arg1, '\0');
      strip_whitespace (&arg1, &end);

      if (!strncmp (arg1, "name", end - arg1))
        rc = 0;
      else if (!strncmp (arg1, "value", end - arg1))
        rc = 1;
      else
        OSS (fatal, *expanding_var,
             _("second argument to `%s' function must be `name' or `value', not `%s'"),
             funcname, exp);
      free (exp);
    }
# endif

  return rc;
}

/* Given a list of variable names (ARGV[0]), returned the first variable which
   is defined (i.e. value is not empty).  ARGV[1] indicates whether to return
   the variable name or its value. */
static char *
func_firstdefined (char *o, char **argv, const char *funcname)
{
  unsigned int i;
  const char *words = argv[0];    /* Use a temp variable for find_next_token */
  const char *p;
  int ret_value = parse_value_name_argument (argv[1], funcname);

  /* FIXME: Optimize by not expanding the arguments, but instead expand them
     one by one here.  This will require a find_next_token variant which
     takes `$(' and `)' into account. */
  while ((p = find_next_token (&words, &i)) != NULL)
    {
      struct variable *v = lookup_variable (p, i);
      if (v && v->value_length)
        {
          if (ret_value)
            variable_expand_string_2 (o, v->value, v->value_length, &o);
          else
            o = variable_buffer_output (o, p, i);
          break;
        }
    }

  return o;
}

/* Given a list of variable names (ARGV[0]), returned the last variable which
   is defined (i.e. value is not empty).  ARGV[1] indicates whether to return
   the variable name or its value. */
static char *
func_lastdefined (char *o, char **argv, const char *funcname)
{
  struct variable *last_v = NULL;
  unsigned int i;
  const char *words = argv[0];    /* Use a temp variable for find_next_token */
  const char *p;
  int ret_value = parse_value_name_argument (argv[1], funcname);

  /* FIXME: Optimize this.  Walk from the end on unexpanded arguments. */
  while ((p = find_next_token (&words, &i)) != NULL)
    {
      struct variable *v = lookup_variable (p, i);
      if (v && v->value_length)
        {
          last_v = v;
          break;
        }
    }

  if (last_v != NULL)
    {
      if (ret_value)
        variable_expand_string_2 (o, last_v->value, last_v->value_length, &o);
      else
        o = variable_buffer_output (o, last_v->name, last_v->length);
    }
  return o;
}

#endif /* CONFIG_WITH_DEFINED_FUNCTIONS */

static char *
func_firstword (char *o, char **argv, const char *funcname UNUSED)
{
  unsigned int i;
  const char *words = argv[0];    /* Use a temp variable for find_next_token */
  const char *p = find_next_token (&words, &i);

  if (p != 0)
    o = variable_buffer_output (o, p, i);

  return o;
}

static char *
func_lastword (char *o, char **argv, const char *funcname UNUSED)
{
  unsigned int i;
  const char *words = argv[0];    /* Use a temp variable for find_next_token */
  const char *p = NULL;
  const char *t;

  while ((t = find_next_token (&words, &i)))
    p = t;

  if (p != 0)
    o = variable_buffer_output (o, p, i);

  return o;
}

static char *
func_words (char *o, char **argv, const char *funcname UNUSED)
{
  int i = 0;
  const char *word_iterator = argv[0];
  char buf[20];

  while (find_next_token (&word_iterator, NULL) != 0)
    ++i;

  sprintf (buf, "%d", i);
  o = variable_buffer_output (o, buf, strlen (buf));

  return o;
}

/* Set begpp to point to the first non-whitespace character of the string,
 * and endpp to point to the last non-whitespace character of the string.
 * If the string is empty or contains nothing but whitespace, endpp will be
 * begpp-1.
 */
char *
strip_whitespace (const char **begpp, const char **endpp)
{
  while (*begpp <= *endpp && ISSPACE (**begpp))
    (*begpp) ++;
  while (*endpp >= *begpp && ISSPACE (**endpp))
    (*endpp) --;
  return (char *)*begpp;
}

static void
check_numeric (const char *s, const char *msg)
{
  const char *end = s + strlen (s) - 1;
  const char *beg = s;
  strip_whitespace (&s, &end);

  for (; s <= end; ++s)
    if (!ISDIGIT (*s))  /* ISDIGIT only evals its arg once: see makeint.h.  */
      break;

  if (s <= end || end - beg < 0)
    OSS (fatal, *expanding_var, "%s: '%s'", msg, beg);
}



static char *
func_word (char *o, char **argv, const char *funcname UNUSED)
{
  const char *end_p;
  const char *p;
  int i;

  /* Check the first argument.  */
  check_numeric (argv[0], _("non-numeric first argument to 'word' function"));
  i = atoi (argv[0]);

  if (i == 0)
    O (fatal, *expanding_var,
       _("first argument to 'word' function must be greater than 0"));

  end_p = argv[1];
  while ((p = find_next_token (&end_p, 0)) != 0)
    if (--i == 0)
      break;

  if (i == 0)
    o = variable_buffer_output (o, p, end_p - p);

  return o;
}

static char *
func_wordlist (char *o, char **argv, const char *funcname UNUSED)
{
  int start, count;

  /* Check the arguments.  */
  check_numeric (argv[0],
                 _("non-numeric first argument to 'wordlist' function"));
  check_numeric (argv[1],
                 _("non-numeric second argument to 'wordlist' function"));

  start = atoi (argv[0]);
  if (start < 1)
    ON (fatal, *expanding_var,
        "invalid first argument to 'wordlist' function: '%d'", start);

  count = atoi (argv[1]) - start + 1;

  if (count > 0)
    {
      const char *p;
      const char *end_p = argv[2];

      /* Find the beginning of the "start"th word.  */
      while (((p = find_next_token (&end_p, 0)) != 0) && --start)
        ;

      if (p)
        {
          /* Find the end of the "count"th word from start.  */
          while (--count && (find_next_token (&end_p, 0) != 0))
            ;

          /* Return the stuff in the middle.  */
          o = variable_buffer_output (o, p, end_p - p);
        }
    }

  return o;
}

static char *
func_findstring (char *o, char **argv, const char *funcname UNUSED)
{
  /* Find the first occurrence of the first string in the second.  */
  if (strstr (argv[1], argv[0]) != 0)
    o = variable_buffer_output (o, argv[0], strlen (argv[0]));

  return o;
}

static char *
func_foreach (char *o, char **argv, const char *funcname UNUSED)
{
  /* expand only the first two.  */
  char *varname = expand_argument (argv[0], NULL);
  char *list = expand_argument (argv[1], NULL);
  const char *body = argv[2];
#ifdef CONFIG_WITH_VALUE_LENGTH
  long body_len = strlen (body);
#endif

  int doneany = 0;
  const char *list_iterator = list;
  const char *p;
  unsigned int len;
  struct variable *var;

  /* Clean up the variable name by removing whitespace.  */
  char *vp = next_token (varname);
  end_of_token (vp)[0] = '\0';

  push_new_variable_scope ();
  var = define_variable (vp, strlen (vp), "", o_automatic, 0);

  /* loop through LIST,  put the value in VAR and expand BODY */
  while ((p = find_next_token (&list_iterator, &len)) != 0)
    {
#ifndef CONFIG_WITH_VALUE_LENGTH
      char *result = 0;

      free (var->value);
      var->value = xstrndup (p, len);

      result = allocated_variable_expand (body);

      o = variable_buffer_output (o, result, strlen (result));
      o = variable_buffer_output (o, " ", 1);
      doneany = 1;
      free (result);
#else  /* CONFIG_WITH_VALUE_LENGTH */
      if (len >= var->value_alloc_len)
        {
# ifdef CONFIG_WITH_RDONLY_VARIABLE_VALUE
          if (var->rdonly_val)
            var->rdonly_val = 0;
          else
# endif
            free (var->value);
          var->value_alloc_len = VAR_ALIGN_VALUE_ALLOC (len + 1);
          var->value = xmalloc (var->value_alloc_len);
        }
      memcpy (var->value, p, len);
      var->value[len] = '\0';
      var->value_length = len;
      VARIABLE_CHANGED (var);

      variable_expand_string_2 (o, body, body_len, &o);
      o = variable_buffer_output (o, " ", 1);
      doneany = 1;
#endif /* CONFIG_WITH_VALUE_LENGTH */
    }

  if (doneany)
    /* Kill the last space.  */
    --o;

  pop_variable_scope ();
  free (varname);
  free (list);

  return o;
}

#ifdef CONFIG_WITH_LOOP_FUNCTIONS

/* Helper for func_for that evaluates the INIT and NEXT parts. */
static void
helper_eval (char *text, size_t text_len)
{
    unsigned int buf_len;
    char *buf;

    install_variable_buffer (&buf, &buf_len);
    eval_buffer (text, NULL, text + text_len);
    restore_variable_buffer (buf, buf_len);
}

/*
  $(for init,condition,next,body)
  */
static char *
func_for (char *o, char **argv, const char *funcname UNUSED)
{
  char        *init     = argv[0];
  const char  *cond     = argv[1];
  const char  *next     = argv[2];
  size_t       next_len = strlen (next);
  char        *next_buf = xmalloc (next_len + 1);
  const char  *body     = argv[3];
  size_t       body_len = strlen (body);
  unsigned int doneany  = 0;

  push_new_variable_scope ();

  /* Evaluate INIT. */

  helper_eval (init, strlen (init));

  /* Loop till COND is false. */

  while (expr_eval_if_conditionals (cond, NULL) == 0 /* true */)
    {
      /* Expand BODY. */

      if (!doneany)
        doneany = 1;
      else
        o = variable_buffer_output (o, " ", 1);
      variable_expand_string_2 (o, body, body_len, &o);

      /* Evaluate NEXT. */

      memcpy (next_buf, next, next_len + 1);
      helper_eval (next_buf, next_len);
    }

  pop_variable_scope ();
  free (next_buf);

  return o;
}

/*
  $(while condition,body)
 */
static char *
func_while (char *o, char **argv, const char *funcname UNUSED)
{
  const char  *cond     = argv[0];
  const char  *body     = argv[1];
  size_t       body_len = strlen (body);
  unsigned int doneany  = 0;

  push_new_variable_scope ();

  while (expr_eval_if_conditionals (cond, NULL) == 0 /* true */)
    {
      if (!doneany)
        doneany = 1;
      else
        o = variable_buffer_output (o, " ", 1);
      variable_expand_string_2 (o, body, body_len, &o);
    }

  pop_variable_scope ();

  return o;
}

#endif /* CONFIG_WITH_LOOP_FUNCTIONS */

struct a_word
{
  struct a_word *next;
  struct a_word *chain;
  char *str;
  int length;
  int matched;
};

static unsigned long
a_word_hash_1 (const void *key)
{
  return_STRING_HASH_1 (((struct a_word const *) key)->str);
}

static unsigned long
a_word_hash_2 (const void *key)
{
  return_STRING_HASH_2 (((struct a_word const *) key)->str);
}

static int
a_word_hash_cmp (const void *x, const void *y)
{
  int result = ((struct a_word const *) x)->length - ((struct a_word const *) y)->length;
  if (result)
    return result;
  return_STRING_COMPARE (((struct a_word const *) x)->str,
                         ((struct a_word const *) y)->str);
}

struct a_pattern
{
  struct a_pattern *next;
  char *str;
  char *percent;
  int length;
#ifdef KMK
  unsigned int sfxlen;
#endif
};

static char *
func_filter_filterout (char *o, char **argv, const char *funcname)
{
  struct a_word *wordhead;
  struct a_word **wordtail;
  struct a_word *wp;
  struct a_pattern *pathead;
  struct a_pattern **pattail;
  struct a_pattern *pp;

  struct hash_table a_word_table;
  int is_filter = funcname[CSTRLEN ("filter")] == '\0';
  const char *pat_iterator = argv[0];
  const char *word_iterator = argv[1];
  int literals = 0;
  int words = 0;
  int hashing = 0;
  char *p;
  unsigned int len;

  /* Chop ARGV[0] up into patterns to match against the words.
     We don't need to preserve it because our caller frees all the
     argument memory anyway.  */

  pattail = &pathead;
  while ((p = find_next_token (&pat_iterator, &len)) != 0)
    {
      struct a_pattern *pat = alloca (sizeof (struct a_pattern));

      *pattail = pat;
      pattail = &pat->next;

      if (*pat_iterator != '\0')
        ++pat_iterator;

      pat->str = p;
      p[len] = '\0';
      pat->percent = find_percent (p);
      if (pat->percent == 0)
        literals++;
#ifdef KMK
      pat->sfxlen = pat->percent ? strlen(pat->percent + 1) : 0;
#endif

      /* find_percent() might shorten the string so LEN is wrong.  */
      pat->length = strlen (pat->str);
    }
  *pattail = 0;

  /* Chop ARGV[1] up into words to match against the patterns.  */

  wordtail = &wordhead;
  while ((p = find_next_token (&word_iterator, &len)) != 0)
    {
      struct a_word *word = alloca (sizeof (struct a_word));

      *wordtail = word;
      wordtail = &word->next;

      if (*word_iterator != '\0')
        ++word_iterator;

      p[len] = '\0';
      word->str = p;
      word->length = len;
      word->matched = 0;
      word->chain = 0;
      words++;
    }
  *wordtail = 0;

  /* Only use a hash table if arg list lengths justifies the cost.  */
  hashing = (literals >= 2 && (literals * words) >= 10);
  if (hashing)
    {
      hash_init (&a_word_table, words, a_word_hash_1, a_word_hash_2,
                 a_word_hash_cmp);
      for (wp = wordhead; wp != 0; wp = wp->next)
        {
          struct a_word *owp = hash_insert (&a_word_table, wp);
          if (owp)
            wp->chain = owp;
        }
    }

  if (words)
    {
      int doneany = 0;

      /* Run each pattern through the words, killing words.  */
      for (pp = pathead; pp != 0; pp = pp->next)
        {
          if (pp->percent)
            for (wp = wordhead; wp != 0; wp = wp->next)
#ifdef KMK
              wp->matched |= pattern_matches_ex (pp->str, pp->percent, pp->sfxlen,
                                                 wp->str, wp->length);
#else
              wp->matched |= pattern_matches (pp->str, pp->percent, wp->str);
#endif
          else if (hashing)
            {
              struct a_word a_word_key;
              a_word_key.str = pp->str;
              a_word_key.length = pp->length;
              wp = hash_find_item (&a_word_table, &a_word_key);
              while (wp)
                {
                  wp->matched |= 1;
                  wp = wp->chain;
                }
            }
          else
            for (wp = wordhead; wp != 0; wp = wp->next)
              wp->matched |= (wp->length == pp->length
                              && strneq (pp->str, wp->str, wp->length));
        }

      /* Output the words that matched (or didn't, for filter-out).  */
      for (wp = wordhead; wp != 0; wp = wp->next)
        if (is_filter ? wp->matched : !wp->matched)
          {
            o = variable_buffer_output (o, wp->str, strlen (wp->str));
            o = variable_buffer_output (o, " ", 1);
            doneany = 1;
          }

      if (doneany)
        /* Kill the last space.  */
        --o;
    }

  if (hashing)
    hash_free (&a_word_table, 0);

  return o;
}


static char *
func_strip (char *o, char **argv, const char *funcname UNUSED)
{
  const char *p = argv[0];
  int doneany = 0;

  while (*p != '\0')
    {
      int i=0;
      const char *word_start;

      NEXT_TOKEN (p);
      word_start = p;
      for (i=0; *p != '\0' && !ISSPACE (*p); ++p, ++i)
        {}
      if (!i)
        break;
      o = variable_buffer_output (o, word_start, i);
      o = variable_buffer_output (o, " ", 1);
      doneany = 1;
    }

  if (doneany)
    /* Kill the last space.  */
    --o;

  return o;
}

/*
  Print a warning or fatal message.
*/
static char *
func_error (char *o, char **argv, const char *funcname)
{
  char **argvp;
  char *msg, *p;
  int len;

  /* The arguments will be broken on commas.  Rather than create yet
     another special case where function arguments aren't broken up,
     just create a format string that puts them back together.  */
  for (len=0, argvp=argv; *argvp != 0; ++argvp)
    len += strlen (*argvp) + 2;

  p = msg = alloca (len + 1);

  for (argvp=argv; argvp[1] != 0; ++argvp)
    {
      strcpy (p, *argvp);
      p += strlen (*argvp);
      *(p++) = ',';
      *(p++) = ' ';
    }
  strcpy (p, *argvp);

  switch (*funcname)
    {
    case 'e':
      OS (fatal, reading_file, "%s", msg);

    case 'w':
      OS (error, reading_file, "%s", msg);
      break;

    case 'i':
      outputs (0, msg);
      outputs (0, "\n");
      break;

    default:
      OS (fatal, *expanding_var, "Internal error: func_error: '%s'", funcname);
    }

  /* The warning function expands to the empty string.  */
  return o;
}

#ifdef KMK
/* Compare strings *S1 and *S2.
   Return negative if the first is less, positive if it is greater,
   zero if they are equal.  */

static int
version_compare_wrapper (const void *v1, const void *v2)
{
  const char *s1 = *((char **)v1);
  const char *s2 = *((char **)v2);
  return version_compare (s1, s2);
}
#endif /* KMK */

/*
  chop argv[0] into words, and sort them.
 */
static char *
func_sort (char *o, char **argv, const char *funcname UNUSED)
{
  const char *t;
  char **words;
  int wordi;
  char *p;
  unsigned int len;

  /* Find the maximum number of words we'll have.  */
  t = argv[0];
  wordi = 0;
  while ((p = find_next_token (&t, NULL)) != 0)
    {
      ++t;
      ++wordi;
    }

  words = xmalloc ((wordi == 0 ? 1 : wordi) * sizeof (char *));

  /* Now assign pointers to each string in the array.  */
  t = argv[0];
  wordi = 0;
  while ((p = find_next_token (&t, &len)) != 0)
    {
      if (*t != '\0') /* bird: Fixes access beyond end of string and overflowing words array. */
        ++t;
      p[len] = '\0';
      words[wordi++] = p;
    }

  if (wordi)
    {
      int i;

      /* Now sort the list of words.  */
#ifdef KMK
      if (funcname[0] == 'v' || funcname[1] == 'v')
        qsort (words, wordi, sizeof (char *), version_compare_wrapper);
      else
        qsort (words, wordi, sizeof (char *), alpha_compare);
#else
      qsort (words, wordi, sizeof (char *), alpha_compare);
#endif

      /* Now write the sorted list, uniquified.  */
#ifdef CONFIG_WITH_RSORT
      if (*funcname != 'r')
        {
          /* sort */
#endif
          for (i = 0; i < wordi; ++i)
            {
              len = strlen (words[i]);
              if (i == wordi - 1 || strlen (words[i + 1]) != len
                  || strcmp (words[i], words[i + 1]))
                {
                  o = variable_buffer_output (o, words[i], len);
                  o = variable_buffer_output (o, " ", 1);
                }
            }
#ifdef CONFIG_WITH_RSORT
        }
      else
        {
          /* rsort - reverse the result */
          i = wordi;
          while (i-- > 0)
            {
              len = strlen (words[i]);
              if (i == 0 || strlen (words[i - 1]) != len
                  || strcmp (words[i], words[i - 1]))
                {
                  o = variable_buffer_output (o, words[i], len);
                  o = variable_buffer_output (o, " ", 1);
                }
            }
        }
#endif

      /* Kill the last space.  */
      --o;
    }

  free (words);

  return o;
}

/*
  $(if condition,true-part[,false-part])

  CONDITION is false iff it evaluates to an empty string.  White
  space before and after condition are stripped before evaluation.

  If CONDITION is true, then TRUE-PART is evaluated, otherwise FALSE-PART is
  evaluated (if it exists).  Because only one of the two PARTs is evaluated,
  you can use $(if ...) to create side-effects (with $(shell ...), for
  example).
*/

static char *
func_if (char *o, char **argv, const char *funcname UNUSED)
{
  const char *begp = argv[0];
  const char *endp = begp + strlen (argv[0]) - 1;
  int result = 0;

  /* Find the result of the condition: if we have a value, and it's not
     empty, the condition is true.  If we don't have a value, or it's the
     empty string, then it's false.  */

  strip_whitespace (&begp, &endp);

  if (begp <= endp)
    {
      char *expansion = expand_argument (begp, endp+1);

      result = strlen (expansion);
      free (expansion);
    }

  /* If the result is true (1) we want to eval the first argument, and if
     it's false (0) we want to eval the second.  If the argument doesn't
     exist we do nothing, otherwise expand it and add to the buffer.  */

  argv += 1 + !result;

  if (*argv)
    {
      char *expansion = expand_argument (*argv, NULL);

      o = variable_buffer_output (o, expansion, strlen (expansion));

      free (expansion);
    }

  return o;
}

/*
  $(or condition1[,condition2[,condition3[...]]])

  A CONDITION is false iff it evaluates to an empty string.  White
  space before and after CONDITION are stripped before evaluation.

  CONDITION1 is evaluated.  If it's true, then this is the result of
  expansion.  If it's false, CONDITION2 is evaluated, and so on.  If none of
  the conditions are true, the expansion is the empty string.

  Once a CONDITION is true no further conditions are evaluated
  (short-circuiting).
*/

static char *
func_or (char *o, char **argv, const char *funcname UNUSED)
{
  for ( ; *argv ; ++argv)
    {
      const char *begp = *argv;
      const char *endp = begp + strlen (*argv) - 1;
      char *expansion;
      int result = 0;

      /* Find the result of the condition: if it's false keep going.  */

      strip_whitespace (&begp, &endp);

      if (begp > endp)
        continue;

      expansion = expand_argument (begp, endp+1);
      result = strlen (expansion);

      /* If the result is false keep going.  */
      if (!result)
        {
          free (expansion);
          continue;
        }

      /* It's true!  Keep this result and return.  */
      o = variable_buffer_output (o, expansion, result);
      free (expansion);
      break;
    }

  return o;
}

/*
  $(and condition1[,condition2[,condition3[...]]])

  A CONDITION is false iff it evaluates to an empty string.  White
  space before and after CONDITION are stripped before evaluation.

  CONDITION1 is evaluated.  If it's false, then this is the result of
  expansion.  If it's true, CONDITION2 is evaluated, and so on.  If all of
  the conditions are true, the expansion is the result of the last condition.

  Once a CONDITION is false no further conditions are evaluated
  (short-circuiting).
*/

static char *
func_and (char *o, char **argv, const char *funcname UNUSED)
{
  char *expansion;

  while (1)
    {
      const char *begp = *argv;
      const char *endp = begp + strlen (*argv) - 1;
      int result;

      /* An empty condition is always false.  */
      strip_whitespace (&begp, &endp);
      if (begp > endp)
        return o;

      expansion = expand_argument (begp, endp+1);
      result = strlen (expansion);

      /* If the result is false, stop here: we're done.  */
      if (!result)
        break;

      /* Otherwise the result is true.  If this is the last one, keep this
         result and quit.  Otherwise go on to the next one!  */

      if (*(++argv))
        free (expansion);
      else
        {
          o = variable_buffer_output (o, expansion, result);
          break;
        }
    }

  free (expansion);

  return o;
}

static char *
func_wildcard (char *o, char **argv, const char *funcname UNUSED)
{
#ifdef _AMIGA
   o = wildcard_expansion (argv[0], o);
#else
   char *p = string_glob (argv[0]);
   o = variable_buffer_output (o, p, strlen (p));
#endif
   return o;
}

/*
  $(eval <makefile string>)

  Always resolves to the empty string.

  Treat the arguments as a segment of makefile, and parse them.
*/

static char *
func_eval (char *o, char **argv, const char *funcname UNUSED)
{
  char *buf;
  unsigned int len;

  /* Eval the buffer.  Pop the current variable buffer setting so that the
     eval'd code can use its own without conflicting.  */

  install_variable_buffer (&buf, &len);

#ifndef CONFIG_WITH_VALUE_LENGTH
  eval_buffer (argv[0], NULL);
#else
  eval_buffer (argv[0], NULL, strchr (argv[0], '\0'));
#endif

  restore_variable_buffer (buf, len);

  return o;
}


#ifdef CONFIG_WITH_EVALPLUS
/* Same as func_eval except that we push and pop the local variable
   context before evaluating the buffer. */
static char *
func_evalctx (char *o, char **argv, const char *funcname UNUSED)
{
  char *buf;
  unsigned int len;

  /* Eval the buffer.  Pop the current variable buffer setting so that the
     eval'd code can use its own without conflicting.  */

  install_variable_buffer (&buf, &len);

  push_new_variable_scope ();

  eval_buffer (argv[0], NULL, strchr (argv[0], '\0'));

  pop_variable_scope ();

  restore_variable_buffer (buf, len);

  return o;
}

/* A mix of func_eval and func_value, saves memory for the expansion.
  This implements both evalval and evalvalctx, the latter has its own
  variable context just like evalctx. */
static char *
func_evalval (char *o, char **argv, const char *funcname)
{
  /* Look up the variable.  */
  struct variable *v = lookup_variable (argv[0], strlen (argv[0]));
  if (v)
    {
      char *buf;
      unsigned int len;
      int var_ctx;
      size_t off;
      const floc *reading_file_saved = reading_file;
# ifdef CONFIG_WITH_MAKE_STATS
      unsigned long long uStartTick = CURRENT_CLOCK_TICK();
#  ifndef CONFIG_WITH_COMPILER
      MAKE_STATS_2(v->evalval_count++);
#  endif
# endif

      var_ctx = !strcmp (funcname, "evalvalctx");
      if (var_ctx)
        push_new_variable_scope ();
      if (v->fileinfo.filenm)
        reading_file = &v->fileinfo;

# ifdef CONFIG_WITH_COMPILER
      /* If this variable has been evaluated more than a few times, it make
         sense to compile it to speed up the processing. */

      v->evalval_count++;
      if (   v->evalprog
          || (v->evalval_count == 3 && kmk_cc_compile_variable_for_eval (v)))
        {
          install_variable_buffer (&buf, &len); /* Really necessary? */
          kmk_exec_eval_variable (v);
          restore_variable_buffer (buf, len);
        }
      else
# endif
      {
        /* Make a copy of the value to the variable buffer first since
           eval_buffer will make changes to its input. */

        off = o - variable_buffer;
        variable_buffer_output (o, v->value, v->value_length + 1);
        o = variable_buffer + off;
        assert (!o[v->value_length]);

        install_variable_buffer (&buf, &len); /* Really necessary? */
        eval_buffer (o, NULL, o + v->value_length);
        restore_variable_buffer (buf, len);
      }

      reading_file = reading_file_saved;
      if (var_ctx)
        pop_variable_scope ();

      MAKE_STATS_2(v->cTicksEvalVal += CURRENT_CLOCK_TICK() - uStartTick);
    }

  return o;
}

/* Optimizes the content of one or more variables to save time in
   the eval functions.  This function will collapse line continuations
   and remove comments.  */
static char *
func_eval_optimize_variable (char *o, char **argv, const char *funcname)
{
  unsigned int i;

  for (i = 0; argv[i]; i++)
    {
      struct variable *v = lookup_variable (argv[i], strlen (argv[i]));
# ifdef CONFIG_WITH_RDONLY_VARIABLE_VALUE
      if (v && v->origin != o_automatic && !v->rdonly_val)
# else
      if (v && v->origin != o_automatic)
# endif
        {
          char *eos, *src;

          eos = collapse_continuations (v->value, v->value_length);
          v->value_length = eos - v->value;

          /* remove comments */

          src = memchr (v->value, '#', v->value_length);
          if (src)
            {
              unsigned char ch = '\0';
              char *dst = src;
              do
                {
                  /* drop blanks preceeding the comment */
                  while (dst > v->value)
                    {
                      ch = (unsigned char)dst[-1];
                      if (!ISBLANK (ch))
                        break;
                      dst--;
                    }

                  /* advance SRC to eol / eos. */
                  src = memchr (src, '\n', eos - src);
                  if (!src)
                      break;

                  /* drop a preceeding newline if possible (full line comment) */
                  if (dst > v->value && dst[-1] == '\n')
                    dst--;

                  /* copy till next comment or eol. */
                  while (src < eos)
                    {
                      ch = *src++;
                      if (ch == '#')
                        break;
                      *dst++ = ch;
                    }
                }
              while (ch == '#' && src < eos);

              *dst = '\0';
              v->value_length = dst - v->value;
            }

          VARIABLE_CHANGED (v);

# ifdef CONFIG_WITH_COMPILER
          /* Compile the variable for evalval, evalctx and expansion. */

          if (   v->recursive
              && !IS_VARIABLE_RECURSIVE_WITHOUT_DOLLAR (v))
                kmk_cc_compile_variable_for_expand (v);
          kmk_cc_compile_variable_for_eval (v);
# endif
        }
      else if (v)
        OSS (error, NILF, _("$(%s ): variable `%s' is of the wrong type\n"), funcname, v->name);
    }

  return o;
}

#endif /* CONFIG_WITH_EVALPLUS */

static char *
func_value (char *o, char **argv, const char *funcname UNUSED)
{
  /* Look up the variable.  */
  struct variable *v = lookup_variable (argv[0], strlen (argv[0]));

  /* Copy its value into the output buffer without expanding it.  */
  if (v)
#ifdef CONFIG_WITH_VALUE_LENGTH
    {
      assert (v->value_length == strlen (v->value));
      o = variable_buffer_output (o, v->value, v->value_length);
    }
#else
    o = variable_buffer_output (o, v->value, strlen (v->value));
#endif

  return o;
}

/*
  \r is replaced on UNIX as well. Is this desirable?
 */
static void
fold_newlines (char *buffer, unsigned int *length, int trim_newlines)
{
  char *dst = buffer;
  char *src = buffer;
  char *last_nonnl = buffer - 1;
  src[*length] = 0;
  for (; *src != '\0'; ++src)
    {
      if (src[0] == '\r' && src[1] == '\n')
        continue;
      if (*src == '\n')
        {
          *dst++ = ' ';
        }
      else
        {
          last_nonnl = dst;
          *dst++ = *src;
        }
    }

  if (!trim_newlines && (last_nonnl < (dst - 2)))
    last_nonnl = dst - 2;

  *(++last_nonnl) = '\0';
  *length = last_nonnl - buffer;
}

pid_t shell_function_pid = 0;
static int shell_function_completed;

void
shell_completed (int exit_code, int exit_sig)
{
  char buf[256];

  shell_function_pid = 0;
  if (exit_sig == 0 && exit_code == 127)
    shell_function_completed = -1;
  else
    shell_function_completed = 1;

  sprintf (buf, "%d", exit_code);
  define_variable_cname (".SHELLSTATUS", buf, o_override, 0);
}

#ifdef WINDOWS32
/*untested*/

# ifndef CONFIG_NEW_WIN_CHILDREN
#include <windows.h>
#include <io.h>
#include "sub_proc.h"

int
windows32_openpipe (int *pipedes, int errfd, pid_t *pid_p, char **command_argv, char **envp)
{
  SECURITY_ATTRIBUTES saAttr;
  HANDLE hIn = INVALID_HANDLE_VALUE;
  HANDLE hErr = INVALID_HANDLE_VALUE;
  HANDLE hChildOutRd;
  HANDLE hChildOutWr;
  HANDLE hProcess, tmpIn, tmpErr;
  DWORD e;

  /* Set status for return.  */
  pipedes[0] = pipedes[1] = -1;
  *pid_p = (pid_t)-1;

  saAttr.nLength = sizeof (SECURITY_ATTRIBUTES);
  saAttr.bInheritHandle = TRUE;
  saAttr.lpSecurityDescriptor = NULL;

  /* Standard handles returned by GetStdHandle can be NULL or
     INVALID_HANDLE_VALUE if the parent process closed them.  If that
     happens, we open the null device and pass its handle to
     process_begin below as the corresponding handle to inherit.  */
  tmpIn = GetStdHandle (STD_INPUT_HANDLE);
  if (DuplicateHandle (GetCurrentProcess (), tmpIn,
                       GetCurrentProcess (), &hIn,
                       0, TRUE, DUPLICATE_SAME_ACCESS) == FALSE)
    {
      e = GetLastError ();
      if (e == ERROR_INVALID_HANDLE)
        {
          tmpIn = CreateFile ("NUL", GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
          if (tmpIn != INVALID_HANDLE_VALUE
              && DuplicateHandle (GetCurrentProcess (), tmpIn,
                                  GetCurrentProcess (), &hIn,
                                  0, TRUE, DUPLICATE_SAME_ACCESS) == FALSE)
            CloseHandle (tmpIn);
        }
      if (hIn == INVALID_HANDLE_VALUE)
        {
          ON (error, NILF,
              _("windows32_openpipe: DuplicateHandle(In) failed (e=%ld)\n"), e);
          return -1;
        }
    }
  tmpErr = (HANDLE)_get_osfhandle (errfd);
  if (DuplicateHandle (GetCurrentProcess (), tmpErr,
                       GetCurrentProcess (), &hErr,
                       0, TRUE, DUPLICATE_SAME_ACCESS) == FALSE)
    {
      e = GetLastError ();
      if (e == ERROR_INVALID_HANDLE)
        {
          tmpErr = CreateFile ("NUL", GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
          if (tmpErr != INVALID_HANDLE_VALUE
              && DuplicateHandle (GetCurrentProcess (), tmpErr,
                                  GetCurrentProcess (), &hErr,
                                  0, TRUE, DUPLICATE_SAME_ACCESS) == FALSE)
            CloseHandle (tmpErr);
        }
      if (hErr == INVALID_HANDLE_VALUE)
        {
          ON (error, NILF,
              _("windows32_openpipe: DuplicateHandle(Err) failed (e=%ld)\n"), e);
          return -1;
        }
    }

  if (! CreatePipe (&hChildOutRd, &hChildOutWr, &saAttr, 0))
    {
      ON (error, NILF, _("CreatePipe() failed (e=%ld)\n"), GetLastError());
      return -1;
    }

  hProcess = process_init_fd (hIn, hChildOutWr, hErr);

  if (!hProcess)
    {
      O (error, NILF, _("windows32_openpipe(): process_init_fd() failed\n"));
      return -1;
    }

  /* make sure that CreateProcess() has Path it needs */
  sync_Path_environment ();
  /* 'sync_Path_environment' may realloc 'environ', so take note of
     the new value.  */
  envp = environ;

  if (! process_begin (hProcess, command_argv, envp, command_argv[0], NULL))
    {
      /* register process for wait */
      process_register (hProcess);

      /* set the pid for returning to caller */
      *pid_p = (pid_t) hProcess;

      /* set up to read data from child */
      pipedes[0] = _open_osfhandle ((intptr_t) hChildOutRd, O_RDONLY);

      /* this will be closed almost right away */
      pipedes[1] = _open_osfhandle ((intptr_t) hChildOutWr, O_APPEND);
      return 0;
    }
  else
    {
      /* reap/cleanup the failed process */
      process_cleanup (hProcess);

      /* close handles which were duplicated, they weren't used */
      if (hIn != INVALID_HANDLE_VALUE)
        CloseHandle (hIn);
      if (hErr != INVALID_HANDLE_VALUE)
        CloseHandle (hErr);

      /* close pipe handles, they won't be used */
      CloseHandle (hChildOutRd);
      CloseHandle (hChildOutWr);

      return -1;
    }
}
# endif /* !CONFIG_NEW_WIN_CHILDREN */
#endif


#ifdef __MSDOS__
FILE *
msdos_openpipe (int* pipedes, int *pidp, char *text)
{
  FILE *fpipe=0;
  /* MSDOS can't fork, but it has 'popen'.  */
  struct variable *sh = lookup_variable ("SHELL", 5);
  int e;
  extern int dos_command_running, dos_status;

  /* Make sure not to bother processing an empty line.  */
  NEXT_TOKEN (text);
  if (*text == '\0')
    return 0;

  if (sh)
    {
      char buf[PATH_MAX + 7];
      /* This makes sure $SHELL value is used by $(shell), even
         though the target environment is not passed to it.  */
      sprintf (buf, "SHELL=%s", sh->value);
      putenv (buf);
    }

  e = errno;
  errno = 0;
  dos_command_running = 1;
  dos_status = 0;
  /* If dos_status becomes non-zero, it means the child process
     was interrupted by a signal, like SIGINT or SIGQUIT.  See
     fatal_error_signal in commands.c.  */
  fpipe = popen (text, "rt");
  dos_command_running = 0;
  if (!fpipe || dos_status)
    {
      pipedes[0] = -1;
      *pidp = -1;
      if (dos_status)
        errno = EINTR;
      else if (errno == 0)
        errno = ENOMEM;
      if (fpipe)
        pclose (fpipe);
      shell_completed (127, 0);
    }
  else
    {
      pipedes[0] = fileno (fpipe);
      *pidp = 42; /* Yes, the Meaning of Life, the Universe, and Everything! */
      errno = e;
    }
  return fpipe;
}
#endif

/*
  Do shell spawning, with the naughty bits for different OSes.
 */

#ifdef VMS

/* VMS can't do $(shell ...)  */

char *
func_shell_base (char *o, char **argv, int trim_newlines)
{
  fprintf (stderr, "This platform does not support shell\n");
  die (MAKE_TROUBLE);
  return NULL;
}

#define func_shell 0

#else
#ifndef _AMIGA
char *
func_shell_base (char *o, char **argv, int trim_newlines)
{
  char *batch_filename = NULL;
  int errfd;
#ifdef __MSDOS__
  FILE *fpipe;
#endif
  char **command_argv;
  const char * volatile error_prefix; /* bird: this volatile ~~and the 'o' one~~, is for shutting up gcc warnings */
  char **envp;
  int pipedes[2];
  pid_t pid;

#ifndef __MSDOS__
#ifdef WINDOWS32
  /* Reset just_print_flag.  This is needed on Windows when batch files
     are used to run the commands, because we normally refrain from
     creating batch files under -n.  */
  int j_p_f = just_print_flag;
  just_print_flag = 0;
#endif

  /* Construct the argument list.  */
  command_argv = construct_command_argv (argv[0], NULL, NULL, 0,
                                         &batch_filename);
  if (command_argv == 0)
    {
#ifdef WINDOWS32
      just_print_flag = j_p_f;
#endif
      return o;
    }
#endif /* !__MSDOS__ */

  /* Using a target environment for 'shell' loses in cases like:
       export var = $(shell echo foobie)
       bad := $(var)
     because target_environment hits a loop trying to expand $(var) to put it
     in the environment.  This is even more confusing when 'var' was not
     explicitly exported, but just appeared in the calling environment.

     See Savannah bug #10593.

  envp = target_environment (NULL);
  */

  envp = environ;

  /* For error messages.  */
  if (reading_file && reading_file->filenm)
    {
      char *p = alloca (strlen (reading_file->filenm)+11+4);
      sprintf (p, "%s:%lu: ", reading_file->filenm,
               reading_file->lineno + reading_file->offset);
      error_prefix = p;
    }
  else
    error_prefix = "";

  /* Set up the output in case the shell writes something.  */
  output_start ();

#ifdef CONFIG_WITH_OUTPUT_IN_MEMORY
  errfd = -1; /** @todo fixme */
#else
  errfd = (output_context && output_context->err >= 0
           ? output_context->err : FD_STDERR);
#endif

#if defined(__MSDOS__)
  fpipe = msdos_openpipe (pipedes, &pid, argv[0]);
  if (pipedes[0] < 0)
    {
      perror_with_name (error_prefix, "pipe");
      return o;
    }

#elif defined(WINDOWS32)
# ifdef CONFIG_NEW_WIN_CHILDREN
  pipedes[1] = -1;
  MkWinChildCreateWithStdOutPipe (command_argv, envp, errfd, &pid, &pipedes[0]);
# else
  windows32_openpipe (pipedes, errfd, &pid, command_argv, envp);
# endif
  /* Restore the value of just_print_flag.  */
  just_print_flag = j_p_f;

  if (pipedes[0] < 0)
    {
      /* Open of the pipe failed, mark as failed execution.  */
      shell_completed (127, 0);
      perror_with_name (error_prefix, "pipe");
      return o;
    }

#else
  if (pipe (pipedes) < 0)
    {
      perror_with_name (error_prefix, "pipe");
      return o;
    }

  /* Close handles that are unnecessary for the child process.  */
  CLOSE_ON_EXEC(pipedes[1]);
  CLOSE_ON_EXEC(pipedes[0]);

  {
    struct output out;
    out.syncout = 1;
    out.out = pipedes[1];
    out.err = errfd;

    pid = child_execute_job (&out, 1, command_argv, envp);
  }

  if (pid < 0)
    {
      perror_with_name (error_prefix, "fork");
      return o;
    }
#endif

  {
    char *buffer;
    unsigned int maxlen, i;
    int cc;

    /* Record the PID for reap_children.  */
    shell_function_pid = pid;
#ifndef  __MSDOS__
    shell_function_completed = 0;

    /* Free the storage only the child needed.  */
    free (command_argv[0]);
    free (command_argv);

    /* Close the write side of the pipe.  We test for -1, since
       pipedes[1] is -1 on MS-Windows, and some versions of MS
       libraries barf when 'close' is called with -1.  */
    if (pipedes[1] >= 0)
      close (pipedes[1]);
#endif

    /* Set up and read from the pipe.  */

    maxlen = 200;
    buffer = xmalloc (maxlen + 1);

    /* Read from the pipe until it gets EOF.  */
    for (i = 0; ; i += cc)
      {
        if (i == maxlen)
          {
            maxlen += 512;
            buffer = xrealloc (buffer, maxlen + 1);
          }

        EINTRLOOP (cc, read (pipedes[0], &buffer[i], maxlen - i));
        if (cc <= 0)
          break;
      }
    buffer[i] = '\0';

    /* Close the read side of the pipe.  */
#ifdef  __MSDOS__
    if (fpipe)
      {
        int st = pclose (fpipe);
        shell_completed (st, 0);
      }
#else
# ifdef _MSC_VER /* Avoid annoying msvcrt when debugging. (bird) */
      if (pipedes[0] != -1)
# endif
        (void) close (pipedes[0]);
#endif

    /* Loop until child_handler or reap_children()  sets
       shell_function_completed to the status of our child shell.  */
    while (shell_function_completed == 0)
      reap_children (1, 0);

    if (batch_filename)
      {
        DB (DB_VERBOSE, (_("Cleaning up temporary batch file %s\n"),
                         batch_filename));
        remove (batch_filename);
        free (batch_filename);
      }
    shell_function_pid = 0;

    /* shell_completed() will set shell_function_completed to 1 when the
       child dies normally, or to -1 if it dies with status 127, which is
       most likely an exec fail.  */

    if (shell_function_completed == -1)
      {
        /* This likely means that the execvp failed, so we should just
           write the error message in the pipe from the child.  */
        fputs (buffer, stderr);
        fflush (stderr);
      }
    else
      {
        /* The child finished normally.  Replace all newlines in its output
           with spaces, and put that in the variable output buffer.  */
        fold_newlines (buffer, &i, trim_newlines);
        o = variable_buffer_output (o, buffer, i);
      }

    free (buffer);
  }

  return o;
}

#else   /* _AMIGA */

/* Do the Amiga version of func_shell.  */

char *
func_shell_base (char *o, char **argv, int trim_newlines)
{
  /* Amiga can't fork nor spawn, but I can start a program with
     redirection of my choice.  However, this means that we
     don't have an opportunity to reopen stdout to trap it.  Thus,
     we save our own stdout onto a new descriptor and dup a temp
     file's descriptor onto our stdout temporarily.  After we
     spawn the shell program, we dup our own stdout back to the
     stdout descriptor.  The buffer reading is the same as above,
     except that we're now reading from a file.  */

#include <dos/dos.h>
#include <proto/dos.h>

  BPTR child_stdout;
  char tmp_output[FILENAME_MAX];
  unsigned int maxlen = 200, i;
  int cc;
  char * buffer, * ptr;
  char ** aptr;
  int len = 0;
  char* batch_filename = NULL;

  /* Construct the argument list.  */
  command_argv = construct_command_argv (argv[0], NULL, NULL, 0,
                                         &batch_filename);
  if (command_argv == 0)
    return o;

  /* Note the mktemp() is a security hole, but this only runs on Amiga.
     Ideally we would use output_tmpfile(), but this uses a special
     Open(), not fopen(), and I'm not familiar enough with the code to mess
     with it.  */
  strcpy (tmp_output, "t:MakeshXXXXXXXX");
  mktemp (tmp_output);
  child_stdout = Open (tmp_output, MODE_NEWFILE);

  for (aptr=command_argv; *aptr; aptr++)
    len += strlen (*aptr) + 1;

  buffer = xmalloc (len + 1);
  ptr = buffer;

  for (aptr=command_argv; *aptr; aptr++)
    {
      strcpy (ptr, *aptr);
      ptr += strlen (ptr) + 1;
      *ptr ++ = ' ';
      *ptr = 0;
    }

  ptr[-1] = '\n';

  Execute (buffer, NULL, child_stdout);
  free (buffer);

  Close (child_stdout);

  child_stdout = Open (tmp_output, MODE_OLDFILE);

  buffer = xmalloc (maxlen);
  i = 0;
  do
    {
      if (i == maxlen)
        {
          maxlen += 512;
          buffer = xrealloc (buffer, maxlen + 1);
        }

      cc = Read (child_stdout, &buffer[i], maxlen - i);
      if (cc > 0)
        i += cc;
    } while (cc > 0);

  Close (child_stdout);

  fold_newlines (buffer, &i, trim_newlines);
  o = variable_buffer_output (o, buffer, i);
  free (buffer);
  return o;
}
#endif  /* _AMIGA */

static char *
func_shell (char *o, char **argv, const char *funcname UNUSED)
{
  return func_shell_base (o, argv, 1);
}
#endif  /* !VMS */

#ifdef EXPERIMENTAL

/*
  equality. Return is string-boolean, i.e., the empty string is false.
 */
static char *
func_eq (char *o, char **argv, const char *funcname UNUSED)
{
  int result = ! strcmp (argv[0], argv[1]);
  o = variable_buffer_output (o,  result ? "1" : "", result);
  return o;
}


/*
  string-boolean not operator.
 */
static char *
func_not (char *o, char **argv, const char *funcname UNUSED)
{
  const char *s = argv[0];
  int result = 0;
  NEXT_TOKEN (s);
  result = ! (*s);
  o = variable_buffer_output (o,  result ? "1" : "", result);
  return o;
}
#endif


#ifdef HAVE_DOS_PATHS
# ifdef __CYGWIN__
#  define IS_ABSOLUTE(n) ((n[0] && n[1] == ':') || STOP_SET (n[0], MAP_DIRSEP))
# else
#  define IS_ABSOLUTE(n) (n[0] && n[1] == ':')
# endif
# define ROOT_LEN 3
#else
#define IS_ABSOLUTE(n) (n[0] == '/')
#define ROOT_LEN 1
#endif

/* Return the absolute name of file NAME which does not contain any '.',
   '..' components nor any repeated path separators ('/').   */
#ifdef KMK
char *
#else
static char *
#endif
abspath (const char *name, char *apath)
{
  char *dest;
  const char *start, *end, *apath_limit;
  unsigned long root_len = ROOT_LEN;

  if (name[0] == '\0' || apath == NULL)
    return NULL;

#ifdef WINDOWS32                                                    /* bird */
  dest = unix_slashes_resolved (name, apath, GET_PATH_MAX);
  if (!dest)
    return NULL;
  dest = strchr(apath, '\0');

  (void)end; (void)start; (void)apath_limit;

#elif defined __OS2__                                               /* bird */
  if (_abspath(apath, name, GET_PATH_MAX))
    return NULL;
  dest = strchr(apath, '\0');

  (void)end; (void)start; (void)apath_limit; (void)dest;

#else /* !WINDOWS32 && !__OS2__ */
  apath_limit = apath + GET_PATH_MAX;

  if (!IS_ABSOLUTE(name))
    {
      /* It is unlikely we would make it until here but just to make sure. */
      if (!starting_directory)
        return NULL;

      strcpy (apath, starting_directory);

#ifdef HAVE_DOS_PATHS
      if (STOP_SET (name[0], MAP_DIRSEP))
        {
          if (STOP_SET (name[1], MAP_DIRSEP))
            {
              /* A UNC.  Don't prepend a drive letter.  */
              apath[0] = name[0];
              apath[1] = name[1];
              root_len = 2;
            }
          /* We have /foo, an absolute file name except for the drive
             letter.  Assume the missing drive letter is the current
             drive, which we can get if we remove from starting_directory
             everything past the root directory.  */
          apath[root_len] = '\0';
        }
#endif

      dest = strchr (apath, '\0');
    }
  else
    {
#if defined(__CYGWIN__) && defined(HAVE_DOS_PATHS)
      if (STOP_SET (name[0], MAP_DIRSEP))
        root_len = 1;
#endif
#ifdef KMK
      memcpy (apath, name, root_len);
      apath[root_len] = '\0';
      assert (strlen (apath) == root_len);
#else
      strncpy (apath, name, root_len);
      apath[root_len] = '\0';
#endif
      dest = apath + root_len;
      /* Get past the root, since we already copied it.  */
      name += root_len;
#ifdef HAVE_DOS_PATHS
      if (! STOP_SET (apath[root_len - 1], MAP_DIRSEP))
        {
          /* Convert d:foo into d:./foo and increase root_len.  */
          apath[2] = '.';
          apath[3] = '/';
          dest++;
          root_len++;
          /* strncpy above copied one character too many.  */
          name--;
        }
      else
        apath[root_len - 1] = '/'; /* make sure it's a forward slash */
#endif
    }

  for (start = end = name; *start != '\0'; start = end)
    {
      unsigned long len;

      /* Skip sequence of multiple path-separators.  */
      while (STOP_SET (*start, MAP_DIRSEP))
        ++start;

      /* Find end of path component.  */
      for (end = start; ! STOP_SET (*end, MAP_DIRSEP|MAP_NUL); ++end)
        ;

      len = end - start;

      if (len == 0)
        break;
      else if (len == 1 && start[0] == '.')
        /* nothing */;
      else if (len == 2 && start[0] == '.' && start[1] == '.')
        {
          /* Back up to previous component, ignore if at root already.  */
          if (dest > apath + root_len)
            for (--dest; ! STOP_SET (dest[-1], MAP_DIRSEP); --dest)
              ;
        }
      else
        {
          if (! STOP_SET (dest[-1], MAP_DIRSEP))
            *dest++ = '/';

          if (dest + len >= apath_limit)
            return NULL;

          dest = memcpy (dest, start, len);
          dest += len;
          *dest = '\0';
        }
    }
#endif /* !WINDOWS32 && !__OS2__ */

  /* Unless it is root strip trailing separator.  */
  if (dest > apath + root_len && STOP_SET (dest[-1], MAP_DIRSEP))
    --dest;

  *dest = '\0';

  return apath;
}


static char *
func_realpath (char *o, char **argv, const char *funcname UNUSED)
{
  /* Expand the argument.  */
  const char *p = argv[0];
  const char *path = 0;
  int doneany = 0;
  unsigned int len = 0;

  while ((path = find_next_token (&p, &len)) != 0)
    {
      if (len < GET_PATH_MAX)
        {
          char *rp;
          struct stat st;
          PATH_VAR (in);
          PATH_VAR (out);

          strncpy (in, path, len);
          in[len] = '\0';

#ifdef HAVE_REALPATH
          ENULLLOOP (rp, realpath (in, out));
#else
          rp = abspath (in, out);
#endif

          if (rp)
            {
              int r;
              EINTRLOOP (r, stat (out, &st));
              if (r == 0)
                {
                  o = variable_buffer_output (o, out, strlen (out));
                  o = variable_buffer_output (o, " ", 1);
                  doneany = 1;
                }
            }
        }
    }

  /* Kill last space.  */
  if (doneany)
    --o;

  return o;
}

static char *
func_file (char *o, char **argv, const char *funcname UNUSED)
{
  char *fn = argv[0];

  if (fn[0] == '>')
    {
      FILE *fp;
#ifdef KMK_FOPEN_NO_INHERIT_MODE
      const char *mode = "w" KMK_FOPEN_NO_INHERIT_MODE;
#else
      const char *mode = "w";
#endif

      /* We are writing a file.  */
      ++fn;
      if (fn[0] == '>')
        {
#ifdef KMK_FOPEN_NO_INHERIT_MODE
          mode = "a" KMK_FOPEN_NO_INHERIT_MODE;
#else
          mode = "a";
#endif
          ++fn;
        }
      NEXT_TOKEN (fn);

      if (fn[0] == '\0')
        O (fatal, *expanding_var, _("file: missing filename"));

      ENULLLOOP (fp, fopen (fn, mode));
      if (fp == NULL)
        OSS (fatal, reading_file, _("open: %s: %s"), fn, strerror (errno));

      if (argv[1])
        {
          int l = strlen (argv[1]);
          int nl = l == 0 || argv[1][l-1] != '\n';

          if (fputs (argv[1], fp) == EOF || (nl && fputc ('\n', fp) == EOF))
            OSS (fatal, reading_file, _("write: %s: %s"), fn, strerror (errno));
        }
      if (fclose (fp))
        OSS (fatal, reading_file, _("close: %s: %s"), fn, strerror (errno));
    }
  else if (fn[0] == '<')
    {
      char *preo = o;
      FILE *fp;

      ++fn;
      NEXT_TOKEN (fn);
      if (fn[0] == '\0')
        O (fatal, *expanding_var, _("file: missing filename"));

      if (argv[1])
        O (fatal, *expanding_var, _("file: too many arguments"));

#ifdef KMK_FOPEN_NO_INHERIT_MODE
      ENULLLOOP (fp, fopen (fn, "r" KMK_FOPEN_NO_INHERIT_MODE));
#else
      ENULLLOOP (fp, fopen (fn, "r"));
#endif
      if (fp == NULL)
        {
          if (errno == ENOENT)
            return o;
          OSS (fatal, reading_file, _("open: %s: %s"), fn, strerror (errno));
        }

      while (1)
        {
          char buf[1024];
          size_t l = fread (buf, 1, sizeof (buf), fp);
          if (l > 0)
            o = variable_buffer_output (o, buf, l);

          if (ferror (fp))
            if (errno != EINTR)
              OSS (fatal, reading_file, _("read: %s: %s"), fn, strerror (errno));
          if (feof (fp))
            break;
        }
      if (fclose (fp))
        OSS (fatal, reading_file, _("close: %s: %s"), fn, strerror (errno));

      /* Remove trailing newline.  */
      if (o > preo && o[-1] == '\n')
        if (--o > preo && o[-1] == '\r')
          --o;
    }
  else
    OS (fatal, *expanding_var, _("file: invalid file operation: %s"), fn);

  return o;
}

static char *
func_abspath (char *o, char **argv, const char *funcname UNUSED)
{
  /* Expand the argument.  */
  const char *p = argv[0];
  const char *path = 0;
  int doneany = 0;
  unsigned int len = 0;

  while ((path = find_next_token (&p, &len)) != 0)
    {
      if (len < GET_PATH_MAX)
        {
          PATH_VAR (in);
          PATH_VAR (out);

          strncpy (in, path, len);
          in[len] = '\0';

          if (abspath (in, out))
            {
              o = variable_buffer_output (o, out, strlen (out));
              o = variable_buffer_output (o, " ", 1);
              doneany = 1;
            }
        }
    }

  /* Kill last space.  */
  if (doneany)
    --o;

  return o;
}

#ifdef CONFIG_WITH_ABSPATHEX
/* Same as abspath except that the current path may be given as the
   2nd argument. */
static char *
func_abspathex (char *o, char **argv, const char *funcname UNUSED)
{
  char *cwd = argv[1];

  /* cwd needs leading spaces chopped and may be optional,
     in which case we're exactly like $(abspath ). */
  if (cwd)
    while (ISBLANK (*cwd))
      cwd++;
  if (!cwd || !*cwd)
    o = func_abspath (o, argv, funcname);
  else
    {
      /* Expand the argument.  */
      const char *p = argv[0];
      unsigned int cwd_len = ~0U;
      char *path = 0;
      int doneany = 0;
      unsigned int len = 0;
      PATH_VAR (in);
      PATH_VAR (out);

      while ((path = find_next_token (&p, &len)) != 0)
        {
          if (len < GET_PATH_MAX)
            {
#ifdef HAVE_DOS_PATHS
              if (path[0] != '/' && path[0] != '\\' && (len < 2 || path[1] != ':') && cwd)
#else
              if (path[0] != '/' && cwd)
#endif
                {
                  /* relative path, prefix with cwd. */
                  if (cwd_len == ~0U)
                    cwd_len = strlen (cwd);
                  if (cwd_len + len + 1 >= GET_PATH_MAX)
                      continue;
                  memcpy (in, cwd, cwd_len);
                  in[cwd_len] = '/';
                  memcpy (in + cwd_len + 1, path, len);
                  in[cwd_len + len + 1] = '\0';
                }
              else
                {
                  /* absolute path pass it as-is. */
                  memcpy (in, path, len);
                  in[len] = '\0';
                }

              if (abspath (in, out))
                {
                  o = variable_buffer_output (o, out, strlen (out));
                  o = variable_buffer_output (o, " ", 1);
                  doneany = 1;
                }
            }
        }

      /* Kill last space.  */
      if (doneany)
        --o;
    }

   return o;
}
#endif

#ifdef CONFIG_WITH_XARGS
/* Create one or more command lines avoiding the max argument
   length restriction of the host OS.

   The last argument is the list of arguments that the normal
   xargs command would be fed from stdin.

   The first argument is initial command and it's arguments.

   If there are three or more arguments, the 2nd argument is
   the command and arguments to be used on subsequent
   command lines. Defaults to the initial command.

   If there are four or more arguments, the 3rd argument is
   the command to be used at the final command line. Defaults
   to the sub sequent or initial command .

   A future version of this function may define more arguments
   and therefor anyone specifying six or more arguments will
   cause fatal errors.

   Typical usage is:
        $(xargs ar cas mylib.a,$(objects))
   or
        $(xargs ar cas mylib.a,ar as mylib.a,$(objects))

   It will then create one or more "ar mylib.a ..." command
   lines with proper \n\t separation so it can be used when
   writing rules. */
static char *
func_xargs (char *o, char **argv, const char *funcname UNUSED)
{
  int argc;
  const char *initial_cmd;
  size_t initial_cmd_len;
  const char *subsequent_cmd;
  size_t subsequent_cmd_len;
  const char *final_cmd;
  size_t final_cmd_len;
  const char *args;
  size_t max_args;
  int i;

#ifdef ARG_MAX
  /* ARG_MAX is a bit unreliable (environment), so drop 25% of the max. */
# define XARGS_MAX  (ARG_MAX - (ARG_MAX / 4))
#else /* FIXME: update configure with a command line length test. */
# define XARGS_MAX  10240
#endif

  argc = 0;
  while (argv[argc])
    argc++;
  if (argc > 4)
    O (fatal, NILF, _("Too many arguments for $(xargs)!\n"));

  /* first: the initial / default command.*/
  initial_cmd = argv[0];
  while (ISSPACE (*initial_cmd))
    initial_cmd++;
  max_args = initial_cmd_len = strlen (initial_cmd);

  /* second: the command for the subsequent command lines. defaults to the initial cmd. */
  subsequent_cmd = argc > 2 && argv[1][0] != '\0' ? argv[1] : "\0";
  while (ISSPACE (*subsequent_cmd))
    subsequent_cmd++;   /* gcc 7.3.0 complains "offset ‘1’ outside bounds of constant string" if constant is "" rather than "\0". */
  if (*subsequent_cmd)
    {
      subsequent_cmd_len = strlen (subsequent_cmd);
      if (subsequent_cmd_len > max_args)
        max_args = subsequent_cmd_len;
    }
  else
    {
      subsequent_cmd = initial_cmd;
      subsequent_cmd_len = initial_cmd_len;
    }

  /* third: the final command. defaults to the subseq cmd. */
  final_cmd = argc > 3 && argv[2][0] != '\0' ? argv[2] : "\0";
  while (ISSPACE (*final_cmd))
    final_cmd++;    /* gcc 7.3.0: same complaint as for subsequent_cmd++ */
  if (*final_cmd)
    {
      final_cmd_len = strlen (final_cmd);
      if (final_cmd_len > max_args)
        max_args = final_cmd_len;
    }
  else
    {
      final_cmd = subsequent_cmd;
      final_cmd_len = subsequent_cmd_len;
    }

  /* last: the arguments to split up into sensible portions. */
  args = argv[argc - 1];

  /* calc the max argument length. */
  if (XARGS_MAX <= max_args + 2)
    ONN (fatal, NILF, _("$(xargs): the commands are longer than the max exec argument length. (%lu <= %lu)\n"),
         (unsigned long)XARGS_MAX, (unsigned long)max_args + 2);
  max_args = XARGS_MAX - max_args - 1;

  /* generate the commands. */
  i = 0;
  for (i = 0; ; i++)
    {
      unsigned int len;
      const char *iterator = args;
      const char *end = args;
      const char *cur;
      const char *tmp;

      /* scan the arguments till we reach the end or the max length. */
      while ((cur = find_next_token(&iterator, &len))
          && (size_t)((cur + len) - args) < max_args)
        end = cur + len;
      if (cur && end == args)
        O (fatal, NILF, _("$(xargs): command + one single arg is too much. giving up.\n"));

      /* emit the command. */
      if (i == 0)
        {
          o = variable_buffer_output (o, (char *)initial_cmd, initial_cmd_len);
          o = variable_buffer_output (o, " ", 1);
        }
      else if (cur)
        {
          o = variable_buffer_output (o, "\n\t", 2);
          o = variable_buffer_output (o, (char *)subsequent_cmd, subsequent_cmd_len);
          o = variable_buffer_output (o, " ", 1);
        }
      else
        {
          o = variable_buffer_output (o, "\n\t", 2);
          o = variable_buffer_output (o, (char *)final_cmd, final_cmd_len);
          o = variable_buffer_output (o, " ", 1);
        }

      tmp = end;
      while (tmp > args && ISSPACE (tmp[-1])) /* drop trailing spaces. */
        tmp--;
      o = variable_buffer_output (o, (char *)args, tmp - args);


      /* next */
      if (!cur)
        break;
      args = end;
      while (ISSPACE (*args))
        args++;
    }

  return o;
}
#endif

#ifdef CONFIG_WITH_STRING_FUNCTIONS
/*
  $(length string)

  XXX: This doesn't take multibyte locales into account.
 */
static char *
func_length (char *o, char **argv, const char *funcname UNUSED)
{
  size_t len = strlen (argv[0]);
  return math_int_to_variable_buffer (o, len);
}

/*
  $(length-var var)

  XXX: This doesn't take multibyte locales into account.
 */
static char *
func_length_var (char *o, char **argv, const char *funcname UNUSED)
{
  struct variable *var = lookup_variable (argv[0], strlen (argv[0]));
  return math_int_to_variable_buffer (o, var ? var->value_length : 0);
}

/* func_insert and func_substr helper. */
static char *
helper_pad (char *o, size_t to_add, const char *pad, size_t pad_len)
{
  while (to_add > 0)
    {
      size_t size = to_add > pad_len ? pad_len : to_add;
      o = variable_buffer_output (o, pad, size);
      to_add -= size;
    }
  return o;
}

/*
  $(insert in, str[, n[, length[, pad]]])

  XXX: This doesn't take multibyte locales into account.
 */
static char *
func_insert (char *o, char **argv, const char *funcname UNUSED)
{
  const char *in      = argv[0];
  math_int    in_len  = (math_int)strlen (in);
  const char *str     = argv[1];
  math_int    str_len = (math_int)strlen (str);
  math_int    n       = 0;
  math_int    length  = str_len;
  const char *pad     = "                ";
  size_t      pad_len = 16;
  size_t      i;

  if (argv[2] != NULL)
    {
      n = math_int_from_string (argv[2]);
      if (n > 0)
        n--;            /* one-origin */
      else if (n == 0)
        n = str_len;    /* append */
      else
        { /* n < 0: from the end */
          n = str_len + n;
          if (n < 0)
            n = 0;
        }
      if (n > 16*1024*1024) /* 16MB */
        OS (fatal, NILF, _("$(insert ): n=%s is out of bounds\n"), argv[2]);

      if (argv[3] != NULL)
        {
          length = math_int_from_string (argv[3]);
          if (length < 0 || length > 16*1024*1024 /* 16MB */)
              OS (fatal, NILF, _("$(insert ): length=%s is out of bounds\n"), argv[3]);

          if (argv[4] != NULL)
            {
              const char *tmp = argv[4];
              for (i = 0; tmp[i] == ' '; i++)
                /* nothing */;
              if (tmp[i] != '\0')
                {
                  pad = argv[4];
                  pad_len = strlen (pad);
                }
              /* else: it was all default spaces. */
            }
        }
    }

  /* the head of the original string */
  if (n > 0)
    {
      if (n <= str_len)
        o = variable_buffer_output (o, str, n);
      else
        {
          o = variable_buffer_output (o, str, str_len);
          o = helper_pad (o, n - str_len, pad, pad_len);
        }
    }

  /* insert the string */
  if (length <= in_len)
    o = variable_buffer_output (o, in, length);
  else
    {
      o = variable_buffer_output (o, in, in_len);
      o = helper_pad (o, length - in_len, pad, pad_len);
    }

  /* the tail of the original string */
  if (n < str_len)
    o = variable_buffer_output (o, str + n, str_len - n);

  return o;
}

/*
  $(pos needle, haystack[, start])
  $(lastpos needle, haystack[, start])

  XXX: This doesn't take multibyte locales into account.
 */
static char *
func_pos (char *o, char **argv, const char *funcname UNUSED)
{
  const char *needle       = *argv[0] ? argv[0] : " ";
  size_t      needle_len   = strlen (needle);
  const char *haystack     = argv[1];
  size_t      haystack_len = strlen (haystack);
  math_int    start        = 0;
  const char *hit;

  if (argv[2] != NULL)
    {
      start = math_int_from_string (argv[2]);
      if (start > 0)
        start--;            /* one-origin */
      else if (start < 0)
        start = haystack_len + start; /* from the end */
      if (start < 0 || start + needle_len > haystack_len)
        return math_int_to_variable_buffer (o, 0);
    }
  else if (funcname[0] == 'l')
    start = haystack_len - 1;

  /* do the searching */
  if (funcname[0] != 'l')
    { /* pos */
      if (needle_len == 1)
        hit = strchr (haystack + start, *needle);
      else
        hit = strstr (haystack + start, needle);
    }
  else
    { /* last pos */
      int    ch  = *needle;
      size_t off = start + 1;

      hit = NULL;
      while (off-- > 0)
        {
          if (   haystack[off] == ch
              && (   needle_len == 1
                  || strncmp (&haystack[off], needle, needle_len) == 0))
            {
              hit = haystack + off;
              break;
            }
        }
    }

  return math_int_to_variable_buffer (o, hit ? hit - haystack + 1 : 0);
}

/*
  $(substr str, start[, length[, pad]])

  XXX: This doesn't take multibyte locales into account.
 */
static char *
func_substr (char *o, char **argv, const char *funcname UNUSED)
{
  const char *str     = argv[0];
  math_int    str_len = (math_int)strlen (str);
  math_int    start   = math_int_from_string (argv[1]);
  math_int    length  = 0;
  const char *pad     = NULL;
  size_t      pad_len = 0;

  if (argv[2] != NULL)
    {
      if (argv[3] != NULL)
        {
          pad = argv[3];
          for (pad_len = 0; pad[pad_len] == ' '; pad_len++)
            /* nothing */;
          if (pad[pad_len] != '\0')
              pad_len = strlen (pad);
          else
            {
              pad = "                ";
              pad_len = 16;
            }
        }
      length = math_int_from_string (argv[2]);
      if (pad != NULL && length > 16*1024*1024 /* 16MB */)
        OS (fatal, NILF, _("$(substr ): length=%s is out of bounds\n"), argv[2]);
      if (pad != NULL && length < 0)
        OS (fatal, NILF, _("$(substr ): negative length (%s) and padding doesn't mix.\n"), argv[2]);
      if (length == 0)
        return o;
    }

  /* Note that negative start and length are used for referencing from the
     end of the string. */
  if (pad == NULL)
    {
      if (start > 0)
        start--;      /* one-origin */
      else
        {
          start = str_len + start;
          if (start <= 0)
            {
              if (length < 0)
                return o;
              start += length;
              if (start <= 0)
                return o;
              length = start;
              start = 0;
            }
        }

      if (start >= str_len)
        return o;
      if (length == 0)
        length = str_len - start;
      else if (length < 0)
        {
          if (str_len <= -length)
            return o;
          length += str_len;
          if (length <= start)
            return o;
          length -= start;
        }
      else if (start + length > str_len)
        length = str_len - start;

      o = variable_buffer_output (o, str + start, length);
    }
  else
    {
      if (start > 0)
        {
          start--;      /* one-origin */
          if (start >= str_len)
            return length ? helper_pad (o, length, pad, pad_len) : o;
          if (length == 0)
            length = str_len - start;
        }
      else
        {
          start = str_len + start;
          if (start <= 0)
            {
              if (start + length <= 0)
                return length ? helper_pad (o, length, pad, pad_len) : o;
              o = helper_pad (o, -start, pad, pad_len);
              return variable_buffer_output (o, str, length + start);
            }
          if (length == 0)
            length = str_len - start;
        }
      if (start + length <= str_len)
        o = variable_buffer_output (o, str + start, length);
      else
        {
          o = variable_buffer_output (o, str + start, str_len - start);
          o = helper_pad (o, start + length - str_len, pad, pad_len);
        }
    }

  return o;
}

/*
  $(translate string, from-set[, to-set[, pad-char]])

  XXX: This doesn't take multibyte locales into account.
 */
static char *
func_translate (char *o, char **argv, const char *funcname UNUSED)
{
  const unsigned char *str      = (const unsigned char *)argv[0];
  const unsigned char *from_set = (const unsigned char *)argv[1];
  const char          *to_set   = argv[2] != NULL ? argv[2] : "";
  char                 trans_tab[1 << CHAR_BIT];
  int                  i;
  char                 ch;

  /* init the array. */
  for (i = 0; i < (1 << CHAR_BIT); i++)
    trans_tab[i] = i;

  while (   (i = *from_set) != '\0'
         && (ch = *to_set) != '\0')
    {
      trans_tab[i] = ch;
      from_set++;
      to_set++;
    }

  if (i != '\0')
    {
      ch = '\0';                        /* no padding == remove char */
      if (argv[2] != NULL && argv[3] != NULL)
        {
          ch = argv[3][0];
          if (ch && argv[3][1])
            OS (fatal, NILF, _("$(translate ): pad=`%s' expected a single char\n"), argv[3]);
          if (ch == '\0')               /* no char == space */
            ch = ' ';
        }
      while ((i = *from_set++) != '\0')
        trans_tab[i] = ch;
    }

  /* do the translation */
  while ((i = *str++) != '\0')
    {
      ch = trans_tab[i];
      if (ch)
        o = variable_buffer_output (o, &ch, 1);
    }

  return o;
}
#endif /* CONFIG_WITH_STRING_FUNCTIONS */

#ifdef CONFIG_WITH_DEFINED
/* Similar to ifdef. */
static char *
func_defined (char *o, char **argv, const char *funcname UNUSED)
{
  struct variable *v = lookup_variable (argv[0], strlen (argv[0]));
  int result = v != NULL && *v->value != '\0';
  o = variable_buffer_output (o,  result ? "1" : "", result);
  return o;
}
#endif /* CONFIG_WITH_DEFINED*/

#ifdef CONFIG_WITH_TOUPPER_TOLOWER
static char *
func_toupper_tolower (char *o, char **argv, const char *funcname)
{
  /* Expand the argument.  */
  const char *p = argv[0];
  while (*p)
    {
      /* convert to temporary buffer */
      char tmp[256];
      unsigned int i;
      if (!strcmp(funcname, "toupper"))
        for (i = 0; i < sizeof(tmp) && *p; i++, p++)
          tmp[i] = toupper(*p);
      else
        for (i = 0; i < sizeof(tmp) && *p; i++, p++)
          tmp[i] = tolower(*p);
      o = variable_buffer_output (o, tmp, i);
    }

  return o;
}
#endif /* CONFIG_WITH_TOUPPER_TOLOWER */

#if defined(CONFIG_WITH_VALUE_LENGTH) && defined(CONFIG_WITH_COMPARE)

/* Strip leading spaces and other things off a command. */
static const char *
comp_cmds_strip_leading (const char *s, const char *e)
{
    while (s < e)
      {
        const char ch = *s;
        if (!ISBLANK (ch)
         && ch != '@'
#ifdef CONFIG_WITH_COMMANDS_FUNC
         && ch != '%'
#endif
         && ch != '+'
         && ch != '-')
          break;
        s++;
      }
    return s;
}

/* Worker for func_comp_vars() which is called if the comparision failed.
   It will do the slow command by command comparision of the commands
   when there invoked as comp-cmds. */
static char *
comp_vars_ne (char *o, const char *s1, const char *e1, const char *s2, const char *e2,
              char *ne_retval, const char *funcname)
{
    /* give up at once if not comp-cmds or comp-cmds-ex. */
    if (strcmp (funcname, "comp-cmds") != 0
     && strcmp (funcname, "comp-cmds-ex") != 0)
      o = variable_buffer_output (o, ne_retval, strlen (ne_retval));
    else
      {
        const char * const s1_start = s1;
        int new_cmd = 1;
        int diff;
        for (;;)
          {
            /* if it's a new command, strip leading stuff. */
            if (new_cmd)
              {
                s1 = comp_cmds_strip_leading (s1, e1);
                s2 = comp_cmds_strip_leading (s2, e2);
                new_cmd = 0;
              }
            if (s1 >= e1 || s2 >= e2)
              break;

            /*
             * Inner compare loop which compares one line.
             * FIXME: parse quoting!
             */
            for (;;)
              {
                const char ch1 = *s1;
                const char ch2 = *s2;
                diff = ch1 - ch2;
                if (diff)
                  break;
                if (ch1 == '\n')
                  break;
                assert (ch1 != '\r');

                /* next */
                s1++;
                s2++;
                if (s1 >= e1 || s2 >= e2)
                  break;
              }

            /*
             * If we exited because of a difference try to end-of-command
             * comparision, e.g. ignore trailing spaces.
             */
            if (diff)
              {
                /* strip */
                while (s1 < e1 && ISBLANK (*s1))
                  s1++;
                while (s2 < e2 && ISBLANK (*s2))
                  s2++;
                if (s1 >= e1 || s2 >= e2)
                  break;

                /* compare again and check that it's a newline. */
                if (*s2 != '\n' || *s1 != '\n')
                  break;
              }
            /* Break out if we exited because of EOS. */
            else if (s1 >= e1 || s2 >= e2)
                break;

            /*
             * Detect the end of command lines.
             */
            if (*s1 == '\n')
              new_cmd = s1 == s1_start || s1[-1] != '\\';
            s1++;
            s2++;
          }

        /*
         * Ignore trailing empty lines.
         */
        if (s1 < e1 || s2 < e2)
          {
            while (s1 < e1 && (ISBLANK (*s1) || *s1 == '\n'))
              if (*s1++ == '\n')
                s1 = comp_cmds_strip_leading (s1, e1);
            while (s2 < e2 && (ISBLANK (*s2) || *s2 == '\n'))
              if (*s2++ == '\n')
                s2 = comp_cmds_strip_leading (s2, e2);
          }

        /* emit the result. */
        if (s1 == e1 && s2 == e2)
          o = variable_buffer_output (o, "", 1) - 1; /** @todo check why this was necessary back the... */
        else
          o = variable_buffer_output (o, ne_retval, strlen (ne_retval));
      }
    return o;
}

/*
    $(comp-vars var1,var2,not-equal-return)
  or
    $(comp-cmds cmd-var1,cmd-var2,not-equal-return)

  Compares the two variables (that's given by name to avoid unnecessary
  expanding) and return the string in the third argument if not equal.
  If equal, nothing is returned.

  comp-vars will to an exact comparision only stripping leading and
  trailing spaces.

  comp-cmds will compare command by command, ignoring not only leading
  and trailing spaces on each line but also leading one leading '@',
  '-', '+' and '%'
*/
static char *
func_comp_vars (char *o, char **argv, const char *funcname)
{
  const char *s1, *e1, *x1, *s2, *e2, *x2;
  char *a1 = NULL, *a2 = NULL;
  size_t l, l1, l2;
  struct variable *var1 = lookup_variable (argv[0], strlen (argv[0]));
  struct variable *var2 = lookup_variable (argv[1], strlen (argv[1]));

  /* the simple cases */
  if (var1 == var2)
    return variable_buffer_output (o, "", 0);       /* eq */
  if (!var1 || !var2)
    return variable_buffer_output (o, argv[2], strlen(argv[2]));
  if (var1->value == var2->value)
    return variable_buffer_output (o, "", 0);       /* eq */
  if (   (!var1->recursive || IS_VARIABLE_RECURSIVE_WITHOUT_DOLLAR (var1))
      && (!var2->recursive || IS_VARIABLE_RECURSIVE_WITHOUT_DOLLAR (var2)) )
  {
    if (    var1->value_length == var2->value_length
        &&  !memcmp (var1->value, var2->value, var1->value_length))
      return variable_buffer_output (o, "", 0);     /* eq */

    /* ignore trailing and leading blanks */
    s1 = var1->value;
    e1 = s1 + var1->value_length;
    while (ISBLANK (*s1))
      s1++;
    while (e1 > s1 && ISBLANK (e1[-1]))
      e1--;

    s2 = var2->value;
    e2 = s2 + var2->value_length;
    while (ISBLANK (*s2))
      s2++;
    while (e2 > s2 && ISBLANK (e2[-1]))
      e2--;

    if (e1 - s1 != e2 - s2)
      return comp_vars_ne (o, s1, e1, s2, e2, argv[2], funcname);
    if (!memcmp (s1, s2, e1 - s1))
      return variable_buffer_output (o, "", 0);     /* eq */
    return comp_vars_ne (o, s1, e1, s2, e2, argv[2], funcname);
  }

  /* ignore trailing and leading blanks */
  s1 = var1->value;
  e1 = s1 + var1->value_length;
  while (ISBLANK (*s1))
    s1++;
  while (e1 > s1 && ISBLANK (e1[-1]))
    e1--;

  s2 = var2->value;
  e2 = s2 + var2->value_length;
  while (ISBLANK (*s2))
    s2++;
  while (e2 > s2 && ISBLANK (e2[-1]))
    e2--;

  /* both empty after stripping? */
  if (s1 == e1 && s2 == e2)
    return variable_buffer_output (o, "", 0);       /* eq */

  /* optimist. */
  if (   e1 - s1 == e2 - s2
      && !memcmp(s1, s2, e1 - s1))
    return variable_buffer_output (o, "", 0);       /* eq */

  /* compare up to the first '$' or the end. */
  x1 = var1->recursive ? memchr (s1, '$', e1 - s1) : NULL;
  x2 = var2->recursive ? memchr (s2, '$', e2 - s2) : NULL;
  if (!x1 && !x2)
    return comp_vars_ne (o, s1, e1, s2, e2, argv[2], funcname);

  l1 = x1 ? x1 - s1 : e1 - s1;
  l2 = x2 ? x2 - s2 : e2 - s2;
  l = l1 <= l2 ? l1 : l2;
  if (l && memcmp (s1, s2, l))
    return comp_vars_ne (o, s1, e1, s2, e2, argv[2], funcname);

  /* one or both buffers now require expanding. */
  if (!x1)
    s1 += l;
  else
    {
      s1 = a1 = allocated_variable_expand ((char *)s1 + l);
      if (!l)
        while (ISBLANK (*s1))
          s1++;
      e1 = strchr (s1, '\0');
      while (e1 > s1 && ISBLANK (e1[-1]))
        e1--;
    }

  if (!x2)
    s2 += l;
  else
    {
      s2 = a2 = allocated_variable_expand ((char *)s2 + l);
      if (!l)
        while (ISBLANK (*s2))
          s2++;
      e2 = strchr (s2, '\0');
      while (e2 > s2 && ISBLANK (e2[-1]))
        e2--;
    }

  /* the final compare */
  if (   e1 - s1 != e2 - s2
      || memcmp (s1, s2, e1 - s1))
      o = comp_vars_ne (o, s1, e1, s2, e2, argv[2], funcname);
  else
      o = variable_buffer_output (o, "", 1) - 1;    /* eq */ /** @todo check why this was necessary back the... */
  if (a1)
    free (a1);
  if (a2)
    free (a2);
  return o;
}

/*
  $(comp-cmds-ex cmds1,cmds2,not-equal-return)

  Compares the two strings and return the string in the third argument
  if not equal. If equal, nothing is returned.

  The comparision will be performed command by command, ignoring not
  only leading and trailing spaces on each line but also leading one
  leading '@', '-', '+' and '%'.
*/
static char *
func_comp_cmds_ex (char *o, char **argv, const char *funcname)
{
  const char *s1, *e1, *s2, *e2;
  size_t l1, l2;

  /* the simple cases */
  s1 = argv[0];
  s2 = argv[1];
  if (s1 == s2)
    return variable_buffer_output (o, "", 0);       /* eq */
  l1 = strlen (argv[0]);
  l2 = strlen (argv[1]);

  if (    l1 == l2
      &&  !memcmp (s1, s2, l1))
    return variable_buffer_output (o, "", 0);       /* eq */

  /* ignore trailing and leading blanks */
  e1 = s1 + l1;
  s1 = comp_cmds_strip_leading (s1, e1);

  e2 = s2 + l2;
  s2 = comp_cmds_strip_leading (s2, e2);

  if (e1 - s1 != e2 - s2)
    return comp_vars_ne (o, s1, e1, s2, e2, argv[2], funcname);
  if (!memcmp (s1, s2, e1 - s1))
    return variable_buffer_output (o, "", 0);       /* eq */
  return comp_vars_ne (o, s1, e1, s2, e2, argv[2], funcname);
}
#endif

#ifdef CONFIG_WITH_DATE
# if defined (_MSC_VER) /* FIXME: !defined (HAVE_STRPTIME) */
char *strptime(const char *s, const char *format, struct tm *tm)
{
  return (char *)"strptime is not implemented";
}
# endif
/* Check if the string is all blanks or not. */
static int
all_blanks (const char *s)
{
  if (!s)
    return 1;
  while (ISSPACE (*s))
    s++;
  return *s == '\0';
}

/* The first argument is the strftime format string, a iso
   timestamp is the default if nothing is given.

   The second argument is a time value if given. The format
   is either the format from the first argument or given as
   an additional third argument. */
static char *
func_date (char *o, char **argv, const char *funcname)
{
  char *p;
  char *buf;
  size_t buf_size;
  struct tm t;
  const char *format;

  /* determin the format - use a single word as the default. */
  format = !strcmp (funcname, "date-utc")
         ? "%Y-%m-%dT%H:%M:%SZ"
         : "%Y-%m-%dT%H:%M:%S";
  if (!all_blanks (argv[0]))
    format = argv[0];

  /* get the time. */
  memset (&t, 0, sizeof(t));
  if (argv[0] && !all_blanks (argv[1]))
    {
      const char *input_format = !all_blanks (argv[2]) ? argv[2] : format;
      p = strptime (argv[1], input_format, &t);
      if (!p || *p != '\0')
        {
          OSSSS (error, NILF, _("$(%s): strptime(%s,%s,) -> %s\n"), funcname,
                 argv[1], input_format, p ? p : "<null>");
          return variable_buffer_output (o, "", 0);
        }
    }
  else
    {
      time_t tval;
      time (&tval);
      if (!strcmp (funcname, "date-utc"))
        t = *gmtime (&tval);
      else
        t = *localtime (&tval);
    }

  /* format it. note that zero isn't necessarily an error, so we'll
     have to keep shut about failures. */
  buf_size = 64;
  buf = xmalloc (buf_size);
  while (strftime (buf, buf_size, format, &t) == 0)
    {
      if (buf_size >= 4096)
        {
          *buf = '\0';
          break;
        }
      buf = xrealloc (buf, buf_size <<= 1);
    }
  o = variable_buffer_output (o, buf, strlen (buf));
  free (buf);
  return o;
}
#endif

#ifdef CONFIG_WITH_FILE_SIZE
/* Prints the size of the specified file. Only one file is
   permitted, notthing is stripped. -1 is returned if stat
   fails. */
static char *
func_file_size (char *o, char **argv, const char *funcname UNUSED)
{
  struct stat st;
  if (stat (argv[0], &st))
    return variable_buffer_output (o, "-1", 2);
  return math_int_to_variable_buffer (o, st.st_size);
}
#endif

#ifdef CONFIG_WITH_WHICH
/* Checks if the specified file exists an is executable.
   On systems employing executable extensions, the name may
   be modified to include the extension. */
static int func_which_test_x (char *file)
{
  struct stat st;
# if defined(WINDOWS32) || defined(__OS2__)
  char *ext;
  char *slash;

  /* fix slashes first. */
  slash = file;
  while ((slash = strchr (slash, '\\')) != NULL)
    *slash++ = '/';

  /* straight */
  if (stat (file, &st) == 0
    && S_ISREG (st.st_mode))
    return 1;

  /* don't try add an extension if there already is one */
  ext = strchr (file, '\0');
  if (ext - file >= 4
   && (   !stricmp (ext - 4, ".exe")
       || !stricmp (ext - 4, ".cmd")
       || !stricmp (ext - 4, ".bat")
       || !stricmp (ext - 4, ".com")))
    return 0;

  /* try the extensions. */
  strcpy (ext, ".exe");
  if (stat (file, &st) == 0
    && S_ISREG (st.st_mode))
    return 1;

  strcpy (ext, ".cmd");
  if (stat (file, &st) == 0
    && S_ISREG (st.st_mode))
    return 1;

  strcpy (ext, ".bat");
  if (stat (file, &st) == 0
    && S_ISREG (st.st_mode))
    return 1;

  strcpy (ext, ".com");
  if (stat (file, &st) == 0
    && S_ISREG (st.st_mode))
    return 1;

  return 0;

# else

  return access (file, X_OK) == 0
     && stat (file, &st) == 0
     && S_ISREG (st.st_mode);
# endif
}

/* Searches for the specified programs in the PATH and print
   their full location if found. Prints nothing if not found. */
static char *
func_which (char *o, char **argv, const char *funcname UNUSED)
{
  const char *path;
  struct variable *path_var;
  unsigned i;
  int first = 1;
  PATH_VAR (buf);

  path_var = lookup_variable ("PATH", 4);
  if (path_var)
    path = path_var->value;
  else
    path = ".";

  /* iterate input */
  for (i = 0; argv[i]; i++)
    {
      unsigned int len;
      const char *iterator = argv[i];
      char *cur;

      while ((cur = find_next_token (&iterator, &len)))
        {
          /* if there is a separator, don't walk the path. */
          if (memchr (cur, '/', len)
#ifdef HAVE_DOS_PATHS
           || memchr (cur, '\\', len)
           || memchr (cur, ':', len)
#endif
             )
            {
              if (len + 1 + 4 < GET_PATH_MAX) /* +4 for .exe */
                {
                  memcpy (buf, cur, len);
                  buf[len] = '\0';
                  if (func_which_test_x (buf))
                    o = variable_buffer_output (o, buf, strlen (buf));
                }
            }
          else
            {
              const char *comp = path;
              for (;;)
                {
                  const char *src = comp;
                  const char *end = strchr (comp, PATH_SEPARATOR_CHAR);
                  size_t src_len = end ? (size_t)(end - comp) : strlen (comp);
                  if (!src_len)
                    {
                      src_len = 1;
                      src = ".";
                    }
                  if (len + src_len + 2 + 4 < GET_PATH_MAX) /* +4 for .exe */
                    {
                      memcpy (buf, src, src_len);
                      buf [src_len] = '/';
                      memcpy (&buf[src_len + 1], cur, len);
                      buf[src_len + 1 + len] = '\0';

                      if (func_which_test_x (buf))
                        {
                          if (!first)
                            o = variable_buffer_output (o, " ", 1);
                          o = variable_buffer_output (o, buf, strlen (buf));
                          first = 0;
                          break;
                        }
                    }

                  /* next */
                  if (!end)
                    break;
                  comp = end + 1;
                }
            }
        }
    }

  return variable_buffer_output (o, "", 0);
}
#endif /* CONFIG_WITH_WHICH */

#ifdef CONFIG_WITH_IF_CONDITIONALS

/* Evaluates the expression given in the argument using the
   same evaluator as for the new 'if' statements, except now
   we don't force the result into a boolean like for 'if' and
   '$(if-expr ,,)'. */
static char *
func_expr (char *o, char **argv, const char *funcname UNUSED)
{
  o = expr_eval_to_string (o, argv[0]);
  return o;
}

/* Same as '$(if ,,)' except the first argument is evaluated
   using the same evaluator as for the new 'if' statements. */
static char *
func_if_expr (char *o, char **argv, const char *funcname UNUSED)
{
  int rc;
  char *to_expand;

  /* Evaluate the condition in argv[0] and expand the 2nd or
     3rd (optional) argument according to the result. */
  rc = expr_eval_if_conditionals (argv[0], NULL);
  to_expand = rc == 0 ? argv[1] : argv[2];
  if (to_expand && *to_expand)
    variable_expand_string_2 (o, to_expand, -1, &o);

  return o;
}

/*
  $(select when1-cond, when1-body[,whenN-cond, whenN-body]).
  */
static char *
func_select (char *o, char **argv, const char *funcname UNUSED)
{
  int i;

  /* Test WHEN-CONDs until one matches. The check for 'otherwise[:]'
     and 'default[:]' make this a bit more fun... */

  for (i = 0; argv[i] != NULL; i += 2)
    {
      const char *cond = argv[i];
      int is_otherwise = 0;

      if (argv[i + 1] == NULL)
        O (fatal, NILF, _("$(select ): not an even argument count\n"));

      while (ISSPACE (*cond))
        cond++;
      if (   (*cond == 'o' && strncmp (cond, "otherwise", 9) == 0)
          || (*cond == 'd' && strncmp (cond, "default",   7) == 0))
        {
          const char *end = cond + (*cond == 'o' ? 9 : 7);
          while (ISSPACE (*end))
            end++;
          if (*end == ':')
            do end++;
            while (ISSPACE (*end));
          is_otherwise = *end == '\0';
        }

      if (   is_otherwise
          || expr_eval_if_conditionals (cond, NULL) == 0 /* true */)
        {
          variable_expand_string_2 (o, argv[i + 1], -1, &o);
          break;
        }
    }

  return o;
}

#endif /* CONFIG_WITH_IF_CONDITIONALS */

#ifdef CONFIG_WITH_SET_CONDITIONALS
static char *
func_set_intersects (char *o, char **argv, const char *funcname UNUSED)
{
  const char *s1_cur;
  unsigned int s1_len;
  const char *s1_iterator = argv[0];

  while ((s1_cur = find_next_token (&s1_iterator, &s1_len)) != 0)
    {
      const char *s2_cur;
      unsigned int s2_len;
      const char *s2_iterator = argv[1];
      while ((s2_cur = find_next_token (&s2_iterator, &s2_len)) != 0)
        if (s2_len == s1_len
         && strneq (s2_cur, s1_cur, s1_len) )
          return variable_buffer_output (o, "1", 1); /* found intersection */
    }

  return o; /* no intersection */
}
#endif /* CONFIG_WITH_SET_CONDITIONALS */

#ifdef CONFIG_WITH_STACK

/* Push an item (string without spaces). */
static char *
func_stack_push (char *o, char **argv, const char *funcname UNUSED)
{
  do_variable_definition(NILF, argv[0], argv[1], o_file, f_append, 0 /* !target_var */);
  return o;
}

/* Pops an item off the stack / get the top stack element.
   (This is what's tricky to do in pure GNU make syntax.) */
static char *
func_stack_pop_top (char *o, char **argv, const char *funcname)
{
  struct variable *stack_var;
  const char *stack = argv[0];

  stack_var = lookup_variable (stack, strlen (stack) );
  if (stack_var)
    {
      unsigned int len;
      const char *iterator = stack_var->value;
      char *lastitem = NULL;
      char *cur;

      while ((cur = find_next_token (&iterator, &len)))
        lastitem = cur;

      if (lastitem != NULL)
        {
          if (strcmp (funcname, "stack-popv") != 0)
            o = variable_buffer_output (o, lastitem, len);
          if (strcmp (funcname, "stack-top") != 0)
            {
              *lastitem = '\0';
              while (lastitem > stack_var->value && ISSPACE (lastitem[-1]))
                *--lastitem = '\0';
#ifdef CONFIG_WITH_VALUE_LENGTH
              stack_var->value_length = lastitem - stack_var->value;
#endif
              VARIABLE_CHANGED (stack_var);
            }
        }
    }
  return o;
}
#endif /* CONFIG_WITH_STACK */

#if defined (CONFIG_WITH_MATH) || defined (CONFIG_WITH_NANOTS) || defined (CONFIG_WITH_FILE_SIZE)
/* outputs the number (as a string) into the variable buffer. */
static char *
math_int_to_variable_buffer (char *o, math_int num)
{
  static const char xdigits[17] = "0123456789abcdef";
  int negative;
  char strbuf[24]; /* 16 hex + 2 prefix + sign + term => 20
                              or 20 dec + sign + term => 22 */
  char *str = &strbuf[sizeof (strbuf) - 1];

  negative = num < 0;
  if (negative)
    num = -num;

  *str = '\0';

  do
    {
#ifdef HEX_MATH_NUMBERS
      *--str = xdigits[num & 0xf];
      num >>= 4;
#else
      *--str = xdigits[num % 10];
      num /= 10;
#endif
    }
  while (num);

#ifdef HEX_MATH_NUMBERS
  *--str = 'x';
  *--str = '0';
#endif

  if (negative)
    *--str = '-';

  return variable_buffer_output (o, str, &strbuf[sizeof (strbuf) - 1] - str);
}
#endif /* CONFIG_WITH_MATH || CONFIG_WITH_NANOTS */

#ifdef CONFIG_WITH_MATH

/* Converts a string to an integer, causes an error if the format is invalid. */
static math_int
math_int_from_string (const char *str)
{
  const char *start;
  unsigned base = 0;
  int      negative = 0;
  math_int num = 0;

  /* strip spaces */
  while (ISSPACE (*str))
    str++;
  if (!*str)
    {
      O (error, NILF, _("bad number: empty\n"));
      return 0;
    }
  start = str;

  /* check for +/- */
  while (*str == '+' || *str == '-' || ISSPACE (*str))
      if (*str++ == '-')
        negative = !negative;

  /* check for prefix - we do not accept octal numbers, sorry. */
  if (*str == '0' && (str[1] == 'x' || str[1] == 'X'))
    {
      base = 16;
      str += 2;
    }
  else
    {
      /* look for a hex digit, if not found treat it as decimal */
      const char *p2 = str;
      for ( ; *p2; p2++)
        if (isxdigit (*p2) && !isdigit (*p2) && isascii (*p2) )
          {
            base = 16;
            break;
          }
      if (base == 0)
        base = 10;
    }

  /* must have at least one digit! */
  if (    !isascii (*str)
      ||  !(base == 16 ? isxdigit (*str) : isdigit (*str)) )
    {
      OS (error, NILF, _("bad number: '%s'\n"), start);
      return 0;
    }

  /* convert it! */
  while (*str && !ISSPACE (*str))
    {
      int ch = *str++;
      if (ch >= '0' && ch <= '9')
        ch -= '0';
      else if (base == 16 && ch >= 'a' && ch <= 'f')
        ch -= 'a' - 10;
      else if (base == 16 && ch >= 'A' && ch <= 'F')
        ch -= 'A' - 10;
      else
        {
          OSNN (error, NILF, _("bad number: '%s' (base=%u, pos=%lu)\n"), start, base, (unsigned long)(str - start));
          return 0;
        }
      num *= base;
      num += ch;
    }

  /* check trailing spaces. */
  while (ISSPACE (*str))
    str++;
  if (*str)
    {
      OS (error, NILF, _("bad number: '%s'\n"), start);
      return 0;
    }

  return negative ? -num : num;
}

/* Add two or more integer numbers. */
static char *
func_int_add (char *o, char **argv, const char *funcname UNUSED)
{
  math_int num;
  int i;

  num = math_int_from_string (argv[0]);
  for (i = 1; argv[i]; i++)
    num += math_int_from_string (argv[i]);

  return math_int_to_variable_buffer (o, num);
}

/* Subtract two or more integer numbers. */
static char *
func_int_sub (char *o, char **argv, const char *funcname UNUSED)
{
  math_int num;
  int i;

  num = math_int_from_string (argv[0]);
  for (i = 1; argv[i]; i++)
    num -= math_int_from_string (argv[i]);

  return math_int_to_variable_buffer (o, num);
}

/* Multiply two or more integer numbers. */
static char *
func_int_mul (char *o, char **argv, const char *funcname UNUSED)
{
  math_int num;
  int i;

  num = math_int_from_string (argv[0]);
  for (i = 1; argv[i]; i++)
    num *= math_int_from_string (argv[i]);

  return math_int_to_variable_buffer (o, num);
}

/* Divide an integer number by one or more divisors. */
static char *
func_int_div (char *o, char **argv, const char *funcname UNUSED)
{
  math_int num;
  math_int divisor;
  int i;

  num = math_int_from_string (argv[0]);
  for (i = 1; argv[i]; i++)
    {
      divisor = math_int_from_string (argv[i]);
      if (!divisor)
        {
          OS (error, NILF, _("divide by zero ('%s')\n"), argv[i]);
          return math_int_to_variable_buffer (o, 0);
        }
      num /= divisor;
    }

  return math_int_to_variable_buffer (o, num);
}


/* Divide and return the remainder. */
static char *
func_int_mod (char *o, char **argv, const char *funcname UNUSED)
{
  math_int num;
  math_int divisor;

  num = math_int_from_string (argv[0]);
  divisor = math_int_from_string (argv[1]);
  if (!divisor)
    {
      OS (error, NILF, _("divide by zero ('%s')\n"), argv[1]);
      return math_int_to_variable_buffer (o, 0);
    }
  num %= divisor;

  return math_int_to_variable_buffer (o, num);
}

/* 2-complement. */
static char *
func_int_not (char *o, char **argv, const char *funcname UNUSED)
{
  math_int num;

  num = math_int_from_string (argv[0]);
  num = ~num;

  return math_int_to_variable_buffer (o, num);
}

/* Bitwise AND (two or more numbers). */
static char *
func_int_and (char *o, char **argv, const char *funcname UNUSED)
{
  math_int num;
  int i;

  num = math_int_from_string (argv[0]);
  for (i = 1; argv[i]; i++)
    num &= math_int_from_string (argv[i]);

  return math_int_to_variable_buffer (o, num);
}

/* Bitwise OR (two or more numbers). */
static char *
func_int_or (char *o, char **argv, const char *funcname UNUSED)
{
  math_int num;
  int i;

  num = math_int_from_string (argv[0]);
  for (i = 1; argv[i]; i++)
    num |= math_int_from_string (argv[i]);

  return math_int_to_variable_buffer (o, num);
}

/* Bitwise XOR (two or more numbers). */
static char *
func_int_xor (char *o, char **argv, const char *funcname UNUSED)
{
  math_int num;
  int i;

  num = math_int_from_string (argv[0]);
  for (i = 1; argv[i]; i++)
    num ^= math_int_from_string (argv[i]);

  return math_int_to_variable_buffer (o, num);
}

/* Compare two integer numbers. Returns make boolean (true="1"; false=""). */
static char *
func_int_cmp (char *o, char **argv, const char *funcname)
{
    math_int num1;
    math_int num2;
    int rc;

    num1 = math_int_from_string (argv[0]);
    num2 = math_int_from_string (argv[1]);

    funcname += sizeof ("int-") - 1;
    if (!strcmp (funcname, "eq"))
      rc = num1 == num2;
    else if (!strcmp (funcname, "ne"))
      rc = num1 != num2;
    else if (!strcmp (funcname, "gt"))
      rc = num1 > num2;
    else if (!strcmp (funcname, "ge"))
      rc = num1 >= num2;
    else if (!strcmp (funcname, "lt"))
      rc = num1 < num2;
    else /*if (!strcmp (funcname, "le"))*/
      rc = num1 <= num2;

    return variable_buffer_output (o, rc ? "1" : "", rc);
}

#endif /* CONFIG_WITH_MATH */

#ifdef CONFIG_WITH_NANOTS
/* Returns the current timestamp as nano seconds. The time
   source is a high res monotone one if the platform provides
   this (and we know about it).

   Tip. Use this with int-sub to profile makefile reading
        and similar. */
static char *
func_nanots (char *o, char **argv UNUSED, const char *funcname UNUSED)
{
  return math_int_to_variable_buffer (o, nano_timestamp ());
}
#endif

#ifdef CONFIG_WITH_OS2_LIBPATH
/* Sets or gets the OS/2 libpath variables.

   The first argument indicates which variable - BEGINLIBPATH,
   ENDLIBPATH, LIBPATHSTRICT or LIBPATH.

   The second indicates whether this is a get (not present) or
   set (present) operation. When present it is the new value for
   the variable. */
static char *
func_os2_libpath (char *o, char **argv, const char *funcname UNUSED)
{
  char buf[4096];
  ULONG fVar;
  APIRET rc;

  /* translate variable name (first arg) */
  if (!strcmp (argv[0], "BEGINLIBPATH"))
    fVar = BEGIN_LIBPATH;
  else if (!strcmp (argv[0], "ENDLIBPATH"))
    fVar = END_LIBPATH;
  else if (!strcmp (argv[0], "LIBPATHSTRICT"))
    fVar = LIBPATHSTRICT;
  else if (!strcmp (argv[0], "LIBPATH"))
    fVar = 0;
  else
    {
      OS (error, NILF, _("$(libpath): unknown variable `%s'"), argv[0]);
      return variable_buffer_output (o, "", 0);
    }

  if (!argv[1])
    {
      /* get the variable value. */
      if (fVar != 0)
        {
          buf[0] = buf[1] = buf[2] = buf[3] = '\0';
          rc = DosQueryExtLIBPATH (buf, fVar);
        }
      else
        rc = DosQueryHeaderInfo (NULLHANDLE, 0, buf, sizeof(buf), QHINF_LIBPATH);
      if (rc != NO_ERROR)
        {
          OSN (error, NILF, _("$(libpath): failed to query `%s', rc=%d"), argv[0], rc);
          return variable_buffer_output (o, "", 0);
        }
      o = variable_buffer_output (o, buf, strlen (buf));
    }
  else
    {
      /* set the variable value. */
      size_t len;
      size_t len_max = sizeof (buf) < 2048 ? sizeof (buf) : 2048;
      const char *val;
      const char *end;

      if (fVar == 0)
        {
          O (error, NILF, _("$(libpath): LIBPATH is read-only"));
          return variable_buffer_output (o, "", 0);
        }

      /* strip leading and trailing spaces and check for max length. */
      val = argv[1];
      while (ISSPACE (*val))
        val++;
      end = strchr (val, '\0');
      while (end > val && ISSPACE (end[-1]))
        end--;

      len = end - val;
      if (len >= len_max)
        {
          OSNN (error,  NILF, _("$(libpath): The new `%s' value is too long (%d bytes, max %d)"),
                argv[0], len, len_max);
          return variable_buffer_output (o, "", 0);
        }

      /* make a stripped copy in low memory and try set it. */
      memcpy (buf, val, len);
      buf[len] = '\0';
      rc = DosSetExtLIBPATH (buf, fVar);
      if (rc != NO_ERROR)
        {
          OSSN (error, NILF, _("$(libpath): failed to set `%s' to `%s', rc=%d"), argv[0], buf, rc);
          return variable_buffer_output (o, "", 0);
        }

      o = variable_buffer_output (o, "", 0);
    }
  return o;
}
#endif  /* CONFIG_WITH_OS2_LIBPATH */

#if defined (CONFIG_WITH_MAKE_STATS) || defined (CONFIG_WITH_MINIMAL_STATS)
/* Retrieve make statistics. */
static char *
func_make_stats (char *o, char **argv, const char *funcname UNUSED)
{
  char buf[512];
  int len;

  if (!argv[0] || (!argv[0][0] && !argv[1]))
    {
# ifdef CONFIG_WITH_MAKE_STATS
      len = sprintf (buf, "alloc-cur: %5ld/%3ld %3luMB  hash: %5lu %2lu%%",
                     make_stats_allocations,
                     make_stats_reallocations,
                     make_stats_allocated / (1024*1024),
                     make_stats_ht_lookups,
                     (make_stats_ht_collisions * 100) / make_stats_ht_lookups);
      o = variable_buffer_output (o, buf, len);
#endif
    }
  else
    {
      /* selective */
      int i;
      for (i = 0; argv[i]; i++)
        {
          unsigned long val;
          if (i != 0)
            o = variable_buffer_output (o, " ", 1);
          if (0)
              continue;
# ifdef CONFIG_WITH_MAKE_STATS
          else if (!strcmp(argv[i], "allocations"))
            val = make_stats_allocations;
          else if (!strcmp(argv[i], "reallocations"))
            val = make_stats_reallocations;
          else if (!strcmp(argv[i], "allocated"))
            val = make_stats_allocated;
          else if (!strcmp(argv[i], "ht_lookups"))
            val = make_stats_ht_lookups;
          else if (!strcmp(argv[i], "ht_collisions"))
            val = make_stats_ht_collisions;
          else if (!strcmp(argv[i], "ht_collisions_pct"))
            val = (make_stats_ht_collisions * 100) / make_stats_ht_lookups;
#endif
          else
            {
              o = variable_buffer_output (o, argv[i], strlen (argv[i]));
              continue;
            }

          len = sprintf (buf, "%ld", val);
          o = variable_buffer_output (o, buf, len);
        }
    }

  return o;
}
#endif  /* CONFIG_WITH_MAKE_STATS */

#ifdef CONFIG_WITH_COMMANDS_FUNC
/* Gets all the commands for a target, separated by newlines.

   This is useful when creating and checking target dependencies since
   it reduces the amount of work and the memory consuption. A new prefix
   character '%' has been introduced for skipping certain lines, like
   for instance the one calling this function and pushing to a dep file.
   Blank lines are also skipped.

   The commands function takes exactly one argument, which is the name of
   the target which commands should be returned.

   The commands-sc is identical to commands except that it uses a ';' to
   separate the commands.

   The commands-usr is similar to commands except that it takes a 2nd
   argument that is used to separate the commands. */
char *
func_commands (char *o, char **argv, const char *funcname)
{
  struct file *file;
  static int recursive = 0;

  if (recursive)
    {
      OS (error, reading_file, _("$(%s ) was invoked recursivly"), funcname);
      return variable_buffer_output (o, "recursive", sizeof ("recursive") - 1);
    }
  if (*argv[0] == '\0')
    {
      OS (error, reading_file, _("$(%s ) was invoked with an empty target name"), funcname);
      return o;
    }
  recursive = 1;

  file = lookup_file (argv[0]);
  if (file && file->cmds)
    {
      unsigned int i;
      int cmd_sep_len;
      struct commands *cmds = file->cmds;
      const char *cmd_sep;

      if (!strcmp (funcname, "commands"))
        {
          cmd_sep = "\n";
          cmd_sep_len = 1;
        }
      else if (!strcmp (funcname, "commands-sc"))
        {
          cmd_sep = ";";
          cmd_sep_len = 1;
        }
      else /*if (!strcmp (funcname, "commands-usr"))*/
        {
          cmd_sep = argv[1];
          cmd_sep_len = strlen (cmd_sep);
        }

      initialize_file_variables (file, 1 /* don't search for pattern vars */);
      set_file_variables (file, 1 /* early call */);
      chop_commands (cmds);

      for (i = 0; i < cmds->ncommand_lines; i++)
        {
          char *p;
          char *in, *out, *ref;

          /* Skip it if it has a '%' prefix or is blank. */
          if (cmds->lines_flags[i] & COMMAND_GETTER_SKIP_IT)
            continue;
          p = cmds->command_lines[i];
          while (ISBLANK (*p))
            p++;
          if (*p == '\0')
            continue;

          /* --- copied from new_job() in job.c --- */

          /* Collapse backslash-newline combinations that are inside variable
             or function references.  These are left alone by the parser so
             that they will appear in the echoing of commands (where they look
             nice); and collapsed by construct_command_argv when it tokenizes.
             But letting them survive inside function invocations loses because
             we don't want the functions to see them as part of the text.  */

          /* IN points to where in the line we are scanning.
             OUT points to where in the line we are writing.
             When we collapse a backslash-newline combination,
             IN gets ahead of OUT.  */

          in = out = p;
          while ((ref = strchr (in, '$')) != 0)
            {
              ++ref;		/* Move past the $.  */

              if (out != in)
                /* Copy the text between the end of the last chunk
                   we processed (where IN points) and the new chunk
                   we are about to process (where REF points).  */
                memmove (out, in, ref - in);

              /* Move both pointers past the boring stuff.  */
              out += ref - in;
              in = ref;

              if (*ref == '(' || *ref == '{')
                {
                  char openparen = *ref;
                  char closeparen = openparen == '(' ? ')' : '}';
                  int count;
                  char *p2;

                  *out++ = *in++;	/* Copy OPENPAREN.  */
                  /* IN now points past the opening paren or brace.
                     Count parens or braces until it is matched.  */
                  count = 0;
                  while (*in != '\0')
                    {
                      if (*in == closeparen && --count < 0)
                        break;
                      else if (*in == '\\' && in[1] == '\n')
                        {
                          /* We have found a backslash-newline inside a
                             variable or function reference.  Eat it and
                             any following whitespace.  */

                          int quoted = 0;
                          for (p2 = in - 1; p2 > ref && *p2 == '\\'; --p2)
                            quoted = !quoted;

                          if (quoted)
                            /* There were two or more backslashes, so this is
                               not really a continuation line.  We don't collapse
                               the quoting backslashes here as is done in
                               collapse_continuations, because the line will
                               be collapsed again after expansion.  */
                            *out++ = *in++;
                          else
                            {
                              /* Skip the backslash, newline and
                                 any following whitespace.  */
                              in = next_token (in + 2);

                              /* Discard any preceding whitespace that has
                                 already been written to the output.  */
                              while (out > ref
                                     && ISBLANK (out[-1]))
                                --out;

                              /* Replace it all with a single space.  */
                              *out++ = ' ';
                            }
                        }
                      else
                        {
                          if (*in == openparen)
                            ++count;

                          *out++ = *in++;
                        }
                    }
                }
              /* Some of these can be amended ($< perhaps), but we're likely to be called while the
                 dep expansion happens, so it would have to be on a hackish basis. sad... */
              else if (*ref == '<' || *ref == '*' || *ref == '%' || *ref == '^' || *ref == '+')
                OSN (error, reading_file, _("$(%s ) does not work reliably with $%c in all cases"), funcname, *ref);
            }

          /* There are no more references in this line to worry about.
             Copy the remaining uninteresting text to the output.  */
          if (out != in)
            strcpy (out, in);

          /* --- copied from new_job() in job.c --- */

          /* Finally, expand the line.  */
          if (i)
            o = variable_buffer_output (o, cmd_sep, cmd_sep_len);
          o = variable_expand_for_file_2 (o, cmds->command_lines[i], ~0U, file, NULL);

          /* Skip it if it has a '%' prefix or is blank. */
          p = o;
          while (ISBLANK (*o)
              || *o == '@'
              || *o == '-'
              || *o == '+')
            o++;
          if (*o != '\0' && *o != '%')
            o = strchr (o, '\0');
          else if (i)
            o = p - cmd_sep_len;
          else
            o = p;
        } /* for each command line */
    }
  /* else FIXME: bitch about it? */

  recursive = 0;
  return o;
}
#endif  /* CONFIG_WITH_COMMANDS_FUNC */
#ifdef KMK

/* Useful when debugging kmk and/or makefiles. */
char *
func_breakpoint (char *o, char **argv UNUSED, const char *funcname UNUSED)
{
#ifdef _MSC_VER
  __debugbreak();
#elif defined(__i386__) || defined(__x86__) || defined(__X86__) || defined(_M_IX86) || defined(__i386) \
   || defined(__amd64__) || defined(__x86_64__) || defined(__AMD64__) || defined(_M_X64) || defined(__amd64)
# ifdef __sun__
  __asm__ __volatile__ ("int $3\n\t");
# else
  __asm__ __volatile__ ("int3\n\t");
# endif
#else
  char *p = (char *)0;
  *p = '\0';
#endif
  return o;
}

/* umask | umask -S. */
char *
func_get_umask (char *o, char **argv UNUSED, const char *funcname UNUSED)
{
  char sz[80];
  int off;
  mode_t u;
  int symbolic = 0;
  const char *psz = argv[0];

  if (psz)
    {
      const char *pszEnd = strchr (psz, '\0');
      strip_whitespace (&psz, &pszEnd);

      if (pszEnd != psz)
        {
          if (   STR_N_EQUALS (psz, pszEnd - pszEnd, "S")
              || STR_N_EQUALS (psz, pszEnd - pszEnd, "-S")
              || STR_N_EQUALS (psz, pszEnd - pszEnd, "symbolic") )
            symbolic = 1;
          else
            OSS (error, reading_file, _("$(%s ) invalid argument `%s'"),
                 funcname, argv[0]);
        }
    }

  u = g_fUMask;
  assert (u == umask (g_fUMask));

  if (symbolic)
    {
      off = 0;
      sz[off++] = 'u';
      sz[off++] = '=';
      if ((u & S_IRUSR) == 0)
        sz[off++] = 'r';
      if ((u & S_IWUSR) == 0)
        sz[off++] = 'w';
      if ((u & S_IXUSR) == 0)
        sz[off++] = 'x';
      sz[off++] = ',';
      sz[off++] = 'g';
      sz[off++] = '=';
      if ((u & S_IRGRP) == 0)
        sz[off++] = 'r';
      if ((u & S_IWGRP) == 0)
        sz[off++] = 'w';
      if ((u & S_IXGRP) == 0)
        sz[off++] = 'x';
      sz[off++] = ',';
      sz[off++] = 'o';
      sz[off++] = '=';
      if ((u & S_IROTH) == 0)
        sz[off++] = 'r';
      if ((u & S_IWOTH) == 0)
        sz[off++] = 'w';
      if ((u & S_IXOTH) == 0)
        sz[off++] = 'x';
    }
  else
    off = sprintf (sz, "%.4o", u);

  return variable_buffer_output (o, sz, off);
}


/* umask 0002 | umask u=rwx,g=rwx,o=rx. */
char *
func_set_umask (char *o, char **argv UNUSED, const char *funcname UNUSED)
{
  mode_t u;
  const char *psz;

  /* Figure what kind of input this is. */
  psz = argv[0];
  while (ISBLANK (*psz))
    psz++;

  if (isdigit ((unsigned char)*psz))
   {
      u = 0;
      while (*psz)
        {
          u <<= 3;
          if (*psz < '0' || *psz >= '8')
            {
              OSS (error, reading_file, _("$(%s ) illegal number `%s'"), funcname, argv[0]);
              break;
            }
          u += *psz - '0';
          psz++;
        }

      if (argv[1] != NULL)
          OS (error, reading_file, _("$(%s ) too many arguments for octal mode"), funcname);

      umask (u);
      g_fUMask = u;
  }
  else
  {
      OS (error, reading_file, _("$(%s ) symbol mode is not implemented"), funcname);
  }

  return o;
}


/* Controls the cache in dir-bird-nt.c. */

char *
func_dircache_ctl (char *o, char **argv UNUSED, const char *funcname UNUSED)
{
# ifdef KBUILD_OS_WINDOWS
  const char *cmd = argv[0];
  while (ISBLANK (*cmd))
    cmd++;
  if (strcmp (cmd, "invalidate") == 0)
    {
      if (argv[1] != NULL)
        O (error, reading_file, "$(dircache-ctl invalidate) takes no parameters");
      dir_cache_invalid_all ();
    }
  else if (strcmp (cmd, "invalidate-and-close-dirs") == 0)
    {
      if (argv[1] != NULL)
        O (error, reading_file, "$(dircache-ctl invalidate) takes no parameters");
      dir_cache_invalid_all_and_close_dirs (0 /*including_root*/);
    }
  else if (strcmp (cmd, "invalidate-missing") == 0)
    {
      if (argv[1] != NULL)
        O (error, reading_file, "$(dircache-ctl invalidate-missing) takes no parameters");
      dir_cache_invalid_missing ();
    }
  else if (strcmp (cmd, "volatile") == 0)
    {
      size_t i;
      for (i = 1; argv[i] != NULL; i++)
        {
          const char *dir = argv[i];
          while (ISBLANK (*dir))
            dir++;
          if (*dir)
            dir_cache_volatile_dir (dir);
        }
    }
  else if (strcmp (cmd, "deleted") == 0)
    {
      size_t i;
      for (i = 1; argv[i] != NULL; i++)
        {
          const char *dir = argv[i];
          while (ISBLANK (*dir))
            dir++;
          if (*dir)
            dir_cache_deleted_directory (dir);
        }
    }
  else
    OS (error, reading_file, "Unknown $(dircache-ctl ) command: '%s'", cmd);
# endif
  return o;
}

/* Helper for performer GNU make style quoting of one filename. */

static char *
helper_quote_make (char *o, const char *name, size_t len, int is_dep,
                   int is_tgt, int quote_trailing_slashes,
                   const char *funcname)
{
  unsigned const map_flags = MAP_BLANK
                           | MAP_NEWLINE
                           | MAP_COMMENT
                           | MAP_VARIABLE
                           | MAP_SEMI
                           | MAP_EQUALS
                           | (is_dep ? MAP_PIPE :
                              is_tgt ? MAP_COLON : 0);
  const char *cur = name;
  assert (memchr (name, '\0', len) == NULL);
  if (len > 0)
    {
      const char * const end = &name[len];
      unsigned long len_out = 0;
      const char *prev = cur;
      do
        {
          char ch = *cur;
          unsigned int flags = stopchar_map[(unsigned int)ch] & map_flags;
          if (!flags)
            cur++; /* likely */
          else
            {
              /* Flush pending output. */
              if (prev != cur)
                {
                  o = variable_buffer_output (o, prev, cur - prev);
                  len_out += cur  - prev;
                }

              /* Dollar is quoted by duplicating the dollar: */
              if (flags & MAP_VARIABLE)
                {
                  o = variable_buffer_output (o, "$", 1);
                  prev = cur++;
                }
              /* The rest is quoted by '\': */
              else
                {
                  size_t const max_slashes = cur - prev;
                  size_t slashes = 0;
                  while (slashes < max_slashes && cur[1 - slashes] == '\\')
                    slashes++;
                  if (slashes)
                    {
                      o  = variable_buffer_output (o, &cur[0 - slashes], slashes);
                      len_out += slashes;
                    }
                  o  = variable_buffer_output (o, "\\", 1);
                  prev = cur++;
                }
            }
        }
      while ((uintptr_t)cur < (uintptr_t)end);

      /* Flush pending output. */
      if (prev != cur)
        {
          o = variable_buffer_output (o, prev, cur - prev);
          len_out += cur  - prev;
        }

      /* Escape trailing slashes when needed. */
      if (   o[-1] == '\\'
          && quote_trailing_slashes)
        {
          size_t slashes = 1;
          while (slashes < len_out && o[-1 - slashes] == '\\')
            slashes++;
          while (slashes-- > 0)
            o  = variable_buffer_output (o, "\\", 1);
        }
    }
  else
      OS (message, 0, "%s: cannot quote empty string", funcname);
  return o;
}

/* Helper for func_quote_make that checks if there are more arguments
   that produces output or not. */

static int func_quote_make_has_more_non_empty_args (char **argv)
{
  for (;;)
    {
      char *arg = *argv;
      if (!arg)
        return 0;
      if (*arg)
        return 1;
      argv++;
    }
}

/* Takes zero or more plain strings and escapes (quotes) spaces and other
   problematic characters, GNU make style.

   There is one slightly problematic aspect of using this, if the input ends
   with backslashes whether or not they will be reduced or taken as-is depends
   on whether they appear at the end of a line or not.  They are taken as-is
   when at the end of a line, otherwise they'll be subject to unescaping
   (unquoting) and reduced by half.

   In addition, the quoting style differs for files on the left side and
   right side of the recipe colon.  Colons aren't escaped are only escaped
   on the left side (target), and the pipe character is only escaped on the
   right side (deps).

   For this reason there are four variants of this function. */

static char *func_quote_make (char *o, char **argv, const char *funcname)
{
  int const is_dep = funcname[5] == '-' && funcname[6] == 'd';
  int const is_tgt = funcname[5] == '-' && funcname[6] == 't';
  int const quote_trailing_slashes = funcname[5] == '\0' || funcname[9] == '\0';
  char * const o_initial = o;
  int i;

  assert (    quote_trailing_slashes
          == (!strcmp (funcname, "quote") || !strcmp (funcname, "quote-dep") || !strcmp (funcname, "quote-tgt")));
  assert (is_dep == !strncmp (funcname, "quote-dep", sizeof ("quote-dep") - 1));
  assert (is_tgt == !strncmp (funcname, "quote-tgt", sizeof ("quote-tgt") - 1));

  for (i = 0; argv[i]; i++)
    {
      char *arg = argv[i];
      if (*arg)
        {
          /* Add space separator. */
          if (o != o_initial)
            o = variable_buffer_output (o, " ", 1);

          /* Output the quoted argument: */
          if (quote_trailing_slashes)
            o = helper_quote_make (o, arg, strlen (arg), is_dep, is_tgt,
                                   quote_trailing_slashes, funcname);
          else
            {
              char *end = strchr (arg, '\0');
              int qts = end != arg && end[-1] == '\\'
                     && func_quote_make_has_more_non_empty_args (&argv[i + 1]);
              o = helper_quote_make (o, arg, strlen (arg), is_dep, is_tgt,
                                     qts, funcname);
            }
        }
      else
          OS (message, 0, "%s: cannot munge empty string", funcname);
    }

  return o;
}

/* Worker for func_quote_shell() for escaping a string that's inside
   double quotes. */

static char *func_escape_shell_in_dq (char *o, const char *arg, size_t len)
{
  const char *prev = arg;
  while (len-- > 0)
    {
      char const ch = *arg;
      switch (ch)
        {
          default:
            arg++;
            break;
          case '!':
          case '$':
          case '`':
          case '"':
          case '\\':
          case '\n':
            if (prev != arg)
              o = variable_buffer_output (o, prev, arg - prev);
            o = variable_buffer_output (o, "\\", 1);
            prev = arg;
            arg++;
            break;
        }
    }
  if (prev != arg)
    o = variable_buffer_output (o, prev, arg - prev);
  return o;
}

/* quote-sh-dq */

static char *func_quote_shell_dq (char *o, char **argv, const char *funcname UNUSED)
{
  return func_escape_shell_in_dq (o, argv[0], strlen (argv[0]));
}


/* Worker for func_quote_shell() for escaping a string that's inside
   single quotes. */

static char *func_escape_shell_in_sq (char *o, const char *arg, size_t len)
{
  while (len > 0)
    {
      char *sq = memchr (arg, '\'', len);
      if (!sq)
          return variable_buffer_output (o, arg, len);
      if (sq != arg)
        o = variable_buffer_output (o, arg, sq - arg);
      o = variable_buffer_output (o, "'\\''", 4);

      /* advance */
      sq++;
      len -= sq - arg;
      arg = sq;
    }
  return o;
}

/* quote-sh-dq */

static char *func_quote_shell_sq (char *o, char **argv, const char *funcname UNUSED)
{
  return func_escape_shell_in_sq (o, argv[0], strlen (argv[0]));
}

/* Output a shell argument with quoting as needed. */
static char *helper_quote_shell (char *o, const char *arg, size_t len,
                                 int leading_space)
{
  if (   memchr (arg, '$', len) != NULL
      || memchr (arg, '*', len) != NULL
      || memchr (arg, '?', len) != NULL
      || memchr (arg, '[', len) != NULL)
    {
      if (leading_space)
        o = variable_buffer_output (o, " '", 2);
      else
        o = variable_buffer_output (o, "'", 1);
      o = func_escape_shell_in_sq (o, arg, len);
      o = variable_buffer_output (o, "'", 1);
    }
  else if (   memchr (arg, ' ',  len) != NULL
           || memchr (arg, '\t', len) != NULL
           || memchr (arg, '\\', len) != NULL
           || memchr (arg, '"',  len) != NULL
           || memchr (arg, '`',  len) != NULL
           || memchr (arg, '!',  len) != NULL
           || memchr (arg, '|',  len) != NULL
           || memchr (arg, '<',  len) != NULL
           || memchr (arg, '>',  len) != NULL
           || memchr (arg, '&',  len) != NULL
           || memchr (arg, ';',  len) != NULL
           || memchr (arg, '(',  len) != NULL
           || memchr (arg, ')',  len) != NULL
           || memchr (arg, '\n', len) != NULL)
    {
      if (leading_space)
        o = variable_buffer_output (o, " \"", 2);
      else
        o = variable_buffer_output (o, "\"", 1);
      o = func_escape_shell_in_dq (o, arg, len);
      o = variable_buffer_output (o, "\"", 1);
    }
  else
    {
      if (leading_space)
        o = variable_buffer_output (o, " ", 1);
      o = variable_buffer_output (o, arg, len);
    }
  return o;
}

/* Takes zero or more plain strings and escapes/quotes spaces and other
   problematic characters, bourne make style.

   The quote-sh-dq and quote-sh-sq variants is for escaping strings that
   going to be put into double quotes and single quotes respectively.

   The normal quote-sh variant assumes it's free to do open and close
   quotes as it pleases. */

static char *func_quote_shell (char *o, char **argv, const char *funcname UNUSED)
{
  int i;
  for (i = 0; argv[i]; i++)
    o = helper_quote_shell (o, argv[i], strlen (argv[i]),
                            i > 0 /*need_leading_space*/);
  return o;
}

/* Unlinks CUR from *CHAINP and frees it, returning the next element. */

static struct nameseq *
helper_unlink_and_free_ns (struct nameseq *cur, struct nameseq *prev,
                           struct nameseq **chainp)
{
  struct nameseq *freeit = cur; \
    free ((char *)cur->name);
  if (prev)
    prev->next = cur = cur->next;
  else
    *chainp = cur = cur->next;
  free_ns (freeit);
  return cur;
}

/* Frees a chain returned by helper_parse_file_list. */

static void free_ns_chain_no_strcache (struct nameseq *ns)
{
  while (ns != 0)
    {
      struct nameseq *t = ns;
      free ((char *)ns->name);
      ns = ns->next;
      free_ns (t);
    }
}

/* Decoded style options for the $(q* ) and $(*file* ) functions. */
#define Q_RET_MASK              0x000f
#define Q_RET_QUOTED            0x0000
#define Q_RET_QUOTED_DEP        0x0001
#define Q_RET_QUOTED_DEP_END    0x0002
#define Q_RET_QUOTED_TGT        0x0003
#define Q_RET_QUOTED_TGT_END    0x0004
#define Q_RET_UNQUOTED          0x0005
#define Q_RET_SHELL             0x0006
#define Q_RET_SHELL_IN_DQ       0x0007
#define Q_RET_SHELL_IN_SQ       0x0008

#define Q_IN_MASK               0x0070
#define Q_IN_QUOTED             0x0000
#define Q_IN_UNQUOTED           0x0010
#define Q_IN_QUOTED_DEP         0x0020  /** @todo needed? */
#define Q_IN_QUOTED_TGT         0x0030  /** @todo needed? */
#define Q_IN_UNQUOTED_SINGLE    0x0040
#define Q_IN_SEP_COMMA          0x0080  /* for VMS hacks, file lists only */

#define Q_SEP_MASK              0x0700
#define Q_SEP_SHIFT             8
#define Q_SEP_SPACE             0x0000
#define Q_SEP_TAB               0x0100
#define Q_SEP_NL                0x0200
#define Q_SEP_NL_TAB            0x0300
#define Q_SEP_COMMA             0x0400  /* for VMS, output only */

#define Q_QDEFAULT              (Q_IN_QUOTED | Q_RET_QUOTED | Q_SEP_SPACE)
#ifndef VMS
# define Q_QDEFAULT_VMS_TRICKS  Q_QDEFAULT
#else /* VMS: Treat ',' as file separators in input, maybe output too. */
# define Q_QDEFAULT_VMS_TRICKS  (Q_IN_SEP_COMMA | \
    (!vms_comma_separator ? Q_QDEFAULT \
     : (Q_QDEFAULT & ~Q_SEP_MASK) | Q_SEP_COMMA)
#endif


/* Decodes the optional style argument.  This is chiefly for the return
   style, but can also pick the input and space styles (just because we can).  */

static unsigned int helper_file_quoting_style (char *style, unsigned int intstyle)
{
  if (style != NULL)
    {
      for (;;)
        {
          /* Skip blanks: */
          while (ISBLANK (*style))
            style++;
          if (*style != '\0')
            {
              /* Find the end of the current word: */
              char * const start = style;
              size_t len;
              char ch;
              while (!ISBLANK ((ch = *style)) && ch != '\0')
                style++;
              len = style - start;

              /* String "switch" to decode the word: */

#define MATCH(a_str)  (len == sizeof (a_str) - 1 && memcmp (start, a_str, sizeof (a_str)) == 0)
              /* return styles: */
              if (MATCH ("quoted")              || MATCH ("q"))
                intstyle = (intstyle & ~Q_RET_MASK) | Q_RET_QUOTED;
              else if (MATCH ("unquoted")       || MATCH ("unq")        || MATCH ("u"))
                intstyle = (intstyle & ~Q_RET_MASK) | Q_RET_UNQUOTED;
              else if (MATCH ("quoted-dep")     || MATCH ("q-dep")      || MATCH ("q-d"))
                intstyle = (intstyle & ~Q_RET_MASK) | Q_RET_QUOTED_DEP;
              else if (MATCH ("quoted-dep-end") || MATCH ("q-dep-end")  || MATCH ("q-d-e"))
                intstyle = (intstyle & ~Q_RET_MASK) | Q_RET_QUOTED_DEP_END;
              else if (MATCH ("quoted-tgt")     || MATCH ("q-tgt")      || MATCH ("q-t"))
                intstyle = (intstyle & ~Q_RET_MASK) | Q_RET_QUOTED_TGT;
              else if (MATCH ("quoted-tgt-end") || MATCH ("q-tgt-end")  || MATCH ("q-t-e"))
                intstyle = (intstyle & ~Q_RET_MASK) | Q_RET_QUOTED_TGT_END;
              else if (MATCH ("shell")          || MATCH ("sh"))
                intstyle = (intstyle & ~Q_RET_MASK) | Q_RET_SHELL;
              else if (MATCH ("shell-in-dq")    || MATCH ("sh-i-d"))
                intstyle = (intstyle & ~Q_RET_MASK) | Q_RET_SHELL_IN_DQ; /* returned string is used inside double shell quotes */
              else if (MATCH ("shell-in-sq")    || MATCH ("sh-i-s"))
                intstyle = (intstyle & ~Q_RET_MASK) | Q_RET_SHELL_IN_SQ; /* returned string is used inside single shell quotes */
              /* input styles: */
              else if (MATCH ("in-quoted")         || MATCH ("i-q"))
                intstyle = (intstyle & ~Q_IN_MASK) | Q_IN_QUOTED;
              else if (MATCH ("in-unquoted")       || MATCH ("i-unq")   || MATCH ("i-u"))
                intstyle = (intstyle & ~Q_IN_MASK) | Q_IN_UNQUOTED;
              else if (MATCH ("in-unquoted-single")|| MATCH ("i-unq-s") || MATCH ("i-u-s")
                    || MATCH ("in-unquoted-file")  || MATCH ("i-unq-f") || MATCH ("i-u-f"))
                intstyle = (intstyle & ~Q_IN_MASK) | Q_IN_UNQUOTED_SINGLE;
              else if (MATCH ("in-quoted-dep")     || MATCH ("i-q-dep") || MATCH ("i-q-d"))
                intstyle = (intstyle & ~Q_IN_MASK) | Q_IN_QUOTED_DEP;
              else if (MATCH ("in-quoted-tgt")     || MATCH ("i-q-tgt") || MATCH ("i-q-t"))
                intstyle = (intstyle & ~Q_IN_MASK) | Q_IN_QUOTED_TGT;
              else if (MATCH ("in-sep-comma")      || MATCH ("i-s-com") || MATCH ("i-s-c"))
                intstyle |= Q_IN_SEP_COMMA; /*?*/
              /* separator styles (output): */
              else if (MATCH ("sep-space")  || MATCH ("s-space")  || MATCH ("s-s"))
                intstyle = (intstyle & ~Q_SEP_MASK) | Q_SEP_SPACE;
              else if (MATCH ("sep-tab")    || MATCH ("s-tab")    || MATCH ("s-t"))
                intstyle = (intstyle & ~Q_SEP_MASK) | Q_SEP_TAB;
              else if (MATCH ("sep-nl")     || MATCH ("s-nl")     || MATCH ("s-n"))
                intstyle = (intstyle & ~Q_SEP_MASK) | Q_SEP_NL;
              else if (MATCH ("sep-nl-tab") || MATCH ("s-nl-tab") || MATCH ("s-n-t"))
                intstyle = (intstyle & ~Q_SEP_MASK) | Q_SEP_NL_TAB;
              else if (MATCH ("sep-comma")  || MATCH ("s-comma")  || MATCH ("s-c"))
                intstyle = (intstyle & ~Q_SEP_MASK) | Q_SEP_COMMA;
              else
                {
                  char savedch = *style;
                  *style = '\0';
                  OS (error, reading_file, "Unknown file style: %s", start);
                  *style = savedch;
                }
#undef MATCH
            }
          else
            break;
        }
    }
  return intstyle;
}

/* Output (returns) a separator according to STYLE. */

static char *helper_return_sep (char *o, unsigned int style)
{
  /* Note! Must match Q_SEP_MASK! */
  static struct
  {
    const char *sep;
    size_t len;
  } const seps[8] =
  {
    { " ",    1 },
    { "\t",   1 },
    { "\n",   1 },
    { "\n\t", 2 },
    { ",",    1 },
    { " ",    1 },
    { " ",    1 },
    { " ",    1 }
  };

  return variable_buffer_output(o,
                                seps[(style & Q_SEP_MASK) >> Q_SEP_SHIFT].sep,
                                seps[(style & Q_SEP_MASK) >> Q_SEP_SHIFT].len);
}

/* Removes one separator from the output.

This is typically used when outputting lists where there is no simple way of
telling whether an entry is the last one or not.  So, is_last is always false
and the last separator is removed afterwards by this function. */

MY_INLINE char *helper_drop_separator (char *o, unsigned int style)
{
  o -= (style & Q_SEP_MASK) != Q_SEP_NL_TAB ? 2 : 1;
  *o = '\0'; /* not strictly necessary */
  return o;
}


/* Outputs (returns) the given file. */

static char *helper_return_file_len (char *o, const char *file, size_t len,
                                     unsigned int style, int is_last)
{
  assert (memchr (file, '\0', len) == NULL);
  switch (style & Q_RET_MASK)
    {
      case Q_RET_UNQUOTED:
        o = variable_buffer_output (o, file, len);
        break;
      case Q_RET_QUOTED:
        o = helper_quote_make (o, file, len, 0 /*is_dep*/, 0 /*is_tgt*/,
                               !is_last /*quote_trailing_slashes*/, NULL);
        break;
      case Q_RET_QUOTED_DEP:
        o = helper_quote_make (o, file, len, 1 /*is_dep*/, 0 /*is_tgt*/,
                               !is_last /*quote_trailing_slashes*/, NULL);
        break;
      case Q_RET_QUOTED_DEP_END:
        o = helper_quote_make (o, file, len, 1 /*is_dep*/, 0 /*is_tgt*/,
                               0 /*quote_trailing_slashes*/, NULL);
        break;
      case Q_RET_QUOTED_TGT:
        o = helper_quote_make (o, file, len, 0 /*is_dep*/, 1 /*is_tgt*/,
                               !is_last /*quote_trailing_slashes*/, NULL);
        break;
      case Q_RET_QUOTED_TGT_END:
        o = helper_quote_make (o, file, len, 0 /*is_dep*/, 1 /*is_tgt*/,
                               0 /*quote_trailing_slashes*/, NULL);
        break;
      case Q_RET_SHELL:
        o = helper_quote_shell (o, file, len, 0 /*need_leading_space*/);
        break;
      case Q_RET_SHELL_IN_DQ:
        o = func_escape_shell_in_dq (o, file, len);
        break;
      case Q_RET_SHELL_IN_SQ:
        o = func_escape_shell_in_sq (o, file, len);
        break;
      default:
        assert (0);
    }

  /* Add separator space if not last. */
  if (!is_last)
    o = helper_return_sep (o, style);
  return o;
}

/* Outputs (returns) the given file. */

static char *helper_return_file (char *o, const char *file,
                                 unsigned int style, int is_last)
{
  return helper_return_file_len (o,file, strlen (file), style, is_last);
}

/* Outputs (returns) the given CHAIN and frees it. */

static char *helper_return_and_free_chain (char *o, struct nameseq *chain, unsigned int style)
{
  struct nameseq *cur;
  for (cur = chain; cur; cur = cur->next)
    o = helper_return_file (o, cur->name, style, cur->next == NULL);
  free_ns_chain_no_strcache (chain);
  return o;
}


/* Helper for helper_parse_file_list that globs a name sequence. */
static struct nameseq *
helper_glob_chain (struct nameseq *chain)
{
  struct nameseq *prev = NULL;
  struct nameseq *cur = chain;
  glob_t gl;
  dir_setup_glob (&gl);

  /** @todo XXX: !NO_ARCHIVES  */
  while (cur)
    {
      switch (glob (cur->name, GLOB_NOSORT | GLOB_ALTDIRFUNC, NULL, &gl))
        {
        case 0: /* Replace CUR with the names found. */
          {
            struct nameseq *subchain = NULL;
            struct nameseq **ppnext = &subchain;
            const char ** const names = (const char **)gl.gl_pathv;
            size_t const num_names = gl.gl_pathc;
            size_t idx;

            cur = helper_unlink_and_free_ns (cur, prev, &chain);

            for (idx = 0; idx < num_names; idx++)
              {
#ifndef CONFIG_WITH_ALLOC_CACHES
                struct nameseq *newp = xcalloc (sizeof (*newp));
#else
                struct nameseq *newp = alloccache_calloc (&nameseq_cache);
#endif
                newp->name = xstrdup (names[idx]);
                newp->next = NULL;
                *ppnext = newp;
                ppnext = &newp->next;
              }

            if (subchain) /* parnaoia */
              {
                *ppnext = cur;
                if (prev)
                  prev->next = subchain;
                else
                  chain = subchain;
              }
            break;
          }

        case GLOB_NOMATCH: /* doesn't exist, remove */
          cur = helper_unlink_and_free_ns (cur, prev, &chain);
          break;

        default: /* Keep it. */
          prev = cur;
          cur = cur->next;
          break;

        case GLOB_NOSPACE:
          OUT_OF_MEM();
        }
      globfree (&gl);
    }
  return chain;
}

/* Parses a file/word list according to STYLE and returns a name list.

   The FILELIST parameter may be modified!

   The GLOB param is for wildcard/qwildcard.

   TODO/XXX: Unquote and split up the FILELIST directly.  All function
             arguments are heap copies already, so we are free to modify
             them.  Would be nice to ditch the nameseq in favor of something
             which also includes the length. */

static struct nameseq *
helper_parse_file_list (char *filelist, unsigned int style, int glob)
{
  if (filelist && *filelist != '\0')
    {
      /* Q_IN_SEP_COMMA: VMS tricks for qbasename, qdir, qnotdir and qsuffix
         where commas are treated as separtors in FILELIST.  We simply
         replace commas in the FILELIST before doing the regular parsing. */
      if (!(style & Q_IN_SEP_COMMA))
        { /* typical */ }
      else
        {
          size_t len = strlen (filelist);
          char *start = filelist;
          char *comma = (char *)memchr (filelist, ',', len);
          while (comma)
            {
              *comma = ' ';
              len -= comma - start - 1;
              if (len)
                {
                  start = comma + 1;
                  comma = (char *)memchr (start, ',', len);
                }
              else
                break;
            }
        }

      switch (style & Q_IN_MASK)
        {
          case Q_IN_QUOTED:
          case Q_IN_QUOTED_DEP: /** @todo ?? */
          case Q_IN_QUOTED_TGT: /** @todo ?? */
            return PARSE_FILE_SEQ(&filelist, struct nameseq, MAP_NUL, NULL,
                                  !glob
                                  ? PARSEFS_NOGLOB | PARSEFS_NOSTRIP | PARSEFS_NOCACHE
                                  : PARSEFS_NOSTRIP | PARSEFS_NOCACHE | PARSEFS_EXISTS);

          case Q_IN_UNQUOTED:
            {
              struct nameseq *chain = NULL;
              struct nameseq **ppnext = &chain;
              const char *it = filelist;
              const char *cur;
              unsigned int curlen;
              while ((cur = find_next_token (&it, &curlen)) != NULL)
                {
#ifndef CONFIG_WITH_ALLOC_CACHES
                  struct nameseq *newp = xcalloc (sizeof (*newp));
#else
                  struct nameseq *newp = alloccache_calloc (&nameseq_cache);
#endif
                  newp->name = xstrndup (cur, curlen);
                  newp->next = NULL;
                  *ppnext = newp;
                  ppnext = &newp->next;
                }
              if (!glob)
                return chain;
              return helper_glob_chain (chain);
            }

          case Q_IN_UNQUOTED_SINGLE:
            if (*filelist != '\0')
              {
#ifndef CONFIG_WITH_ALLOC_CACHES
                struct nameseq *newp = xcalloc (sizeof (*newp));
#else
                struct nameseq *newp = alloccache_calloc (&nameseq_cache);
#endif
                newp->name = xstrdup (filelist);
                newp->next = NULL;
                if (!glob)
                  return newp;
                return helper_glob_chain (newp);
              }
            return NULL;

          default:
            assert (0);
            return NULL;
        }
    }
  return NULL;
}

/* $(requote style, file1 file2 ... fileN). */

static char *func_requote (char *o, char **argv, const char *funcname UNUSED)
{
  unsigned int const style = helper_file_quoting_style (argv[0], Q_QDEFAULT);
  struct nameseq *chain = helper_parse_file_list (argv[1], style, 0);
  return helper_return_and_free_chain (o, chain, style);
}

/* Common worker for func_firstfile() and func_qfirstfile(). */

static char *common_firstfile (char *o, char **argv, unsigned int style)
{
  struct nameseq *chain = helper_parse_file_list (argv[0], style, 0);
  if (chain)
    {
      o = helper_return_file (o, chain->name, style, 1);
      free_ns_chain_no_strcache (chain);
    }
  return o;
}

/* $(firstfile file1 file2 ... fileN) - same as $(firstfile ), except
   for files rather than word tokens.  See func_firstword().  */

static char *func_firstfile (char *o, char **argv, const char *funcname UNUSED)
{
  return common_firstfile(o, argv, Q_IN_QUOTED | Q_RET_QUOTED | Q_SEP_SPACE);
}

/* $(qfirstfile style, file1 file2 ... fileN) - same as $(firstfile ), except
   for files rather than word tokens.  See func_firstword().  */

static char *func_q_firstfile (char *o, char **argv, const char *funcname UNUSED)
{
  unsigned int style = helper_file_quoting_style (argv[0], Q_QDEFAULT);
  return common_firstfile(o, &argv[1], style);
}

/* Common worker for func_lastfile() and func_q_lastfile(). */

static char *common_lastfile (char *o, char **argv, unsigned int style)
{
  struct nameseq *chain = helper_parse_file_list (argv[0], style, 0);
  if (chain)
    {
      struct nameseq *last = chain;
      while (last->next)
        last = last->next;
      o = helper_return_file (o, last->name, style, 1);
      free_ns_chain_no_strcache (chain);
    }
  return o;
}

/* $(lastfile file1 file2 ... fileN) - same as $(lastfile ), except
   for files rather than word tokens.  See func_lastword(). */

static char *func_lastfile (char *o, char **argv, const char *funcname UNUSED)
{
  return common_lastfile (o, argv, Q_IN_QUOTED | Q_RET_QUOTED | Q_SEP_SPACE);
}

/* $(qlastfile style, file1 file2 ... fileN) - same as $(lastfile ), except
   for files rather than word tokens.  See func_lastword(). */

static char *func_q_lastfile (char *o, char **argv, const char *funcname UNUSED)
{
  unsigned int style = helper_file_quoting_style (argv[0], Q_QDEFAULT);
  return common_lastfile (o, &argv[1], style);
}

/* Common worker for func_filelist() and func_q_filelist(). */

static char *common_filelist (char *o, char **argv, unsigned int style)
{
  int start;
  int count;

  /* Check the arguments.  */
  check_numeric (argv[0],
                 _("non-numeric first argument to 'filelist' function"));
  check_numeric (argv[1],
                 _("non-numeric second argument to 'filelist' function"));

  start = atoi (argv[0]);
  if (start < 1)
    ON (fatal, *expanding_var,
        "invalid first argument to 'filelist' function: '%d'", start);
  start--; /* make zero based */

  count = atoi (argv[1]) - start;

  if (count > 0)
    {
      char *line = argv[2];
      struct nameseq *cur;
      struct nameseq *chain;
      chain = helper_parse_file_list (line, style, 0);

      /* Find the beginning of the "start"th word (1-based).  */
      for (cur = chain; cur && start > 1; cur = cur->next)
        start--;

      /* Output the requested count */
      while (cur && count-- > 0)
        o = helper_return_file (o, cur->name, style, count > 0 && cur->next);

      free_ns_chain_no_strcache (chain);
    }

  return o;
}

/* $(filelist start, end, file1..fileN) - same as $(wordlist),
   except for files rather than word tokens.  See func_wordlist(). */

static char *func_filelist (char *o, char **argv, const char *funcname UNUSED)
{
  return common_filelist (o, argv, Q_IN_QUOTED | Q_RET_QUOTED | Q_SEP_SPACE);
}

/* $(qfilelist style, start, end, file1..fileN) - same as $(wordlist),
   except for files rather than word tokens.  See func_wordlist(). */

static char *func_q_filelist (char *o, char **argv, const char *funcname UNUSED)
{
  unsigned int const style = helper_file_quoting_style (argv[0], Q_QDEFAULT);
  return common_filelist (o, &argv[1], style);
}

/* Common worker for func_countfiles() and func_q_countfiles(). */

static char *common_countfiles (char *o, char **argv, unsigned int style)
{
  struct nameseq *chain = helper_parse_file_list (argv[0], style, 0);
  struct nameseq *cur;
  unsigned int files = 0;
  char retval[32];

  for (cur = chain; cur; cur = cur->next)
    files++;
  free_ns_chain_no_strcache (chain);

  return variable_buffer_output (o, retval, sprintf (retval, "%u", files));
}

/* $(countfiles file1 file2 ... fileN) - same as $(words ), except for
   files rather than word tokens.  See func_words(). */

static char *func_countfiles (char *o, char **argv, const char *funcname UNUSED)
{
  return common_countfiles (o, argv, Q_IN_QUOTED | Q_RET_QUOTED | Q_SEP_SPACE);
}

/* $(qcountfiles style, file1 file2 ... fileN) - same as $(words ), except for
   files rather than word tokens and the STYLE argument.  See func_words(). */

static char *func_q_countfiles (char *o, char **argv, const char *funcname UNUSED)
{
  unsigned int const style = helper_file_quoting_style (argv[0], Q_QDEFAULT);
  return common_countfiles (o, &argv[1], style);
}

/* Helper that sets the variable value. */

static void
helper_set_var_value (struct variable *var, const char *value, size_t len)
{
#ifndef CONFIG_WITH_VALUE_LENGTH
  free (var->value);
  var->value = xstrndup (value, len);
#else
  if (len >= var->value_alloc_len)
    {
# ifdef CONFIG_WITH_RDONLY_VARIABLE_VALUE
      if (var->rdonly_val)
        var->rdonly_val = 0;
      else
# endif
        free (var->value);
      var->value_alloc_len = VAR_ALIGN_VALUE_ALLOC (len + 1);
      var->value = xmalloc (var->value_alloc_len);
    }
  memcpy (var->value, value, len);
  var->value[len] = '\0';
  var->value_length = len;
  VARIABLE_CHANGED (var);
#endif
}

/* Common worker for func_foreachfile and func_qforeachfile. */

static char *
common_foreachfile (char *o, char **argv, unsigned int style)
{
  /* expand only the first two.  */
  char *varname = expand_argument (argv[0], NULL);
  char *list = expand_argument (argv[1], NULL);
  const char *body = argv[2];
#ifdef CONFIG_WITH_VALUE_LENGTH
  long body_len = strlen (body);
#endif

  struct nameseq *chain = helper_parse_file_list (list, style, 0);
  struct nameseq *cur;

  /* Clean up the variable name by removing whitespace.  */
  struct variable *var;
  char *vp = next_token (varname);
  char *vp_end = end_of_token (vp);
  vp_end[0] = '\0';

  push_new_variable_scope ();
  var = define_variable (vp, vp_end - vp, "", o_automatic, 0);

  /* Don't need the list any more. */
  free (list);
  list = NULL;

  /* Loop through the chain. */
  for (cur = chain; cur; cur = cur->next)
    {
      /* Update the variable value: */
      unsigned int const len = strlen (cur->name);
      switch (style & Q_RET_MASK)
        {
        case Q_RET_UNQUOTED:
          helper_set_var_value (var, cur->name, len);
          break;
        default:
          { /* Use the output buffer as temporary storage. */
            size_t const saved_off = o - variable_buffer;
            size_t quoted_len;
            char *quoted;
            o = helper_return_file_len (o, cur->name, len, style, 1 /*is_last*/);
            quoted = &variable_buffer[saved_off];
            quoted_len = o - quoted;
            helper_set_var_value (var, quoted, quoted_len);
            o = quoted;
            break;
          }
        }

      /* Expand the body: */
#ifndef CONFIG_WITH_VALUE_LENGTH
      {
        char *result = allocated_variable_expand (body);
        o = variable_buffer_output (o, result, strlen (result));
        free (result);
      }
#else
      variable_expand_string_2 (o, body, body_len, &o);
#endif

      /* Add separator: */
      if (cur->next)
        o = helper_return_sep (o, style);
    }

  pop_variable_scope ();
  free (varname);

  return o;
}

/* $(foreachfile var, filelist, body) - same as $(foreach ), except
   for file rather than word tokens.
   See also func_foreach(). */

static char *
func_foreachfile (char *o, char **argv, const char *funcname UNUSED)
{
  return common_foreachfile (o, argv, Q_IN_QUOTED | Q_RET_QUOTED | Q_SEP_SPACE);
}

/* $(qforeachfile style, var, filelist, body) - same as $(foreach ), except
   for file rather than word tokens and flexible variable value encoding.
   See also func_foreach(). */

static char *
func_q_foreachfile (char *o, char **argv, const char *funcname UNUSED)
{
  unsigned int const style = helper_file_quoting_style (argv[0], Q_QDEFAULT);
  return common_foreachfile (o, &argv[1], style);
}

/* Common worker for func_sortfiles() and func_q_sortfiles(). */

static char *common_sortfiles (char *o, char **argv, unsigned int style,
                               int ascending, int version)
{
  struct nameseq *chain = helper_parse_file_list (argv[0], style, 0);

  /* Count the number of files to determin the array size and whether we've
     got anything to sort. */
  struct nameseq *cur;
  unsigned int num_files = 0;
  for (cur = chain; cur; cur = cur->next)
    num_files++;

  if (num_files <= 1)
    o = helper_return_and_free_chain (o, chain, style);
  else
    {
      /* Create array of string pointers from the chain. */
      char **files = (char **)xmalloc (num_files * sizeof (char *));
      unsigned int idx = 0;
      for (cur = chain; cur; cur = cur->next)
        files[idx++] = (char *)cur->name;

      /* Sort it. */
      if (version)
        qsort(files, num_files, sizeof(files[0]), version_compare_wrapper);
      else
        qsort(files, num_files, sizeof(files[0]), alpha_compare);

      /* Output. We skip equal files. */
      if (ascending)
        for (idx = 0; idx < num_files; idx++)
          {
            const char *curfile = files[idx];
            while (idx + 1 < num_files && strcmp(files[idx + 1], curfile) == 0)
              idx++;
            o = helper_return_file(o, files[idx], style, idx + 1 >= num_files);
          }
      else
        {
          idx = num_files;
          while (idx-- > 0)
            {
              const char *curfile = files[idx];
              while (idx > 0 && strcmp(files[idx - 1], curfile) == 0)
                idx--;
              o = helper_return_file (o, curfile, style, idx == 0);
            }
        }

      free (files);
      free_ns_chain_no_strcache (chain);
    }
  return o;
}

/* $(sortfiles file1 ... fileN) and
   $(rsortfiles file1 ... fileN) - same as $(sort ) and $(rsort ),
   except for files rather than word tokens.  See func_sort(). */

static char *func_sortfiles (char *o, char **argv, const char *funcname)
{
  return common_sortfiles (o, argv, Q_IN_QUOTED | Q_RET_QUOTED | Q_SEP_SPACE,
                           funcname[0] != 'r',
                           funcname[0] == 'v' || funcname[1] == 'v');
}

/* $(qsortfiles style, file1 ... fileN) and
   $(qrsortfiles style, file1 ... fileN) - same as $(sort ) and $(rsort ),
   except for files rather than word tokens and the flexible style.
   See func_sort(). */

static char *func_q_sortfiles (char *o, char **argv, const char *funcname)
{
  unsigned int const style = helper_file_quoting_style (argv[0], Q_QDEFAULT);
  return common_sortfiles (o, &argv[1], style, funcname[1] != 'r',
                           funcname[1] == 'v' || funcname[2] == 'v');
}


/* Helper for determining whether the given path is absolute or not. */

static int helper_is_abs (const char *path)
{
#ifdef HAVE_DOS_PATHS
  return path[0] == '/'
      || path[0] == '\\'
      || (isalpha(path[0]) && path[1] == ':');
#else
  return path[0] == '/';
#endif
}

/* Worker for func_q_abspath and func_q_abspath_ex. */

static char *worker_abspath (char *o, char *line, const char *cwd,
                             size_t cwd_len, unsigned int style)
{
  if (line && *line != '\0')
    {
      PATH_VAR (outbuf);
      struct nameseq *chain = helper_parse_file_list (line, style, 0);

      /* Special case: single path, no cwd - no is_last path trouble */
      if (chain && !chain->next && !cwd)
        {
          if (abspath (chain->name, outbuf))
            o = helper_return_file(o, outbuf, style, 1);
          free_ns_chain_no_strcache (chain);
        }
      else if (chain)
        {
          /* Pass one: replace names by absolute names */
          struct nameseq *prev = NULL;
          struct nameseq *cur = chain;
          while (cur)
            {
              /* If relative path and we've got cwd, join cwd and it. */
              if (cwd && !helper_is_abs (cur->name))
                {
                  size_t len_name = strlen (cur->name);
                  char *name = xrealloc ((char *)cur->name, cwd_len + 1 + len_name + 1);
                  memmove (&name[cwd_len + 1], &name[0], len_name);
                  memcpy (name, cwd, cwd_len);
                  name[cwd_len] = '/';
                  name[cwd_len + 1 + len_name] = '\0';
                  cur->name = name;
                }

              if (abspath (cur->name, outbuf))
                {
                  free ((char *)cur->name);
                  cur->name = xstrdup (outbuf);
                  prev = cur;
                  cur = cur->next;
                }
              else /* remove it */
                cur = helper_unlink_and_free_ns (cur, prev, &chain);
            }

          /* Pass two: output */
          o = helper_return_and_free_chain (o, chain, style);
        }
    }
  return o;
}

/* $(qabspath style, file1 file2 ... fileN) - same as $(abspath ), except
   for files rather than word tokens.  See func_abspath(). */

static char *func_q_abspath (char *o, char **argv, const char *funcname UNUSED)
{
  return worker_abspath (o, argv[1], NULL, 0,
                         helper_file_quoting_style (argv[0], Q_QDEFAULT));
}

# ifdef CONFIG_WITH_ABSPATHEX
/* $(qabspathex style, file1 file2 ... fileN [,cwd]) - same as $(abspathex ),
   except for files rather than word tokens.  See func_abspath_ex(). */

static char *
func_q_abspathex (char *o, char **argv, const char *funcname UNUSED)
{
  /* cwd needs leading spaces chopped and may be optional,
     in which case we're exactly like $(abspath ). */
  char *cwd = argv[2];
  if (cwd)
    {
      while (ISBLANK (*cwd))
        cwd++;
      if (*cwd == '\0')
        cwd = NULL;
    }

  return worker_abspath (o, argv[1], cwd, cwd ? strlen (cwd) : 0,
                         helper_file_quoting_style (argv[0], Q_QDEFAULT));
}
# endif

/* $(qaddprefix style, prefix, file1 ... fileN) and
   $(qaddsuffix style, prefix, file1 ... fileN) - same as $(addprefix )
   and $(addsuffix ) except for files rather than word tokens.
   The suffix/prefix is unquoted on input and subjected to the same quoting
   styling as the file names.
   See func_addsuffix_addprefix(). */

static char *func_q_addsuffix_addprefix (char *o, char **argv, const char *funcname UNUSED)
{
  unsigned int const style = helper_file_quoting_style (argv[0], Q_QDEFAULT);
  const char * const fix = argv[1];
  size_t const fixlen = strlen (fix);
  struct nameseq *chain = helper_parse_file_list (argv[2], style, 0);
  if (chain)
    {
      size_t tmpsize = (fixlen + 512) & ~(size_t)63;
      char *tmp = (char *)xmalloc (tmpsize);
      struct nameseq *cur;

      if (funcname[4] == 'p')
        {
          memcpy (tmp, fix, fixlen);
          for (cur = chain; cur; cur = cur->next)
            {
              size_t curlen = strlen (cur->name);
              if (fixlen + curlen + 1 <= tmpsize)
                { /* likely */ }
              else
                {
                  tmpsize = (fixlen + curlen + 63) & ~(size_t)63;
                  tmp = (char *)xrealloc (tmp, tmpsize);
                }
              memcpy (&tmp[fixlen], cur->name, curlen + 1);
              o = helper_return_file_len (o, tmp, fixlen + curlen,
                                          style, cur->next == NULL);
            }
        }
      else
        for (cur = chain; cur; cur = cur->next)
          {
            size_t curlen = strlen (cur->name);
            if (fixlen + curlen + 1 <= tmpsize)
              { /* likely */ }
            else
              {
                tmpsize = (fixlen + curlen + 63) & ~(size_t)63;
                tmp = (char *)xrealloc (tmp, tmpsize);
              }
            memcpy (tmp, cur->name, curlen);
            memcpy (&tmp[curlen], fix, fixlen + 1);

            o = helper_return_file_len (o, tmp, fixlen + curlen,
                                        style, cur->next == NULL);
          }
      free_ns_chain_no_strcache (chain);
    }
  return o;
}

/* $(qbasename style, path1 .. pathN) and $(qdir style, path1 .. pathN)
   - same as $(basename ) and $(dir ), except for files rather than word tokens.
   See func_basename_dir(). */

static char *
func_q_basename_dir (char *o, char **argv, const char *funcname)
{
  unsigned int const style = helper_file_quoting_style (argv[0], Q_QDEFAULT_VMS_TRICKS);
  struct nameseq *chain = helper_parse_file_list (argv[1], style, 0);
  struct nameseq *cur;

  int const is_basename = funcname[1] == 'b';
  int const is_dir = !is_basename;
  int const stop = MAP_DIRSEP | (is_basename ? MAP_DOT : 0) | MAP_NUL;

  for (cur = chain; cur; cur = cur->next)
    {
      int const is_last = cur->next == NULL;
      const char * const path = cur->name;
      const char * const end = strchr (path, '\0');

      /* Locate the last dot or path separator (P): */
      const char *p = path != end ? end - 1 : end;
      while (p >= path && !STOP_SET (*p, stop))
        --p;

      /* Do the outputting: */
      if (p >= path && (is_dir))
        o = helper_return_file_len (o, path, ++p - path, style, is_last);
      else if (p >= path && *p == '.')
        o = helper_return_file_len (o, path, p - path, style, is_last);
#ifdef HAVE_DOS_PATHS
      /* Handle the "d:foobar" case */
      else if (path[0] && path[1] == ':' && is_dir)
        o = helper_return_file_len (o, path, 2, style, is_last);
#endif
      else if (is_dir)
#ifdef VMS
        {
          extern int vms_report_unix_paths;
          o = helper_return_file_len (o, vms_report_unix_paths ? "./" : "[]",
                                      2, style, is_last);
        }
#else
# ifndef _AMIGA
      o = helper_return_file_len (o, "./", 2, style, is_last);
# else
      ; /* Just a nop...  */
# endif /* AMIGA */
#endif /* !VMS */
      else
        /* The entire name is the basename.  */
        o = helper_return_file_len (o, path, end - path, style, is_last);
    }

  free_ns_chain_no_strcache (chain);
  return o;
}

/* $(qnotdir style, path1 ... pathN) - same as $(notdir ), except for
   files rather than word tokens.  See func_notdir_suffix(). */

static char *
func_q_notdir (char *o, char **argv, const char *funcname)
{
  unsigned int const style = helper_file_quoting_style (argv[0], Q_QDEFAULT_VMS_TRICKS);
  struct nameseq *chain = helper_parse_file_list (argv[1], style, 0);
  struct nameseq *cur;
  int const stop = MAP_DIRSEP;
  (void)funcname;

  for (cur = chain; cur; cur = cur->next)
    {
      int const is_last = cur->next == NULL;
      const char * const path = cur->name;
      const char * const end = strchr(path, '\0');

      /* Locate the last dot or path separator (P): */
      const char *p = path != end ? end - 1 : end;
      while (p >= path && ! STOP_SET (*p, stop))
        --p;

      if ((uintptr_t)p >= (uintptr_t)path)
          o = helper_return_file_len (o, p + 1, end - p - 1, style, is_last);
#ifdef HAVE_DOS_PATHS
      else if (path[0] && path[1] == ':')  /* "d:foo/bar" -> "foo/bar"  */
        o = helper_return_file_len (o, path + 2, end - path - 2, style, is_last);
#endif
      else
        o = helper_return_file_len (o, path, end - path, style, is_last);
    }

  free_ns_chain_no_strcache (chain);
  return o;
}

/* $(qsuffix style, path1 ... pathN) - same as $(suffix ), except for
   files rather than word tokens.  See func_notdir_suffix(). */

static char *
func_q_suffix (char *o, char **argv, const char *funcname)
{
  unsigned int const style = helper_file_quoting_style (argv[0], Q_QDEFAULT_VMS_TRICKS);
  struct nameseq *chain = helper_parse_file_list (argv[1], style, 0);
  struct nameseq *prev;
  struct nameseq *cur;
  int const stop = MAP_DIRSEP | MAP_DOT;
  (void)funcname;

  /* For suffixes we do a pre-pass that removes elements without suffixes.
     This simplifies the handling of end-quoting. */
  prev = NULL;
  cur = chain;
  while (cur)
    {
      const char * const path = cur->name;
      if (strchr (path, '.') != NULL)
        {
          const char * const end = strchr (path, '\0');
          const char *p = end - 1;
          while ((uintptr_t)p >= (uintptr_t)path && ! STOP_SET (*p, stop))
            --p;
          if ((uintptr_t)p >= (uintptr_t)path && *p == '.')
            {
              if (p != path)
                  memmove ((char *)path, p, end - p + 1);
              prev = cur;
              cur = cur->next;
            }
          else /* remove it */
            cur = helper_unlink_and_free_ns (cur, prev, &chain);
        }
      else /* remove it */
        cur = helper_unlink_and_free_ns (cur, prev, &chain);
    }

  /* Output pass: */
  return helper_return_and_free_chain (o, chain, style);
}

# ifdef CONFIG_WITH_ROOT_FUNC
/*
 $(qroot style, path...pathN) - same as $(root ), except files rather
 than space delimited word tokens.  See func_root().

 This is mainly for dealing with drive letters and UNC paths on Windows
 and OS/2.
 */
static char *
func_q_root (char *o, char **argv, const char *funcname UNUSED)
{
  unsigned int const style = helper_file_quoting_style (argv[0], Q_QDEFAULT);
  struct nameseq *chain = helper_parse_file_list (argv[1], style, 0);
  struct nameseq *prev;
  struct nameseq *cur;

  /* First pass: Strip non-root components and remove rootless elements. */
  prev = NULL;
  cur = chain;
  while (cur)
    {
      const char *path = cur->name;
      const char *end  = NULL;
      char ch;

# ifdef HAVE_DOS_PATHS
      if (isalpha(path[0]) && path[1] == ':')
          end = path + 2;
      else if (   IS_PATHSEP(path[0])
               && IS_PATHSEP(path[1])
               && !IS_PATHSEP(path[2]) && path[2]
               && path[3])
        {
          /* Min recognized UNC: "//./" - find the next slash
             Typical root: "//srv/shr/" */
          /* XXX: Check if //./ needs special handling. */
          end = path + 3;
          while ((ch = *end) != '\0' && !IS_PATHSEP(ch))
            end++;

          if (IS_PATHSEP(ch) && !IS_PATHSEP(end[1]))
            {
              end++;
              while ((ch = *end) != '\0' && !IS_PATHSEP(ch))
                end++;
            }
          else
            end = NULL; /* invalid */
        }
      else if (IS_PATHSEP(*end))
        end = path + 1;
      else
        end = NULL;

# elif defined (VMS) || defined (AMGIA)
      /* XXX: VMS and AMGIA */
      OS (fatal, NILF, _("$(%s ) is not implemented on this platform"), funcname);
# else
      if (IS_PATHSEP(*path))
        end = path + 1;
# endif
      if (end != NULL)
        {
          /* Include all subsequent path separators. */

          while ((ch = *end) != '\0' && IS_PATHSEP(ch))
            end++;
          *(char *)end = '\0';

          prev = cur;
          cur = cur->next;
        }
      else
        cur = helper_unlink_and_free_ns(cur, prev, &chain);
    }

  /* Second pass: Output */
  return helper_return_and_free_chain (o, chain, style);
}

/*
 $(qnotroot style, path1 .. pathN) - same as $(notroot ), except files
 rather than space delimited word tokens.  See func_notroot().

 This is mainly for dealing with drive letters and UNC paths on Windows
 and OS/2.
 */
static char *
func_q_notroot (char *o, char **argv, const char *funcname UNUSED)
{
  unsigned int const style = helper_file_quoting_style (argv[0], Q_QDEFAULT);
  struct nameseq *chain = helper_parse_file_list (argv[1], style, 0);
  struct nameseq *cur;

  for (cur = chain; cur; cur = cur->next)
    {
      const char *start = cur->name;
      char ch;

# ifdef HAVE_DOS_PATHS
      if (isalpha(start[0]) && start[1] == ':')
        start += 2;
      else if (   IS_PATHSEP(start[0])
               && IS_PATHSEP(start[1])
               && !IS_PATHSEP(start[2]) && start[2] != '\0'
               && start[3] != '\0')
        {
          /* Min recognized UNC: "//./" - find the next slash
             Typical root: "//srv/shr/" */
          /* XXX: Check if //./ needs special handling. */
          start += 3;
          while ((ch = *start) != '\0' && !IS_PATHSEP(ch))
            start++;

          if (IS_PATHSEP(ch) && !IS_PATHSEP(start[1]))
            {
              start++;
              while ((ch = *start) != '\0' && !IS_PATHSEP(ch))
                start++;
            }
          else
            start = cur->name; /* invalid UNC, pretend it's a couple unixy root slashes. */
        }

# elif defined (VMS) || defined (AMGIA)
      /* XXX: VMS and AMGIA */
      OS (fatal, NILF, _("$(%s) is not implemented on this platform"), funcname);
# endif

      /* Exclude all subsequent / leading path separators. */
      while ((ch = *start) != '\0' && IS_PATHSEP(ch))
        start++;

      if (ch != '\0')
        o = helper_return_file(o, start, style, cur->next == NULL);
      else
        o = helper_return_file_len (o, ".", 1, style, cur->next == NULL);
    }

  free_ns_chain_no_strcache (chain);
  return o;
}

# endif

/* $(qrealpath style, path1 .. pathN) - same as $(realpath ), except files
 rather than space delimited word tokens.  See func_realpath(). */

static char *
func_q_realpath (char *o, char **argv, const char *funcname UNUSED)
{
  PATH_VAR (outbuf);
  unsigned int const style = helper_file_quoting_style (argv[0], Q_QDEFAULT);
  struct nameseq *chain = helper_parse_file_list (argv[1], style, 0);

  /* Pass one: Do the realpath/abspath thing and remove anything that fails
               or doesn't exists. */
  struct nameseq *cur = chain;
  struct nameseq *prev = NULL;
  while (cur)
    {
      char *result;
#ifdef HAVE_REALPATH
      ENULLLOOP (result, realpath (cur->name, outbuf));
#else
      result = abspath (cur->name, outbuf);
#endif
      if (result)
        {
           struct stat st;
           int r;
           EINTRLOOP (r, stat (outbuf, &st));
           if (r == 0)
             {
               free ((char *)cur->name);
               cur->name = xstrdup (result);
               prev = cur;
               cur = cur->next;
             }
           else
             cur = helper_unlink_and_free_ns(cur, prev, &chain);
        }
      else
        cur = helper_unlink_and_free_ns(cur, prev, &chain);
    }

  /* Pass two: Output. */
  return helper_return_and_free_chain (o, chain, style);
}

/* $(qwildcard path1 .. pathN [, style]) - same as $(wildcard ), except files
 rather than space delimited word tokens.  See func_wildcard(). */

static char *
func_q_wildcard (char *o, char **argv, const char *funcname UNUSED)
{
  unsigned int const style = helper_file_quoting_style (argv[0], Q_QDEFAULT);
  struct nameseq *chain = helper_parse_file_list (argv[1], style, 1 /*glob*/);
#ifdef _AMIGA
  OS (fatal, NILF, _("$(%s ) is not implemented on this platform"), funcname);
#endif
  return helper_return_and_free_chain (o, chain, style);
}

static char *
worker_filter_filterout (char *o, char **argv, unsigned style, int is_filter)
{
  struct nameseq *wordchain;
  struct a_word *wordhead;
  struct a_word **wordtail;
  struct a_word *wp;
  struct nameseq *patchain;
  struct a_pattern *pathead;
  struct a_pattern **pattail;
  struct a_pattern *pp;
  struct nameseq *cur;
  struct hash_table a_word_table;
  int literals;
  int words;
  int hashing;
  unsigned int words_len; /* for output estimation */

  /* Chop ARGV[0] up into patterns to match against the words. */
  /** @todo this very inefficient as we convert between two list format and
   *        duplicates the patterns on the heap.  We could just modify argv[0]
   *        directly. */
  patchain = helper_parse_file_list (argv[0], style, 0 /*glob*/);
  pattail = &pathead;
  for (cur = patchain, literals = 0; cur; cur = cur->next)
    {
      struct a_pattern *pat = alloca (sizeof (struct a_pattern));

      *pattail = pat;
      pattail = &pat->next;

      pat->str = (char *)cur->name; /* (safe - PARSEFS_NOCACHE) */
      pat->percent = find_percent (pat->str); /* may modify name */
      if (pat->percent == 0)
        literals++;
      pat->sfxlen = pat->percent ? strlen(pat->percent + 1) : 0;
      pat->length = strlen (pat->str);
    }
  *pattail = NULL;

  /* Chop ARGV[1] up into words to match against the patterns.  */
  /** @todo this very inefficient as we convert between two list format and
   *        duplicates the words on the heap.  We could just modify argv[1]
   *        directly. */
  wordchain = helper_parse_file_list (argv[1], style, 0 /*glob*/);
  wordtail = &wordhead;
  for (cur = wordchain, words = 0, words_len = 0; cur; cur = cur->next)
    {
      struct a_word *word = alloca (sizeof (struct a_word));

      *wordtail = word;
      wordtail = &word->next;

      word->str = (char *)cur->name; /* (safe - PARSEFS_NOCACHE) */
      word->length = strlen (cur->name);
      words_len += word->length + 1;
      word->matched = 0;
      word->chain = NULL;
      words++;
    }
  *wordtail = NULL;

  /* Only use a hash table if arg list lengths justifies the cost.  */
  hashing = (literals >= 2 && (literals * words) >= 10);
  if (hashing)
    {
      hash_init (&a_word_table, words, a_word_hash_1, a_word_hash_2,
                 a_word_hash_cmp);
      for (wp = wordhead; wp != 0; wp = wp->next)
        {
          struct a_word *owp = hash_insert (&a_word_table, wp);
          if (owp)
            wp->chain = owp;
        }
    }

  if (words)
    {
      int doneany = 0;

      if (is_filter)
        words_len = 0;

      /* Run each pattern through the words, killing words.  */
      for (pp = pathead; pp != 0; pp = pp->next)
        {
          if (pp->percent)
            for (wp = wordhead; wp != 0; wp = wp->next)
              if (!wp->matched
                  && pattern_matches_ex (pp->str, pp->percent, pp->sfxlen,
                                         wp->str, wp->length))
                {
                  wp->matched = 1;
                  if (is_filter)
                    words_len += wp->length + 1;
                  else
                    words_len -= wp->length + 1;
                }
          else if (hashing)
            {
              struct a_word a_word_key;
              a_word_key.str = pp->str;
              a_word_key.length = pp->length;
              wp = hash_find_item (&a_word_table, &a_word_key);
              while (wp)
                {
                  if (!wp->matched)
                    {
                      wp->matched = 1;
                      if (is_filter)
                        words_len += wp->length + 1;
                      else
                        words_len -= wp->length + 1;
                    }
                  wp = wp->chain;
                }
            }
          else
            for (wp = wordhead; wp != 0; wp = wp->next)
              if (!wp->matched
                  && wp->length == pp->length
                  && strneq (pp->str, wp->str, wp->length))
                {
                  wp->matched = 1;
                  if (is_filter)
                    words_len += wp->length + 1;
                  else
                    words_len -= wp->length + 1;
                }
        }

      /* Output the words that matched (or didn't, for filter-out).  */
      o = ensure_variable_buffer_space (o, words_len);

      for (wp = wordhead; wp != 0; wp = wp->next)
        if (wp->matched == is_filter)
          {
            o = helper_return_file_len (o, wp->str, wp->length,
                                        style, 0 /*is_last*/);
            doneany = 1;
          }

      /* Kill the last separator. */
      if (doneany)
        o = helper_drop_separator (o, style);
    }

  /* Clean up. */
  if (hashing)
    hash_free (&a_word_table, 0);
  free_ns_chain_no_strcache (wordchain);
  free_ns_chain_no_strcache (patchain);

  return o;
}

/* Implements $(qfilter ) and $(qfilter-out ). */
static char *
func_q_filter_filterout (char *o, char **argv, const char *funcname)
{
  unsigned int const style = helper_file_quoting_style (argv[0], Q_QDEFAULT);
  return worker_filter_filterout (o, &argv[1], style, funcname[7] == '\0');
}

#endif /* KMK */

#ifdef CONFIG_WITH_LAZY_DEPS_VARS

/* Helper that parses the index argument for the $(deps* ) and $(qdeps* )
   functions. */
static unsigned int parse_dep_index (const char *index, const char *funcname)
{
  unsigned int idx = 0;
  if (index)
    {
      while (ISSPACE (*index))
        index++;
      if (*index != '\0')
        {
          char *next = (char *)index;
          long l = strtol (index, &next, 0);
          while (ISSPACE (*next))
            next++;
          idx = (unsigned int)l;
          if (*next != '\0' || l < 0 || (long)idx != l)
            OSS (fatal, NILF, _("%s: invalid index value: `%s'\n"), funcname, index);
        }
    }
  return idx;
}

/* Helper that calculates the output length for a dependency */

MY_INLINE unsigned int helper_dep_output_len (struct dep *d)
{
  const char *c = dep_name (d);
#ifndef NO_ARCHIVES
  if (ar_name (c))
    return strlen (strchr (c, '(') + 1);
#endif
#ifdef CONFIG_WITH_STRCACHE2
  if (!d->need_2nd_expansion)
    return strcache2_get_len (&file_strcache, c) + 1;
#endif
  return strlen (c) + 1;
}

/* Helper that outputs a depndency. */

MY_INLINE char *helper_dep_output_one (char *o, struct dep *d,
                                       unsigned int style, int is_last)
{
  const char *c = dep_name (d);
#ifndef NO_ARCHIVES
  if (ar_name (c))
    {
      c = strchr(c, '(') + 1;
      o = helper_return_file_len (o, c, strlen(c) - 1, style, is_last);
    }
  else
#endif
#ifdef CONFIG_WITH_STRCACHE2
  if (!d->need_2nd_expansion)
    {
      unsigned int len = strcache2_get_len (&file_strcache, c);
      o = helper_return_file_len (o, c, len, style, is_last);
    }
  else
#endif
    o = helper_return_file (o, c, style, is_last);
  return o;
}

/* Implements $^/$(deps )/$(qdeps ) and $+/$(deps-all )/$(qdeps-all ).

   If no second argument is given, or if it's empty, or if it's zero,
   all dependencies will be returned.  If the second argument is non-zero
   the dependency at that position will be returned.  If the argument is
   negative a fatal error is thrown.  */
static char *
worker_deps (char *o, char **argv, const char *funcname, unsigned int style,
             int all_deps)
{
  unsigned int idx = parse_dep_index (argv[1], funcname);
  struct file *file;

  /* Find the file and select the list corresponding to FUNCNAME. */

  file = lookup_file (argv[0]);
  if (file)
    {
      struct dep *deps;
      struct dep *d;
      if (!all_deps)
        {
          deps = file->deps_no_dupes;
          if (!deps && file->deps)
            deps = file->deps_no_dupes = create_uniqute_deps_chain (file->deps);
        }
      else
        deps = file->deps;

      if (   file->double_colon
          && (   file->double_colon != file
              || file->last != file))
          OSS (error, NILF, _("$(%s ) cannot be used on files with multiple double colon rules like `%s'\n"),
               funcname, file->name);

      if (idx == 0 /* all */)
        {
          /* Since this may be a long list, calculate the output space needed if
             no quoting takes place and no multichar separators are used. */

          unsigned int total_len = 0;
          for (d = deps; d; d = d->next)
            if (!d->ignore_mtime)
              total_len += helper_dep_output_len (d);
          if (total_len > 0)
            {
              o = ensure_variable_buffer_space (o, total_len);

              for (d = deps; d; d = d->next)
                if (!d->ignore_mtime)
                  o = helper_dep_output_one (o, d, style, 0 /*is_last*/);

              /* nuke the last list separator */
              o = helper_drop_separator (o, style);
            }
        }
      else
        {
          /* Dependency given by index.  */

          for (d = deps; d; d = d->next)
            if (!d->ignore_mtime)
              if (--idx == 0) /* 1 based indexing */
                return helper_dep_output_one (o, d, style, 1 /*is_last*/);
        }
    }

  return o;
}

/* Implements $? / $(deps-newer ) / $(qdeps-newer ).

   If no second argument is given, or if it's empty, or if it's zero,
   all dependencies will be returned.  If the second argument is non-zero
   the dependency at that position will be returned.  If the argument is
   negative a fatal error is thrown.  */
static char *
worker_deps_newer (char *o, char **argv, const char *funcname, unsigned int style)
{
  unsigned int idx = parse_dep_index (argv[1], funcname);
  struct file *file;

  /* Find the file. */

  file = lookup_file (argv[0]);
  if (file)
    {
      struct dep *deps = file->deps;
      struct dep *d;

      if (   file->double_colon
          && (   file->double_colon != file
              || file->last != file))
          OSS (error, NILF, _("$(%s ) cannot be used on files with multiple double colon rules like `%s'\n"),
               funcname, file->name);

      if (idx == 0 /* all */)
        {
          unsigned int total_len = 0;

          /* calc the result length. */

          for (d = deps; d; d = d->next)
            if (!d->ignore_mtime && d->changed)
              total_len += helper_dep_output_len (d);
          if (total_len)
            {
              o = ensure_variable_buffer_space (o, total_len);

              for (d = deps; d; d = d->next)
                if (!d->ignore_mtime && d->changed)
                  o = helper_dep_output_one (o, d, style, 0 /*is_last*/);

              /* nuke the last list separator */
              o = helper_drop_separator (o, style);
            }
        }
      else
        {
          /* Dependency given by index.  */

          for (d = deps; d; d = d->next)
            if (!d->ignore_mtime && d->changed)
              if (--idx == 0) /* 1 based indexing */
                return helper_dep_output_one (o, d, style, 1 /*is_last*/);
        }
    }

  return o;
}

/* Implements $|, the order only dependency list.

   If no second argument is given, or if it's empty, or if it's zero,
   all dependencies will be returned.  If the second argument is non-zero
   the dependency at that position will be returned.  If the argument is
   negative a fatal error is thrown.  */
static char *
worker_deps_order_only (char *o, char **argv, const char *funcname, unsigned int style)
{
  unsigned int idx = parse_dep_index (argv[1], funcname);
  struct file *file;

  /* Find the file. */

  file = lookup_file (argv[0]);
  if (file)
    {
      struct dep *deps = file->deps;
      struct dep *d;

      if (   file->double_colon
          && (   file->double_colon != file
              || file->last != file))
          OSS (error, NILF, _("$(%s ) cannot be used on files with multiple double colon rules like `%s'\n"),
               funcname, file->name);

      if (idx == 0 /* all */)
        {
          unsigned int total_len = 0;

          /* calc the result length. */

          for (d = deps; d; d = d->next)
            if (d->ignore_mtime)
              total_len += helper_dep_output_len (d);
          if (total_len)
            {
              o = ensure_variable_buffer_space (o, total_len);

              for (d = deps; d; d = d->next)
                if (d->ignore_mtime)
                  o = helper_dep_output_one (o, d, style, 0 /*is_last*/);

              /* nuke the last list separator */
              o = helper_drop_separator (o, style);
            }
        }
      else
        {
          /* Dependency given by index.  */

          for (d = deps; d; d = d->next)
            if (d->ignore_mtime)
              if (--idx == 0) /* 1 based indexing */
                return helper_dep_output_one (o, d, style, 1 /*is_last*/);
        }
    }

  return o;
}

/* Implements $^ and $+.

   The first comes with FUNCNAME 'deps', the second as 'deps-all'.

   If no second argument is given, or if it's empty, or if it's zero,
   all dependencies will be returned.  If the second argument is non-zero
   the dependency at that position will be returned.  If the argument is
   negative a fatal error is thrown.  */
static char *
func_deps (char *o, char **argv, const char *funcname)
{
# ifdef VMS
  return worker_deps (o, argv, funcname, Q_RET_UNQUOTED | Q_SEP_COMMA,
# else
  return worker_deps (o, argv, funcname, Q_RET_UNQUOTED | Q_SEP_SPACE,
# endif
                      funcname[4] != '\0');
}

/* Implements $?.

   If no second argument is given, or if it's empty, or if it's zero,
   all dependencies will be returned.  If the second argument is non-zero
   the dependency at that position will be returned.  If the argument is
   negative a fatal error is thrown.  */
static char *
func_deps_newer (char *o, char **argv, const char *funcname)
{
# ifdef VMS
  return worker_deps_newer (o, argv, funcname, Q_RET_UNQUOTED | Q_SEP_COMMA);
# else
  return worker_deps_newer (o, argv, funcname, Q_RET_UNQUOTED | Q_SEP_SPACE);
# endif
}

/* Implements $|, the order only dependency list.

   If no second argument is given, or if it's empty, or if it's zero,
   all dependencies will be returned.  If the second argument is non-zero
   the dependency at that position will be returned.  If the argument is
   negative a fatal error is thrown.  */
static char *
func_deps_order_only (char *o, char **argv, const char *funcname)
{
# ifdef VMS
  return worker_deps_order_only (o, argv, funcname, Q_RET_UNQUOTED | Q_SEP_COMMA);
# else
  return worker_deps_order_only (o, argv, funcname, Q_RET_UNQUOTED | Q_SEP_SPACE);
# endif
}

#endif /* CONFIG_WITH_LAZY_DEPS_VARS */
#ifdef KMK

/* Implements $(qdeps ) and $(qdeps-all )

   If no third argument is given, or if it's empty, or if it's zero,
   all dependencies will be returned.  If the third argument is non-zero
   the dependency at that position will be returned.  If the argument is
   negative a fatal error is thrown.  */
static char *
func_q_deps (char *o, char **argv, const char *funcname)
{
  unsigned int const style = helper_file_quoting_style (argv[0], Q_QDEFAULT);
  return worker_deps (o, &argv[1], funcname, style, funcname[5] != '\0');
}

/* Implements $(qdeps-newer ).

   If no third argument is given, or if it's empty, or if it's zero,
   all dependencies will be returned.  If the third argument is non-zero
   the dependency at that position will be returned.  If the argument is
   negative a fatal error is thrown.  */
static char *
func_q_deps_newer (char *o, char **argv, const char *funcname)
{
  unsigned int const style = helper_file_quoting_style (argv[0], Q_QDEFAULT);
  return worker_deps_newer (o, &argv[1], funcname, style);
}

/* Implements $(qdeps-oo ), the order only dependency list.

   If no third argument is given, or if it's empty, or if it's zero,
   all dependencies will be returned.  If the third argument is non-zero
   the dependency at that position will be returned.  If the argument is
   negative a fatal error is thrown.  */
static char *
func_q_deps_order_only (char *o, char **argv, const char *funcname)
{
  unsigned int const style = helper_file_quoting_style (argv[0], Q_QDEFAULT);
  return worker_deps_order_only (o, &argv[1], funcname, style);
}

/* Implements $(qtarget ), converting a single unquoted file to the given
   quoting style.

   Typically used like this:
      $(qtarget sh,$@)
      $(qone-unquoted sh,$@) */
static char *
func_q_one_unquoted (char *o, char **argv, const char *funcname)
{
  unsigned int const style = helper_file_quoting_style (argv[0], Q_QDEFAULT);
  return helper_return_file (o, argv[1], style, 1 /*is_last*/);
}

#endif /* KMK */

/* Lookup table for builtin functions.

   This doesn't have to be sorted; we use a straight lookup.  We might gain
   some efficiency by moving most often used functions to the start of the
   table.

   If MAXIMUM_ARGS is 0, that means there is no maximum and all
   comma-separated values are treated as arguments.

   EXPAND_ARGS means that all arguments should be expanded before invocation.
   Functions that do namespace tricks (foreach) don't automatically expand.  */

static char *func_call (char *o, char **argv, const char *funcname);

#define FT_ENTRY(_name, _min, _max, _exp, _func) \
  { { (_func) }, STRING_SIZE_TUPLE(_name), (_min), (_max), (_exp), 0 }

static struct function_table_entry function_table_init[] =
{
 /*         Name            MIN MAX EXP? Function */
  FT_ENTRY ("abspath",       0,  1,  1,  func_abspath),
  FT_ENTRY ("addprefix",     2,  2,  1,  func_addsuffix_addprefix),
  FT_ENTRY ("addsuffix",     2,  2,  1,  func_addsuffix_addprefix),
  FT_ENTRY ("basename",      0,  1,  1,  func_basename_dir),
  FT_ENTRY ("dir",           0,  1,  1,  func_basename_dir),
  FT_ENTRY ("notdir",        0,  1,  1,  func_notdir_suffix),
#ifdef CONFIG_WITH_ROOT_FUNC
  FT_ENTRY ("root",          0,  1,  1,  func_root),
  FT_ENTRY ("notroot",       0,  1,  1,  func_notroot),
#endif
  FT_ENTRY ("subst",         3,  3,  1,  func_subst),
  FT_ENTRY ("suffix",        0,  1,  1,  func_notdir_suffix),
  FT_ENTRY ("filter",        2,  2,  1,  func_filter_filterout),
  FT_ENTRY ("filter-out",    2,  2,  1,  func_filter_filterout),
  FT_ENTRY ("findstring",    2,  2,  1,  func_findstring),
#ifdef CONFIG_WITH_DEFINED_FUNCTIONS
  FT_ENTRY ("firstdefined",  0,  2,  1,  func_firstdefined),
#endif
  FT_ENTRY ("firstword",     0,  1,  1,  func_firstword),
  FT_ENTRY ("flavor",        0,  1,  1,  func_flavor),
  FT_ENTRY ("join",          2,  2,  1,  func_join),
#ifdef CONFIG_WITH_DEFINED_FUNCTIONS
  FT_ENTRY ("lastdefined",   0,  2,  1,  func_lastdefined),
#endif
  FT_ENTRY ("lastword",      0,  1,  1,  func_lastword),
  FT_ENTRY ("patsubst",      3,  3,  1,  func_patsubst),
  FT_ENTRY ("realpath",      0,  1,  1,  func_realpath),
#ifdef CONFIG_WITH_RSORT
  FT_ENTRY ("rsort",         0,  1,  1,  func_sort),
# ifdef KMK
  FT_ENTRY ("rversort",      0,  1,  1,  func_sort),
# endif
#endif
  FT_ENTRY ("shell",         0,  1,  1,  func_shell),
  FT_ENTRY ("sort",          0,  1,  1,  func_sort),
# ifdef KMK
  FT_ENTRY ("versort",       0,  1,  1,  func_sort),
# endif
  FT_ENTRY ("strip",         0,  1,  1,  func_strip),
#ifdef CONFIG_WITH_WHERE_FUNCTION
  FT_ENTRY ("where",         0,  1,  1,  func_where),
#endif
  FT_ENTRY ("wildcard",      0,  1,  1,  func_wildcard),
  FT_ENTRY ("word",          2,  2,  1,  func_word),
  FT_ENTRY ("wordlist",      3,  3,  1,  func_wordlist),
  FT_ENTRY ("words",         0,  1,  1,  func_words),
  FT_ENTRY ("origin",        0,  1,  1,  func_origin),
  FT_ENTRY ("foreach",       3,  3,  0,  func_foreach),
#ifdef CONFIG_WITH_LOOP_FUNCTIONS
  FT_ENTRY ("for",           4,  4,  0,  func_for),
  FT_ENTRY ("while",         2,  2,  0,  func_while),
#endif
  FT_ENTRY ("call",          1,  0,  1,  func_call),
  FT_ENTRY ("info",          0,  1,  1,  func_error),
  FT_ENTRY ("error",         0,  1,  1,  func_error),
  FT_ENTRY ("warning",       0,  1,  1,  func_error),
  FT_ENTRY ("if",            2,  3,  0,  func_if),
  FT_ENTRY ("or",            1,  0,  0,  func_or),
  FT_ENTRY ("and",           1,  0,  0,  func_and),
  FT_ENTRY ("value",         0,  1,  1,  func_value),
#ifdef EXPERIMENTAL
  FT_ENTRY ("eq",            2,  2,  1,  func_eq),
  FT_ENTRY ("not",           0,  1,  1,  func_not),
#endif
  FT_ENTRY ("eval",          0,  1,  1,  func_eval),
#ifdef CONFIG_WITH_EVALPLUS
  FT_ENTRY ("evalctx",       0,  1,  1,  func_evalctx),
  FT_ENTRY ("evalval",       1,  1,  1,  func_evalval),
  FT_ENTRY ("evalvalctx",    1,  1,  1,  func_evalval),
  FT_ENTRY ("evalcall",      1,  0,  1,  func_call),
  FT_ENTRY ("evalcall2",     1,  0,  1,  func_call),
  FT_ENTRY ("eval-opt-var",  1,  0,  1,  func_eval_optimize_variable),
#endif
  FT_ENTRY ("file",          1,  2,  1,  func_file),
#ifdef CONFIG_WITH_STRING_FUNCTIONS
  FT_ENTRY ("length",        1,  1,  1,  func_length),
  FT_ENTRY ("length-var",    1,  1,  1,  func_length_var),
  FT_ENTRY ("insert",        2,  5,  1,  func_insert),
  FT_ENTRY ("pos",           2,  3,  1,  func_pos),
  FT_ENTRY ("lastpos",       2,  3,  1,  func_pos),
  FT_ENTRY ("substr",        2,  4,  1,  func_substr),
  FT_ENTRY ("translate",     2,  4,  1,  func_translate),
#endif
#ifdef CONFIG_WITH_PRINTF
  FT_ENTRY ("printf",        1,  0,  1,  kmk_builtin_func_printf),
#endif
#ifdef CONFIG_WITH_LAZY_DEPS_VARS
  FT_ENTRY ("deps",          1,  2,  1,  func_deps),
  FT_ENTRY ("deps-all",      1,  2,  1,  func_deps),
  FT_ENTRY ("deps-newer",    1,  2,  1,  func_deps_newer),
  FT_ENTRY ("deps-oo",       1,  2,  1,  func_deps_order_only),
#endif
#ifdef CONFIG_WITH_DEFINED
  FT_ENTRY ("defined",       1,  1,  1,  func_defined),
#endif
#ifdef CONFIG_WITH_TOUPPER_TOLOWER
  FT_ENTRY ("toupper",       0,  1,  1,  func_toupper_tolower),
  FT_ENTRY ("tolower",       0,  1,  1,  func_toupper_tolower),
#endif
#ifdef CONFIG_WITH_ABSPATHEX
  FT_ENTRY ("abspathex",     0,  2,  1,  func_abspathex),
#endif
#ifdef CONFIG_WITH_XARGS
  FT_ENTRY ("xargs",         2,  0,  1,  func_xargs),
#endif
#if defined(CONFIG_WITH_VALUE_LENGTH) && defined(CONFIG_WITH_COMPARE)
  FT_ENTRY ("comp-vars",     3,  3,  1,  func_comp_vars),
  FT_ENTRY ("comp-cmds",     3,  3,  1,  func_comp_vars),
  FT_ENTRY ("comp-cmds-ex",  3,  3,  1,  func_comp_cmds_ex),
#endif
#ifdef CONFIG_WITH_DATE
  FT_ENTRY ("date",          0,  1,  1,  func_date),
  FT_ENTRY ("date-utc",      0,  3,  1,  func_date),
#endif
#ifdef CONFIG_WITH_FILE_SIZE
  FT_ENTRY ("file-size",     1,  1,  1,  func_file_size),
#endif
#ifdef CONFIG_WITH_WHICH
  FT_ENTRY ("which",         0,  0,  1,  func_which),
#endif
#ifdef CONFIG_WITH_IF_CONDITIONALS
  FT_ENTRY ("expr",          1,  1,  0,  func_expr),
  FT_ENTRY ("if-expr",       2,  3,  0,  func_if_expr),
  FT_ENTRY ("select",        2,  0,  0,  func_select),
#endif
#ifdef CONFIG_WITH_SET_CONDITIONALS
  FT_ENTRY ("intersects",    2,  2,  1,  func_set_intersects),
#endif
#ifdef CONFIG_WITH_STACK
  FT_ENTRY ("stack-push",    2,  2,  1,  func_stack_push),
  FT_ENTRY ("stack-pop",     1,  1,  1,  func_stack_pop_top),
  FT_ENTRY ("stack-popv",    1,  1,  1,  func_stack_pop_top),
  FT_ENTRY ("stack-top",     1,  1,  1,  func_stack_pop_top),
#endif
#ifdef CONFIG_WITH_MATH
  FT_ENTRY ("int-add",       2,  0,  1,  func_int_add),
  FT_ENTRY ("int-sub",       2,  0,  1,  func_int_sub),
  FT_ENTRY ("int-mul",       2,  0,  1,  func_int_mul),
  FT_ENTRY ("int-div",       2,  0,  1,  func_int_div),
  FT_ENTRY ("int-mod",       2,  2,  1,  func_int_mod),
  FT_ENTRY ("int-not",       1,  1,  1,  func_int_not),
  FT_ENTRY ("int-and",       2,  0,  1,  func_int_and),
  FT_ENTRY ("int-or",        2,  0,  1,  func_int_or),
  FT_ENTRY ("int-xor",       2,  0,  1,  func_int_xor),
  FT_ENTRY ("int-eq",        2,  2,  1,  func_int_cmp),
  FT_ENTRY ("int-ne",        2,  2,  1,  func_int_cmp),
  FT_ENTRY ("int-gt",        2,  2,  1,  func_int_cmp),
  FT_ENTRY ("int-ge",        2,  2,  1,  func_int_cmp),
  FT_ENTRY ("int-lt",        2,  2,  1,  func_int_cmp),
  FT_ENTRY ("int-le",        2,  2,  1,  func_int_cmp),
#endif
#ifdef CONFIG_WITH_NANOTS
  FT_ENTRY ("nanots",        0,  0,  0,  func_nanots),
#endif
#ifdef CONFIG_WITH_OS2_LIBPATH
  FT_ENTRY ("libpath",       1,  2,  1,  func_os2_libpath),
#endif
#if defined (CONFIG_WITH_MAKE_STATS) || defined (CONFIG_WITH_MINIMAL_STATS)
  FT_ENTRY ("make-stats",    0,  0,  0,  func_make_stats),
#endif
#ifdef CONFIG_WITH_COMMANDS_FUNC
  FT_ENTRY ("commands",      1,  1,  1,  func_commands),
  FT_ENTRY ("commands-sc",   1,  1,  1,  func_commands),
  FT_ENTRY ("commands-usr",  2,  2,  1,  func_commands),
#endif
#ifdef KMK_HELPERS
  FT_ENTRY ("kb-src-tool",   1,  2,  0,  func_kbuild_source_tool),
  FT_ENTRY ("kb-obj-base",   1,  2,  0,  func_kbuild_object_base),
  FT_ENTRY ("kb-obj-suff",   1,  2,  0,  func_kbuild_object_suffix),
  FT_ENTRY ("kb-src-prop",   3,  4,  0,  func_kbuild_source_prop),
  FT_ENTRY ("kb-src-one",    0,  1,  0,  func_kbuild_source_one),
  FT_ENTRY ("kb-exp-tmpl",   6,  6,  1,  func_kbuild_expand_template),
#endif
#ifdef KMK
  FT_ENTRY ("dircache-ctl",  1,  0,  1,  func_dircache_ctl),
  FT_ENTRY ("breakpoint",    0,  0,  0,  func_breakpoint),
  FT_ENTRY ("set-umask",     1,  3,  1,  func_set_umask),
  FT_ENTRY ("get-umask",     0,  0,  0,  func_get_umask),
#endif
#ifdef KMK
  FT_ENTRY ("quote",         1,  0,  1,  func_quote_make),
  FT_ENTRY ("quote-dep",     1,  0,  1,  func_quote_make),
  FT_ENTRY ("quote-tgt",     1,  0,  1,  func_quote_make),
  FT_ENTRY ("quote-depend",  1,  0,  1,  func_quote_make),
  FT_ENTRY ("quote-tgtend",  1,  0,  1,  func_quote_make),
  FT_ENTRY ("quote-sh",      1,  0,  1,  func_quote_shell),
  FT_ENTRY ("quote-sh-dq",   1,  1,  1,  func_quote_shell_dq),
  FT_ENTRY ("quote-sh-sq",   1,  1,  1,  func_quote_shell_sq),
  FT_ENTRY ("requote",       1,  0,  1,  func_requote),
  /* Quoted input and maybe output variants of functions typically
     working with files: */
  FT_ENTRY ("firstfile",     0,  1, 1, func_firstfile),
  FT_ENTRY ("lastfile",      0,  1, 1, func_lastfile),
  FT_ENTRY ("filelist",      3,  3, 1, func_filelist),
  FT_ENTRY ("countfiles",    0,  1, 1, func_countfiles),
  FT_ENTRY ("foreachfile",   3,  3, 0, func_foreachfile),
  FT_ENTRY ("sortfiles",     0,  1, 1, func_sortfiles),
  FT_ENTRY ("versortfiles",  0,  1, 1, func_sortfiles),
# ifdef CONFIG_WITH_RSORT
  FT_ENTRY ("rsortfiles",    0,  1, 1, func_sortfiles),
  FT_ENTRY ("rversortfiles", 0,  1, 1, func_sortfiles),
# endif
  /* Function variants with preceding style argument and quoting by default. */
  FT_ENTRY ("qfirstfile",   1+0, 1+1, 1, func_q_firstfile),
  FT_ENTRY ("qlastfile",    1+0, 1+1, 1, func_q_lastfile),
  FT_ENTRY ("qfilelist",    1+3, 1+3, 1, func_q_filelist),
  FT_ENTRY ("qcountfiles",  1+0, 1+1, 1, func_q_countfiles),
  FT_ENTRY ("qforeachfile", 1+3, 1+3, 0, func_q_foreachfile),
  FT_ENTRY ("qsortfiles",   1+0, 1+1, 1, func_q_sortfiles),
  FT_ENTRY ("qversortfiles",1+0, 1+1, 1, func_q_sortfiles),
# ifdef CONFIG_WITH_RSORT
  FT_ENTRY ("qrsortfiles",  1+0, 1+1, 1, func_q_sortfiles),
  FT_ENTRY ("qrversortfiles",1+0,1+1, 1, func_q_sortfiles),
# endif
  FT_ENTRY ("qabspath",     1+0, 1+1, 1, func_q_abspath),
  FT_ENTRY ("qaddprefix",   1+2, 1+2, 1, func_q_addsuffix_addprefix),
  FT_ENTRY ("qaddsuffix",   1+2, 1+2, 1, func_q_addsuffix_addprefix),
  FT_ENTRY ("qbasename",    1+0, 1+1, 1, func_q_basename_dir),
  FT_ENTRY ("qdir",         1+0, 1+1, 1, func_q_basename_dir),
  FT_ENTRY ("qnotdir",      1+0, 1+1, 1, func_q_notdir),
# ifdef CONFIG_WITH_ROOT_FUNC
  FT_ENTRY ("qroot",        1+0, 1+1, 1, func_q_root),
  FT_ENTRY ("qnotroot",     1+0, 1+1, 1, func_q_notroot),
# endif
  FT_ENTRY ("qsuffix",      1+0, 1+1, 1, func_q_suffix),
  FT_ENTRY ("qrealpath",    1+0, 1+1, 1, func_q_realpath),
# ifdef CONFIG_WITH_ABSPATHEX
  FT_ENTRY ("qabspathex",   1+0, 1+2, 1, func_q_abspathex),
# endif
  FT_ENTRY ("qwildcard",    1+0, 1+1, 1, func_q_wildcard),
  FT_ENTRY ("qone-unquoted",1+1, 1+1, 1, func_q_one_unquoted),
  FT_ENTRY ("qtarget",      1+1, 1+1, 1, func_q_one_unquoted),    /* For requoting plain $@ to given style. */
  FT_ENTRY ("qdeps",        1+1, 1+2, 1, func_q_deps),            /* $^ with quoting style */
  FT_ENTRY ("qdeps-all",    1+1, 1+2, 1, func_q_deps),            /* $+ with quoting style */
  FT_ENTRY ("qdeps-newer",  1+1, 1+2, 1, func_q_deps_newer),      /* $? with quoting style */
  FT_ENTRY ("qdeps-oo",     1+1, 1+2, 1, func_q_deps_order_only), /* $| with quoting style */
  FT_ENTRY ("qfilter",      1+2, 1+2, 1, func_q_filter_filterout),
  FT_ENTRY ("qfilter-out",  1+2, 1+2, 1, func_q_filter_filterout),
  /** @todo XXX: Add more qxxxx variants. */
#endif
};

#define FUNCTION_TABLE_ENTRIES (sizeof (function_table_init) / sizeof (struct function_table_entry))


/* These must come after the definition of function_table.  */

static char *
expand_builtin_function (char *o, int argc, char **argv,
                         const struct function_table_entry *entry_p)
{
  char *p;

  if (argc < (int)entry_p->minimum_args)
    fatal (*expanding_var, strlen (entry_p->name),
           _("insufficient number of arguments (%d) to function '%s'"),
           argc, entry_p->name);

  /* I suppose technically some function could do something with no arguments,
     but so far no internal ones do, so just test it for all functions here
     rather than in each one.  We can change it later if necessary.  */

  if (!argc && !entry_p->alloc_fn)
    return o;

  if (!entry_p->fptr.func_ptr)
    OS (fatal, *expanding_var,
        _("unimplemented on this platform: function '%s'"), entry_p->name);

  if (!entry_p->alloc_fn)
    return entry_p->fptr.func_ptr (o, argv, entry_p->name);

  /* This function allocates memory and returns it to us.
     Write it to the variable buffer, then free it.  */

  p = entry_p->fptr.alloc_func_ptr (entry_p->name, argc, argv);
  if (p)
    {
      o = variable_buffer_output (o, p, strlen (p));
      free (p);
    }

  return o;
}

/* Check for a function invocation in *STRINGP.  *STRINGP points at the
   opening ( or { and is not null-terminated.  If a function invocation
   is found, expand it into the buffer at *OP, updating *OP, incrementing
   *STRINGP past the reference and returning nonzero.  If not, return zero.  */

static int
handle_function2 (const struct function_table_entry *entry_p, char **op, const char **stringp) /* bird split it up. */
{
  char openparen = (*stringp)[0];
  char closeparen = openparen == '(' ? ')' : '}';
  const char *beg;
  const char *end;
  int count = 0;
  char *abeg = NULL;
  char **argv, **argvp;
  int nargs;

  beg = *stringp + 1;

  /* We found a builtin function.  Find the beginning of its arguments (skip
     whitespace after the name).  */

  beg += entry_p->len;
  NEXT_TOKEN (beg);

  /* Find the end of the function invocation, counting nested use of
     whichever kind of parens we use.  Since we're looking, count commas
     to get a rough estimate of how many arguments we might have.  The
     count might be high, but it'll never be low.  */

  for (nargs=1, end=beg; *end != '\0'; ++end)
    if (*end == ',')
      ++nargs;
    else if (*end == openparen)
      ++count;
    else if (*end == closeparen && --count < 0)
      break;

  if (count >= 0)
    fatal (*expanding_var, strlen (entry_p->name),
           _("unterminated call to function '%s': missing '%c'"),
           entry_p->name, closeparen);

  *stringp = end;

  /* Get some memory to store the arg pointers.  */
  argvp = argv = alloca (sizeof (char *) * (nargs + 2));

  /* Chop the string into arguments, then a nul.  As soon as we hit
     MAXIMUM_ARGS (if it's >0) assume the rest of the string is part of the
     last argument.

     If we're expanding, store pointers to the expansion of each one.  If
     not, make a duplicate of the string and point into that, nul-terminating
     each argument.  */

  if (entry_p->expand_args)
    {
      const char *p;
      for (p=beg, nargs=0; p <= end; ++argvp)
        {
          const char *next;

          ++nargs;

          if (nargs == entry_p->maximum_args
              || (! (next = find_next_argument (openparen, closeparen, p, end))))
            next = end;

          *argvp = expand_argument (p, next);
          p = next + 1;
        }
    }
  else
    {
      int len = end - beg;
      char *p, *aend;

      abeg = xmalloc (len+1);
      memcpy (abeg, beg, len);
      abeg[len] = '\0';
      aend = abeg + len;

      for (p=abeg, nargs=0; p <= aend; ++argvp)
        {
          char *next;

          ++nargs;

          if (nargs == entry_p->maximum_args
              || (! (next = find_next_argument (openparen, closeparen, p, aend))))
            next = aend;

          *argvp = p;
          *next = '\0';
          p = next + 1;
        }
    }
  *argvp = NULL;

  /* Finally!  Run the function...  */
  *op = expand_builtin_function (*op, nargs, argv, entry_p);

  /* Free memory.  */
  if (entry_p->expand_args)
    for (argvp=argv; *argvp != 0; ++argvp)
      free (*argvp);
  else
    free (abeg);

  return 1;
}


int  /* bird split it up and hacked it. */
#ifndef CONFIG_WITH_VALUE_LENGTH
handle_function (char **op, const char **stringp)
{
  const struct function_table_entry *entry_p = lookup_function (*stringp + 1);
  if (!entry_p)
    return 0;
  return handle_function2 (entry_p, op, stringp);
}
#else  /* CONFIG_WITH_VALUE_LENGTH */
handle_function (char **op, const char **stringp, const char *nameend, const char *eol UNUSED)
{
  const char *fname = *stringp + 1;
  const struct function_table_entry *entry_p =
      lookup_function_in_hash_tab (fname, nameend - fname);
  if (!entry_p)
    return 0;
  return handle_function2 (entry_p, op, stringp);
}
#endif /* CONFIG_WITH_VALUE_LENGTH */

#ifdef CONFIG_WITH_COMPILER
/* Used by the "compiler" to get all info about potential functions. */
make_function_ptr_t
lookup_function_for_compiler (const char *name, unsigned int len,
                              unsigned char *minargsp, unsigned char *maxargsp,
                              char *expargsp, const char **funcnamep)
{
  const struct function_table_entry *entry_p = lookup_function (name, len);
  if (!entry_p)
    return 0;
  *minargsp  = entry_p->minimum_args;
  *maxargsp  = entry_p->maximum_args;
  *expargsp  = entry_p->expand_args;
  *funcnamep = entry_p->name;
  return entry_p->func_ptr;
}
#endif /* CONFIG_WITH_COMPILER */


/* User-defined functions.  Expand the first argument as either a builtin
   function or a make variable, in the context of the rest of the arguments
   assigned to $1, $2, ... $N.  $0 is the name of the function.  */

static char *
func_call (char *o, char **argv, const char *funcname UNUSED)
{
  static int max_args = 0;
  char *fname;
  char *body;
  int flen;
  int i;
  int saved_args;
  const struct function_table_entry *entry_p;
  struct variable *v;
#ifdef CONFIG_WITH_EVALPLUS
  char *buf;
  unsigned int len;
#endif
#ifdef CONFIG_WITH_VALUE_LENGTH
  char *fname_end;
#endif
#if defined (CONFIG_WITH_EVALPLUS) || defined (CONFIG_WITH_VALUE_LENGTH)
  char num[11];
#endif

  /* Clean up the name of the variable to be invoked.  */
  fname = next_token (argv[0]);
#ifndef CONFIG_WITH_VALUE_LENGTH
  end_of_token (fname)[0] = '\0';
#else
  fname_end = end_of_token (fname);
  *fname_end = '\0';
#endif

  /* Calling nothing is a no-op */
#ifndef CONFIG_WITH_VALUE_LENGTH
  if (*fname == '\0')
#else
  if (fname == fname_end)
#endif
    return o;

  /* Are we invoking a builtin function?  */

#ifndef CONFIG_WITH_VALUE_LENGTH
  entry_p = lookup_function (fname);
#else
  entry_p = lookup_function (fname, fname_end - fname);
#endif
  if (entry_p)
    {
      /* How many arguments do we have?  */
      for (i=0; argv[i+1]; ++i)
        ;
      return expand_builtin_function (o, i, argv+1, entry_p);
    }

  /* Not a builtin, so the first argument is the name of a variable to be
     expanded and interpreted as a function.  Find it.  */
  flen = strlen (fname);

  v = lookup_variable (fname, flen);

  if (v == 0)
    warn_undefined (fname, flen);

  if (v == 0 || *v->value == '\0')
    return o;

  body = alloca (flen + 4);
  body[0] = '$';
  body[1] = '(';
  memcpy (body + 2, fname, flen);
  body[flen+2] = ')';
  body[flen+3] = '\0';

  /* Set up arguments $(1) .. $(N).  $(0) is the function name.  */

  push_new_variable_scope ();

  for (i=0; *argv; ++i, ++argv)
#ifdef CONFIG_WITH_VALUE_LENGTH
    define_variable (num, sprintf (num, "%d", i), *argv, o_automatic, 0);
#else
    {
      char num[11];

      sprintf (num, "%d", i);
      define_variable (num, strlen (num), *argv, o_automatic, 0);
    }
#endif

#ifdef CONFIG_WITH_EVALPLUS
  /* $(.ARGC) is the argument count. */

  len = sprintf (num, "%d", i - 1);
  define_variable_vl (".ARGC", sizeof (".ARGC") - 1, num, len,
                      1 /* dup val */, o_automatic, 0);
#endif

  /* If the number of arguments we have is < max_args, it means we're inside
     a recursive invocation of $(call ...).  Fill in the remaining arguments
     in the new scope with the empty value, to hide them from this
     invocation.  */

  for (; i < max_args; ++i)
#ifdef CONFIG_WITH_VALUE_LENGTH
    define_variable (num, sprintf (num, "%d", i), "", o_automatic, 0);
#else
    {
      char num[11];

      sprintf (num, "%d", i);
      define_variable (num, strlen (num), "", o_automatic, 0);
    }
#endif

  saved_args = max_args;
  max_args = i;

#ifdef CONFIG_WITH_EVALPLUS
  if (!strcmp (funcname, "call"))
    {
#endif
      /* Expand the body in the context of the arguments, adding the result to
         the variable buffer.  */

      v->exp_count = EXP_COUNT_MAX;
#ifndef CONFIG_WITH_VALUE_LENGTH
      o = variable_expand_string (o, body, flen+3);
      v->exp_count = 0;

      o += strlen (o);
#else  /* CONFIG_WITH_VALUE_LENGTH */
      variable_expand_string_2 (o, body, flen+3, &o);
      v->exp_count = 0;
#endif /* CONFIG_WITH_VALUE_LENGTH */
#ifdef CONFIG_WITH_EVALPLUS
    }
  else
    {
      const floc *reading_file_saved = reading_file;
      char *eos;

      if (!strcmp (funcname, "evalcall"))
        {
          /* Evaluate the variable value without expanding it. We
             need a copy since eval_buffer is destructive.  */

          size_t off = o - variable_buffer;
          eos = variable_buffer_output (o, v->value, v->value_length + 1) - 1;
          o = variable_buffer + off;
          if (v->fileinfo.filenm)
            reading_file = &v->fileinfo;
        }
      else
        {
          /* Expand the body first and then evaluate the output. */

          v->exp_count = EXP_COUNT_MAX;
          o = variable_expand_string_2 (o, body, flen+3, &eos);
          v->exp_count = 0;
        }

      install_variable_buffer (&buf, &len);
      eval_buffer (o, NULL, eos);
      restore_variable_buffer (buf, len);
      reading_file = reading_file_saved;

      /* Deal with the .RETURN value if present. */

      v = lookup_variable_in_set (".RETURN", sizeof (".RETURN") - 1,
                                  current_variable_set_list->set);
      if (v && v->value_length)
        {
          if (v->recursive && !IS_VARIABLE_RECURSIVE_WITHOUT_DOLLAR (v))
            {
              v->exp_count = EXP_COUNT_MAX;
              variable_expand_string_2 (o, v->value, v->value_length, &o);
              v->exp_count = 0;
            }
          else
            o = variable_buffer_output (o, v->value, v->value_length);
        }
    }
#endif /* CONFIG_WITH_EVALPLUS */

  max_args = saved_args;

  pop_variable_scope ();

  return o;
}

void
define_new_function (const floc *flocp, const char *name,
                     unsigned int min, unsigned int max, unsigned int flags,
                     gmk_func_ptr func)
{
  const char *e = name;
  struct function_table_entry *ent;
  size_t len;

  while (STOP_SET (*e, MAP_USERFUNC))
    e++;
  len = e - name;

  if (len == 0)
    O (fatal, flocp, _("Empty function name"));
  if (*name == '.' || *e != '\0')
    OS (fatal, flocp, _("Invalid function name: %s"), name);
  if (len > 255)
    OS (fatal, flocp, _("Function name too long: %s"), name);
  if (min > 255)
    ONS (fatal, flocp,
         _("Invalid minimum argument count (%u) for function %s"), min, name);
  if (max > 255 || (max && max < min))
    ONS (fatal, flocp,
         _("Invalid maximum argument count (%u) for function %s"), max, name);

  ent = xmalloc (sizeof (struct function_table_entry));
  ent->name = name;
  ent->len = len;
  ent->minimum_args = min;
  ent->maximum_args = max;
  ent->expand_args = ANY_SET(flags, GMK_FUNC_NOEXPAND) ? 0 : 1;
  ent->alloc_fn = 1;
  ent->fptr.alloc_func_ptr = func;

  hash_insert (&function_table, ent);
}

void
hash_init_function_table (void)
{
  hash_init (&function_table, FUNCTION_TABLE_ENTRIES * 2,
             function_table_entry_hash_1, function_table_entry_hash_2,
             function_table_entry_hash_cmp);
  hash_load (&function_table, function_table_init,
             FUNCTION_TABLE_ENTRIES, sizeof (struct function_table_entry));
#if defined (CONFIG_WITH_OPTIMIZATION_HACKS) || defined (CONFIG_WITH_VALUE_LENGTH)
  {
    unsigned int i;
    for (i = 0; i < FUNCTION_TABLE_ENTRIES; i++)
      {
        const char *fn = function_table_init[i].name;
        while (*fn)
          {
            func_char_map[(int)*fn] = 1;
            fn++;
          }
        assert (function_table_init[i].len <= MAX_FUNCTION_LENGTH);
        assert (function_table_init[i].len >= MIN_FUNCTION_LENGTH);
      }
  }
#endif
}
