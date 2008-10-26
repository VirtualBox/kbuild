/* Builtin function expansion for GNU Make.
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

#include "make.h"
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
#endif

#ifdef KMK_HELPERS
# include "kbuild.h"
#endif
#ifdef CONFIG_WITH_XARGS /* bird */
# ifdef HAVE_LIMITS_H
#  include <limits.h>
# endif
#endif
#include <assert.h> /* bird */

#if defined (CONFIG_WITH_MATH) || defined (CONFIG_WITH_NANOTS) || defined (CONFIG_WITH_FILE_SIZE) /* bird */
# include <ctype.h>
# ifdef _MSC_VER
typedef __int64 math_int;
# else
#  include <stdint.h>
typedef int64_t math_int;
# endif
static char *math_int_to_variable_buffer (char *, math_int);
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


struct function_table_entry
  {
    const char *name;
    unsigned char len;
    unsigned char minimum_args;
    unsigned char maximum_args;
    char expand_args;
    char *(*func_ptr) (char *output, char **argv, const char *fname);
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
unsigned long make_stats_allocated = 0;
unsigned long make_stats_allocated_sum = 0;
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
          && ((p > text && !isblank ((unsigned char)p[-1]))
              || (p[slen] != '\0' && !isblank ((unsigned char)p[slen]))))
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

__inline static const struct function_table_entry *
lookup_function_in_hash_tab (const char *s, unsigned char len)
{
    struct function_table_entry function_table_entry_key;
    function_table_entry_key.name = s;
    function_table_entry_key.len = len;

    return hash_find_item (&function_table, &function_table_entry_key);
}

/* Look up a function by name.  */

__inline static const struct function_table_entry *
lookup_function (const char *s, unsigned int len)
{
  unsigned char ch;
# if 0 /* insane loop unroll */

  if (len > MAX_FUNCTION_LENGTH)
      len = MAX_FUNCTION_LENGTH + 1;

#  define X(idx) \
        if (!func_char_map[ch = s[idx]]) \
          { \
            if (isblank (ch)) \
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
        if ((ch = s[12]) == '\0' || isblank (ch))
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
  if (ch == '\0' || isblank (ch))
    return lookup_function_in_hash_tab (s, e - s);
  return 0;
# endif /* normal loop */
}

#else  /* original code */
/* Look up a function by name.  */

static const struct function_table_entry *
lookup_function (const char *s)
{
  const char *e = s;
  while (*e && ( (*e >= 'a' && *e <= 'z') || *e == '-'))
    e++;
  if (*e == '\0' || isblank ((unsigned char) *e))
    {
      struct function_table_entry function_table_entry_key;
      function_table_entry_key.name = s;
      function_table_entry_key.len = e - s;

      return hash_find_item (&function_table, &function_table_entry_key);
    }
  return 0;
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

#ifndef CONFIG_WITH_ALLOC_CACHES
  chain = multi_glob (parse_file_seq
		      (&line, '\0', sizeof (struct nameseq),
		       /* We do not want parse_file_seq to strip `./'s.
			  That would break examples like:
			  $(patsubst ./%.c,obj/%.o,$(wildcard ./?*.c)).  */
		       0),
		      sizeof (struct nameseq));
#else
  chain = multi_glob (parse_file_seq
                      (&line, '\0', &nameseq_cache,
                       /* We do not want parse_file_seq to strip `./'s.
                          That would break examples like:
                          $(patsubst ./%.c,obj/%.o,$(wildcard ./?*.c)).  */
                       0),
                      &nameseq_cache);
#endif

  if (result == 0)
    {
      length = 100;
      result = xmalloc (100);
    }

  idx = 0;
  while (chain != 0)
    {
      const char *name = chain->name;
      unsigned int len = strlen (name);

      struct nameseq *next = chain->next;
#ifndef CONFIG_WITH_ALLOC_CACHES
      free (chain);
#else
      alloccache_free (&nameseq_cache, chain);
#endif
      chain = next;

      /* multi_glob will pass names without globbing metacharacters
	 through as is, but we want only files that actually exist.  */
      if (file_exists_p (name))
	{
	  if (idx + len + 1 > length)
	    {
	      length += (len + 1) * 2;
	      result = xrealloc (result, length);
	    }
	  memcpy (&result[idx], name, len);
	  idx += len;
	  result[idx++] = ' ';
	}
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

#ifdef VMS
# define IS_PATHSEP(c) ((c) == ']')
#else
# ifdef HAVE_DOS_PATHS
#  define IS_PATHSEP(c) ((c) == '/' || (c) == '\\')
# else
#  define IS_PATHSEP(c) ((c) == '/')
# endif
#endif


static char *
func_notdir_suffix (char *o, char **argv, const char *funcname)
{
  /* Expand the argument.  */
  const char *list_iterator = argv[0];
  const char *p2;
  int doneany =0;
  unsigned int len=0;

  int is_suffix = streq (funcname, "suffix");
  int is_notdir = !is_suffix;
  while ((p2 = find_next_token (&list_iterator, &len)) != 0)
    {
      const char *p = p2 + len;


      while (p >= p2 && (!is_suffix || *p != '.'))
	{
	  if (IS_PATHSEP (*p))
	    break;
	  --p;
	}

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
      else if (streq (funcname, "notdir") && p2[0] && p2[1] == ':')
	{
	  p = p2 + 2;
	  o = variable_buffer_output (o, p, len - (p - p2));
	}
#endif
      else if (is_notdir)
	o = variable_buffer_output (o, p2, len);

      if (is_notdir || p >= p2)
	{
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
  int doneany=0;
  unsigned int len=0;

  int is_basename= streq (funcname, "basename");
  int is_dir= !is_basename;

  while ((p2 = find_next_token (&p3, &len)) != 0)
    {
      const char *p = p2 + len;
      while (p >= p2 && (!is_basename  || *p != '.'))
        {
          if (IS_PATHSEP (*p))
            break;
          --p;
        }

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
        o = variable_buffer_output (o, "[]", 2);
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

      o = variable_buffer_output (o, " ", 1);
      doneany = 1;
    }

  if (doneany)
    /* Kill last space.  */
    --o;

  return o;
}

static char *
func_addsuffix_addprefix (char *o, char **argv, const char *funcname)
{
  int fixlen = strlen (argv[0]);
  const char *list_iterator = argv[1];
  int is_addprefix = streq (funcname, "addprefix");
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

  while (find_next_token (&word_iterator, (unsigned int *) 0) != 0)
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
  while (*begpp <= *endpp && isspace ((unsigned char)**begpp))
    (*begpp) ++;
  while (*endpp >= *begpp && isspace ((unsigned char)**endpp))
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
    if (!ISDIGIT (*s))  /* ISDIGIT only evals its arg once: see make.h.  */
      break;

  if (s <= end || end - beg < 0)
    fatal (*expanding_var, "%s: '%s'", msg, beg);
}



static char *
func_word (char *o, char **argv, const char *funcname UNUSED)
{
  const char *end_p;
  const char *p;
  int i;

  /* Check the first argument.  */
  check_numeric (argv[0], _("non-numeric first argument to `word' function"));
  i = atoi (argv[0]);

  if (i == 0)
    fatal (*expanding_var,
           _("first argument to `word' function must be greater than 0"));

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
		 _("non-numeric first argument to `wordlist' function"));
  check_numeric (argv[1],
		 _("non-numeric second argument to `wordlist' function"));

  start = atoi (argv[0]);
  if (start < 1)
    fatal (*expanding_var,
           "invalid first argument to `wordlist' function: `%d'", start);

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

  push_new_variable_scope ();
  var = define_variable (varname, strlen (varname), "", o_automatic, 0);

  /* loop through LIST,  put the value in VAR and expand BODY */
  while ((p = find_next_token (&list_iterator, &len)) != 0)
    {
#ifndef CONFIG_WITH_VALUE_LENGTH
      char *result = 0;

      free (var->value);
      var->value = savestring (p, len);
      result = allocated_variable_expand (body);

      o = variable_buffer_output (o, result, strlen (result));
      o = variable_buffer_output (o, " ", 1);
      doneany = 1;
      free (result);
#else  /* CONFIG_WITH_VALUE_LENGTH */
      if (len >= (unsigned int)var->value_alloc_len)
        {
# ifdef CONFIG_WITH_RDONLY_VARIABLE_VALUE
          if (var->rdonly_val)
            var->rdonly_val = 0;
          else
# endif
            free (var->value);
          var->value_alloc_len = (len + 32) & ~31;
          var->value = xmalloc (var->value_alloc_len);
        }
      memcpy (var->value, p, len);
      var->value[len] = '\0';
      var->value_length = len;

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
  int save_c;
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
  int is_filter = streq (funcname, "filter");
  const char *pat_iterator = argv[0];
  const char *word_iterator = argv[1];
  int literals = 0;
  int words = 0;
  int hashing = 0;
  char *p;
  unsigned int len;

  /* Chop ARGV[0] up into patterns to match against the words.  */

  pattail = &pathead;
  while ((p = find_next_token (&pat_iterator, &len)) != 0)
    {
      struct a_pattern *pat = alloca (sizeof (struct a_pattern));

      *pattail = pat;
      pattail = &pat->next;

      if (*pat_iterator != '\0')
	++pat_iterator;

      pat->str = p;
      pat->length = len;
      pat->save_c = p[len];
      p[len] = '\0';
      pat->percent = find_percent (p);
      if (pat->percent == 0)
	literals++;
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
	      wp->matched |= pattern_matches (pp->str, pp->percent, wp->str);
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

  for (pp = pathead; pp != 0; pp = pp->next)
    pp->str[pp->length] = pp->save_c;

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

      while (isspace ((unsigned char)*p))
	++p;
      word_start = p;
      for (i=0; *p != '\0' && !isspace ((unsigned char)*p); ++p, ++i)
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

  switch (*funcname) {
    case 'e':
      fatal (reading_file, "%s", msg);

    case 'w':
      error (reading_file, "%s", msg);
      break;

    case 'i':
      printf ("%s\n", msg);
      fflush(stdout);
      break;

    default:
      fatal (*expanding_var, "Internal error: func_error: '%s'", funcname);
  }

  /* The warning function expands to the empty string.  */
  return o;
}


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
  int i;

  /* Find the maximum number of words we'll have.  */
  t = argv[0];
  wordi = 1;
  while (*t != '\0')
    {
      char c = *(t++);

      if (! isspace ((unsigned char)c))
        continue;

      ++wordi;

      while (isspace ((unsigned char)*t))
        ++t;
    }

  words = xmalloc (wordi * sizeof (char *));

  /* Now assign pointers to each string in the array.  */
  t = argv[0];
  wordi = 0;
  while ((p = find_next_token (&t, &len)) != 0)
    {
      ++t;
      p[len] = '\0';
      words[wordi++] = p;
    }

  if (wordi)
    {
      /* Now sort the list of words.  */
      qsort (words, wordi, sizeof (char *), alpha_compare);

      /* Now write the sorted list, uniquified.  */
#ifdef CONFIG_WITH_RSORT
      if (strcmp (funcname, "rsort"))
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
  int result;

  while (1)
    {
      const char *begp = *argv;
      const char *endp = begp + strlen (*argv) - 1;

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
  eval_buffer (argv[0]);
#else
  eval_buffer (argv[0], strchr (argv[0], '\0'));
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

  eval_buffer (argv[0], strchr (argv[0], '\0'));

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
      const struct floc *reading_file_saved = reading_file;

      /* Make a copy of the value to the variable buffer since
         eval_buffer will make changes to its input. */

      off = o - variable_buffer;
      variable_buffer_output (o, v->value, v->value_length + 1);
      o = variable_buffer + off;

      /* Eval the value.  Pop the current variable buffer setting so that the
         eval'd code can use its own without conflicting. (really necessary?)  */

      install_variable_buffer (&buf, &len);
      var_ctx = !strcmp (funcname, "evalvalctx");
      if (var_ctx)
        push_new_variable_scope ();
      if (v->fileinfo.filenm)
        reading_file = &v->fileinfo;

      assert (!o[v->value_length]);
      eval_buffer (o, o + v->value_length);

      reading_file = reading_file_saved;
      if (var_ctx)
        pop_variable_scope ();
      restore_variable_buffer (buf, len);
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
#ifdef CONFIG_WITH_RDONLY_VARIABLE_VALUE
      if (v && !v->origin != o_automatic && !v->rdonly_val)
#else
      if (v && !v->origin != o_automatic)
#endif
        {
          char *eos, *src;

          eos = collapse_continuations (v->value, v->value_length);
          v->value_length = eos - v->value;

          /* remove comments */

          src = memchr (v->value, '#', v->value_length);
          if (src)
            {
              unsigned char ch;
              char *dst = src;
              do
                {
                  /* drop blanks preceeding the comment */
                  while (dst > v->value)
                    {
                      ch = (unsigned char)dst[-1];
                      if (!isblank (ch))
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
        }
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
    o = variable_buffer_output (o, v->value,
                                v->value_length >= 0
                                ? (unsigned int)v->value_length /* FIXME */
                                : strlen(v->value));
#else
    o = variable_buffer_output (o, v->value, strlen(v->value));
#endif

  return o;
}

/*
  \r  is replaced on UNIX as well. Is this desirable?
 */
static void
fold_newlines (char *buffer, unsigned int *length)
{
  char *dst = buffer;
  char *src = buffer;
  char *last_nonnl = buffer -1;
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
  *(++last_nonnl) = '\0';
  *length = last_nonnl - buffer;
}



int shell_function_pid = 0, shell_function_completed;


#ifdef WINDOWS32
/*untested*/

#include <windows.h>
#include <io.h>
#include "sub_proc.h"


void
windows32_openpipe (int *pipedes, int *pid_p, char **command_argv, char **envp)
{
  SECURITY_ATTRIBUTES saAttr;
  HANDLE hIn;
  HANDLE hErr;
  HANDLE hChildOutRd;
  HANDLE hChildOutWr;
  HANDLE hProcess;


  saAttr.nLength = sizeof (SECURITY_ATTRIBUTES);
  saAttr.bInheritHandle = TRUE;
  saAttr.lpSecurityDescriptor = NULL;

  if (DuplicateHandle (GetCurrentProcess(),
		      GetStdHandle(STD_INPUT_HANDLE),
		      GetCurrentProcess(),
		      &hIn,
		      0,
		      TRUE,
		      DUPLICATE_SAME_ACCESS) == FALSE) {
    fatal (NILF, _("create_child_process: DuplicateHandle(In) failed (e=%ld)\n"),
	   GetLastError());

  }
  if (DuplicateHandle(GetCurrentProcess(),
		      GetStdHandle(STD_ERROR_HANDLE),
		      GetCurrentProcess(),
		      &hErr,
		      0,
		      TRUE,
		      DUPLICATE_SAME_ACCESS) == FALSE) {
    fatal (NILF, _("create_child_process: DuplicateHandle(Err) failed (e=%ld)\n"),
	   GetLastError());
  }

  if (!CreatePipe(&hChildOutRd, &hChildOutWr, &saAttr, 0))
    fatal (NILF, _("CreatePipe() failed (e=%ld)\n"), GetLastError());

  hProcess = process_init_fd(hIn, hChildOutWr, hErr);

  if (!hProcess)
    fatal (NILF, _("windows32_openpipe (): process_init_fd() failed\n"));

  /* make sure that CreateProcess() has Path it needs */
  sync_Path_environment();

  if (!process_begin(hProcess, command_argv, envp, command_argv[0], NULL)) {
    /* register process for wait */
    process_register(hProcess);

    /* set the pid for returning to caller */
    *pid_p = (int) hProcess;

  /* set up to read data from child */
  pipedes[0] = _open_osfhandle((long) hChildOutRd, O_RDONLY);

  /* this will be closed almost right away */
  pipedes[1] = _open_osfhandle((long) hChildOutWr, O_APPEND);
  } else {
    /* reap/cleanup the failed process */
	process_cleanup(hProcess);

    /* close handles which were duplicated, they weren't used */
	CloseHandle(hIn);
	CloseHandle(hErr);

	/* close pipe handles, they won't be used */
	CloseHandle(hChildOutRd);
	CloseHandle(hChildOutWr);

    /* set status for return */
    pipedes[0] = pipedes[1] = -1;
    *pid_p = -1;
  }
}
#endif


#ifdef __MSDOS__
FILE *
msdos_openpipe (int* pipedes, int *pidp, char *text)
{
  FILE *fpipe=0;
  /* MSDOS can't fork, but it has `popen'.  */
  struct variable *sh = lookup_variable ("SHELL", 5);
  int e;
  extern int dos_command_running, dos_status;

  /* Make sure not to bother processing an empty line.  */
  while (isblank ((unsigned char)*text))
    ++text;
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
      shell_function_completed = -1;
    }
  else
    {
      pipedes[0] = fileno (fpipe);
      *pidp = 42; /* Yes, the Meaning of Life, the Universe, and Everything! */
      errno = e;
      shell_function_completed = 1;
    }
  return fpipe;
}
#endif

/*
  Do shell spawning, with the naughty bits for different OSes.
 */

#ifdef VMS

/* VMS can't do $(shell ...)  */
#define func_shell 0

#else
#ifndef _AMIGA
static char *
func_shell (char *o, char **argv, const char *funcname UNUSED)
{
  char *batch_filename = NULL;

#ifdef __MSDOS__
  FILE *fpipe;
#endif
  char **command_argv;
  const char *error_prefix;
  char **envp;
  int pipedes[2];
  int pid;

#ifndef __MSDOS__
  /* Construct the argument list.  */
  command_argv = construct_command_argv (argv[0], NULL, NULL, &batch_filename);
  if (command_argv == 0)
    return o;
#endif

  /* Using a target environment for `shell' loses in cases like:
     export var = $(shell echo foobie)
     because target_environment hits a loop trying to expand $(var)
     to put it in the environment.  This is even more confusing when
     var was not explicitly exported, but just appeared in the
     calling environment.

     See Savannah bug #10593.

  envp = target_environment (NILF);
  */

  envp = environ;

  /* For error messages.  */
  if (reading_file && reading_file->filenm)
    {
      char *p = alloca (strlen (reading_file->filenm)+11+4);
      sprintf (p, "%s:%lu: ", reading_file->filenm, reading_file->lineno);
      error_prefix = p;
    }
  else
    error_prefix = "";

#if defined(__MSDOS__)
  fpipe = msdos_openpipe (pipedes, &pid, argv[0]);
  if (pipedes[0] < 0)
    {
      perror_with_name (error_prefix, "pipe");
      return o;
    }
#elif defined(WINDOWS32)
  windows32_openpipe (pipedes, &pid, command_argv, envp);
  if (pipedes[0] < 0)
    {
      /* open of the pipe failed, mark as failed execution */
      shell_function_completed = -1;

      return o;
    }
  else
#else
  if (pipe (pipedes) < 0)
    {
      perror_with_name (error_prefix, "pipe");
      return o;
    }

# ifdef __EMX__
  /* close some handles that are unnecessary for the child process */
  CLOSE_ON_EXEC(pipedes[1]);
  CLOSE_ON_EXEC(pipedes[0]);
  /* Never use fork()/exec() here! Use spawn() instead in exec_command() */
  pid = child_execute_job (0, pipedes[1], command_argv, envp);
  if (pid < 0)
    perror_with_name (error_prefix, "spawn");
# else /* ! __EMX__ */
  pid = vfork ();
  if (pid < 0)
    perror_with_name (error_prefix, "fork");
  else if (pid == 0)
    child_execute_job (0, pipedes[1], command_argv, envp);
  else
# endif
#endif
    {
      /* We are the parent.  */
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

      /* Close the write side of the pipe.  */
# ifdef _MSC_VER /* Avoid annoying msvcrt when debugging. (bird) */
      if (pipedes[1] != -1)
# endif
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
	(void) pclose (fpipe);
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

      if (batch_filename) {
	DB (DB_VERBOSE, (_("Cleaning up temporary batch file %s\n"),
                       batch_filename));
	remove (batch_filename);
	free (batch_filename);
      }
      shell_function_pid = 0;

      /* The child_handler function will set shell_function_completed
	 to 1 when the child dies normally, or to -1 if it
	 dies with status 127, which is most likely an exec fail.  */

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
	  fold_newlines (buffer, &i);
	  o = variable_buffer_output (o, buffer, i);
	}

      free (buffer);
    }

  return o;
}

#else	/* _AMIGA */

/* Do the Amiga version of func_shell.  */

static char *
func_shell (char *o, char **argv, const char *funcname)
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
  command_argv = construct_command_argv (argv[0], (char **) NULL,
                                         (struct file *) 0, &batch_filename);
  if (command_argv == 0)
    return o;

  /* Note the mktemp() is a security hole, but this only runs on Amiga.
     Ideally we would use main.c:open_tmpfile(), but this uses a special
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

  fold_newlines (buffer, &i);
  o = variable_buffer_output (o, buffer, i);
  free (buffer);
  return o;
}
#endif  /* _AMIGA */
#endif  /* !VMS */

#ifdef EXPERIMENTAL

/*
  equality. Return is string-boolean, ie, the empty string is false.
 */
static char *
func_eq (char *o, char **argv, const char *funcname)
{
  int result = ! strcmp (argv[0], argv[1]);
  o = variable_buffer_output (o,  result ? "1" : "", result);
  return o;
}


/*
  string-boolean not operator.
 */
static char *
func_not (char *o, char **argv, const char *funcname)
{
  const char *s = argv[0];
  int result = 0;
  while (isspace ((unsigned char)*s))
    s++;
  result = ! (*s);
  o = variable_buffer_output (o,  result ? "1" : "", result);
  return o;
}
#endif

#ifdef CONFIG_WITH_LAZY_DEPS_VARS

/* This is also in file.c (bad).  */
# if VMS
#  define FILE_LIST_SEPARATOR ','
# else
#  define FILE_LIST_SEPARATOR ' '
# endif

/* Implements $^ and $+.

   The first is somes with with FUNCNAME 'dep', the second as 'dep-all'.

   If no second argument is given, or if it's empty, or if it's zero,
   all dependencies will be returned.  If the second argument is non-zero
   the dependency at that position will be returned.  If the argument is
   negative a fatal error is thrown.  */
static char *
func_deps (char *o, char **argv, const char *funcname)
{
  unsigned int idx = 0;
  struct file *file;

  /* Handle the argument if present. */

  if (argv[1])
    {
      char *p = argv[1];
      while (isspace ((unsigned int)*p))
        p++;
      if (*p != '\0')
        {
          char *n;
          long l = strtol (p, &n, 0);
          while (isspace ((unsigned int)*n))
            n++;
          idx = l;
          if (*n != '\0' || l < 0 || (long)idx != l)
            fatal (NILF, _("%s: invalid index value: `%s'\n"), funcname, p);
        }
    }

  /* Find the file and select the list corresponding to FUNCNAME. */

  file = lookup_file (argv[0]);
  if (file)
    {
      struct dep *deps = funcname[4] != '\0' && file->org_deps
                       ? file->org_deps : file->deps;
      struct dep *d;

      if (   file->double_colon
          && (   file->double_colon != file
              || file->last != file))
          error (NILF, _("$(%s ) cannot be used on files with multiple double colon rules like `%s'\n"),
                 funcname, file->name);

      if (idx == 0 /* all */)
        {
          unsigned int total_len = 0;

          /* calc the result length. */

          for (d = deps; d; d = d->next)
            if (!d->ignore_mtime)
              {
                const char *c = dep_name (d);

#ifndef	NO_ARCHIVES
                if (ar_name (c))
                  {
                    c = strchr (c, '(') + 1;
                    total_len += strlen (c);
                  }
                else
#else
                  total_len += strcache2_get_len (&file_strcache, c) + 1;
#endif
              }

          if (total_len)
            {
              /* prepare the variable buffer dude wrt to the output size and
                 pass along the strings.  */

              o = variable_buffer_output (o + total_len, "", 0) - total_len; /* a hack */

              for (d = deps; d; d = d->next)
                if (!d->ignore_mtime)
                  {
                    unsigned int len;
                    const char *c = dep_name (d);

#ifndef	NO_ARCHIVES
                    if (ar_name (c))
                      {
                        c = strchr (c, '(') + 1;
                        len += strlen (c);
                      }
                    else
#else
                      len = strcache2_get_len (&file_strcache, c) + 1;
#endif
                    o = variable_buffer_output (o, c, len);
                    o[-1] = FILE_LIST_SEPARATOR;
                  }

                --o;        /* nuke the last list separator */
                *o = '\0';
            }
        }
      else
        {
          /* Dependency given by index.  */

          for (d = deps; d; d = d->next)
            if (!d->ignore_mtime)
              {
                if (--idx == 0) /* 1 based indexing */
                  {
                    unsigned int len;
                    const char *c = dep_name (d);

#ifndef	NO_ARCHIVES
                    if (ar_name (c))
                      {
                        c = strchr (c, '(') + 1;
                        len += strlen (c) - ;
                      }
                    else
#else
                        len = strcache2_get_len (&file_strcache, c);
#endif
                    o = variable_buffer_output (o, c, len);
                    break;
                  }
              }
        }
    }

  return o;
}

/* Implements $?.

   If no second argument is given, or if it's empty, or if it's zero,
   all dependencies will be returned.  If the second argument is non-zero
   the dependency at that position will be returned.  If the argument is
   negative a fatal error is thrown.  */
static char *
func_deps_newer (char *o, char **argv, const char *funcname)
{
  unsigned int idx = 0;
  struct file *file;

  /* Handle the argument if present. */

  if (argv[1])
    {
      char *p = argv[1];
      while (isspace ((unsigned int)*p))
        p++;
      if (*p != '\0')
        {
          char *n;
          long l = strtol (p, &n, 0);
          while (isspace ((unsigned int)*n))
            n++;
          idx = l;
          if (*n != '\0' || l < 0 || (long)idx != l)
            fatal (NILF, _("%s: invalid index value: `%s'\n"), funcname, p);
        }
    }

  /* Find the file. */

  file = lookup_file (argv[0]);
  if (file)
    {
      struct dep *deps = file->deps;
      struct dep *d;

      if (   file->double_colon
          && (   file->double_colon != file
              || file->last != file))
          error (NILF, _("$(%s ) cannot be used on files with multiple double colon rules like `%s'\n"),
                 funcname, file->name);

      if (idx == 0 /* all */)
        {
          unsigned int total_len = 0;

          /* calc the result length. */

          for (d = deps; d; d = d->next)
            if (!d->ignore_mtime && d->changed)
              {
                const char *c = dep_name (d);

#ifndef	NO_ARCHIVES
                if (ar_name (c))
                  {
                    c = strchr (c, '(') + 1;
                    total_len += strlen (c);
                  }
                else
#else
                  total_len += strcache2_get_len (&file_strcache, c) + 1;
#endif
              }

          if (total_len)
            {
              /* prepare the variable buffer dude wrt to the output size and
                 pass along the strings.  */

              o = variable_buffer_output (o + total_len, "", 0) - total_len; /* a hack */

              for (d = deps; d; d = d->next)
                if (!d->ignore_mtime && d->changed)
                  {
                    unsigned int len;
                    const char *c = dep_name (d);

#ifndef	NO_ARCHIVES
                    if (ar_name (c))
                      {
                        c = strchr (c, '(') + 1;
                        len += strlen (c);
                      }
                    else
#else
                      len = strcache2_get_len (&file_strcache, c) + 1;
#endif
                    o = variable_buffer_output (o, c, len);
                    o[-1] = FILE_LIST_SEPARATOR;
                  }

                --o;        /* nuke the last list separator */
                *o = '\0';
            }
        }
      else
        {
          /* Dependency given by index.  */

          for (d = deps; d; d = d->next)
            if (!d->ignore_mtime && d->changed)
              {
                if (--idx == 0) /* 1 based indexing */
                  {
                    unsigned int len;
                    const char *c = dep_name (d);

#ifndef	NO_ARCHIVES
                    if (ar_name (c))
                      {
                        c = strchr (c, '(') + 1;
                        len += strlen (c) - ;
                      }
                    else
#else
                        len = strcache2_get_len (&file_strcache, c);
#endif
                    o = variable_buffer_output (o, c, len);
                    break;
                  }
              }
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
func_deps_order_only (char *o, char **argv, const char *funcname)
{
  unsigned int idx = 0;
  struct file *file;

  /* Handle the argument if present. */

  if (argv[1])
    {
      char *p = argv[1];
      while (isspace ((unsigned int)*p))
        p++;
      if (*p != '\0')
        {
          char *n;
          long l = strtol (p, &n, 0);
          while (isspace ((unsigned int)*n))
            n++;
          idx = l;
          if (*n != '\0' || l < 0 || (long)idx != l)
            fatal (NILF, _("%s: invalid index value: `%s'\n"), funcname, p);
        }
    }

  /* Find the file. */

  file = lookup_file (argv[0]);
  if (file)
    {
      struct dep *deps = file->deps;
      struct dep *d;

      if (   file->double_colon
          && (   file->double_colon != file
              || file->last != file))
          error (NILF, _("$(%s ) cannot be used on files with multiple double colon rules like `%s'\n"),
                 funcname, file->name);

      if (idx == 0 /* all */)
        {
          unsigned int total_len = 0;

          /* calc the result length. */

          for (d = deps; d; d = d->next)
            if (d->ignore_mtime)
              {
                const char *c = dep_name (d);

#ifndef	NO_ARCHIVES
                if (ar_name (c))
                  {
                    c = strchr (c, '(') + 1;
                    total_len += strlen (c);
                  }
                else
#else
                  total_len += strcache2_get_len (&file_strcache, c) + 1;
#endif
              }

          if (total_len)
            {
              /* prepare the variable buffer dude wrt to the output size and
                 pass along the strings.  */

              o = variable_buffer_output (o + total_len, "", 0) - total_len; /* a hack */

              for (d = deps; d; d = d->next)
                if (d->ignore_mtime)
                  {
                    unsigned int len;
                    const char *c = dep_name (d);

#ifndef	NO_ARCHIVES
                    if (ar_name (c))
                      {
                        c = strchr (c, '(') + 1;
                        len += strlen (c);
                      }
                    else
#else
                      len = strcache2_get_len (&file_strcache, c) + 1;
#endif
                    o = variable_buffer_output (o, c, len);
                    o[-1] = FILE_LIST_SEPARATOR;
                  }

                --o;        /* nuke the last list separator */
                *o = '\0';
            }
        }
      else
        {
          /* Dependency given by index.  */

          for (d = deps; d; d = d->next)
            if (d->ignore_mtime)
              {
                if (--idx == 0) /* 1 based indexing */
                  {
                    unsigned int len;
                    const char *c = dep_name (d);

#ifndef	NO_ARCHIVES
                    if (ar_name (c))
                      {
                        c = strchr (c, '(') + 1;
                        len += strlen (c) - ;
                      }
                    else
#else
                        len = strcache2_get_len (&file_strcache, c);
#endif
                    o = variable_buffer_output (o, c, len);
                    break;
                  }
              }
        }
    }

  return o;
}
#endif /* CONFIG_WITH_LAZY_DEPS_VARS */


#ifdef CONFIG_WITH_DEFINED
/* Similar to ifdef. */
static char *
func_defined (char *o, char **argv, const char *funcname)
{
  struct variable *v = lookup_variable (argv[0], strlen (argv[0]));
  int result = v != NULL && *v->value != '\0';
  o = variable_buffer_output (o,  result ? "1" : "", result);
  return o;
}
#endif /* CONFIG_WITH_DEFINED*/


/* Return the absolute name of file NAME which does not contain any `.',
   `..' components nor any repeated path separators ('/').   */
#ifdef KMK
char *
#else
static char *
#endif
abspath (const char *name, char *apath)
{
  char *dest;
  const char *start, *end, *apath_limit;

  if (name[0] == '\0' || apath == NULL)
    return NULL;

#ifdef WINDOWS32                                                    /* bird */
  dest = w32ify((char *)name, 1);
  if (!dest)
    return NULL;
  {
  size_t len = strlen(dest);
  memcpy(apath, dest, len);
  dest = apath + len;
  }

  (void)end; (void)start; (void)apath_limit;

#elif defined __OS2__                                               /* bird */
  if (_abspath(apath, name, GET_PATH_MAX))
    return NULL;
  dest = strchr(apath, '\0');

  (void)end; (void)start; (void)apath_limit; (void)dest;

#else /* !WINDOWS32 && !__OS2__ */
  apath_limit = apath + GET_PATH_MAX;

#ifdef HAVE_DOS_PATHS /* bird added this */
  if (isalpha(name[0]) && name[1] == ':')
    {
      /* drive spec */
      apath[0] = toupper(name[0]);
      apath[1] = ':';
      apath[2] = '/';
      name += 2;
    }
  else
#endif /* HAVE_DOS_PATHS */
  if (name[0] != '/')
    {
      /* It is unlikely we would make it until here but just to make sure. */
      if (!starting_directory)
	return NULL;

      strcpy (apath, starting_directory);

      dest = strchr (apath, '\0');
    }
  else
    {
      apath[0] = '/';
      dest = apath + 1;
    }

  for (start = end = name; *start != '\0'; start = end)
    {
      unsigned long len;

      /* Skip sequence of multiple path-separators.  */
      while (*start == '/')
	++start;

      /* Find end of path component.  */
      for (end = start; *end != '\0' && *end != '/'; ++end)
        ;

      len = end - start;

      if (len == 0)
	break;
      else if (len == 1 && start[0] == '.')
	/* nothing */;
      else if (len == 2 && start[0] == '.' && start[1] == '.')
	{
	  /* Back up to previous component, ignore if at root already.  */
	  if (dest > apath + 1)
	    while ((--dest)[-1] != '/');
	}
      else
	{
	  if (dest[-1] != '/')
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
#ifdef HAVE_DOS_PATHS /* bird (is this correct? what about UNC?) */
  if (dest > apath + 1 + (apath[0] != '/') && dest[-1] == '/')
#else
  if (dest > apath + 1 && dest[-1] == '/')
#endif
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
  PATH_VAR (in);
  PATH_VAR (out);

  while ((path = find_next_token (&p, &len)) != 0)
    {
      if (len < GET_PATH_MAX)
        {
          strncpy (in, path, len);
          in[len] = '\0';

          if (
#ifdef HAVE_REALPATH
              realpath (in, out)
#else
              abspath (in, out)
#endif
             )
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

static char *
func_abspath (char *o, char **argv, const char *funcname UNUSED)
{
  /* Expand the argument.  */
  const char *p = argv[0];
  const char *path = 0;
  int doneany = 0;
  unsigned int len = 0;
  PATH_VAR (in);
  PATH_VAR (out);

  while ((path = find_next_token (&p, &len)) != 0)
    {
      if (len < GET_PATH_MAX)
        {
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
  while (isblank(*cwd))
    cwd++;
  if (!*cwd)
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
   lenght restriction of the host OS.

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
    fatal (NILF, _("Too many arguments for $(xargs)!\n"));

  /* first: the initial / default command.*/
  initial_cmd = argv[0];
  while (isspace ((unsigned char)*initial_cmd))
    initial_cmd++;
  max_args = initial_cmd_len = strlen (initial_cmd);

  /* second: the command for the subsequent command lines. defaults to the initial cmd. */
  subsequent_cmd = argc > 2 && argv[1][0] != '\0' ? argv[1] : "";
  while (isspace ((unsigned char)*subsequent_cmd))
    subsequent_cmd++;
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
  final_cmd = argc > 3 && argv[2][0] != '\0' ? argv[2] : "";
  while (isspace ((unsigned char)*final_cmd))
    final_cmd++;
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
    fatal (NILF, _("$(xargs): the commands are longer than the max exec argument length. (%lu <= %lu)\n"),
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
        fatal (NILF, _("$(xargs): command + one single arg is too much. giving up.\n"));

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
      while (tmp > args && isspace ((unsigned char)tmp[-1])) /* drop trailing spaces. */
        tmp--;
      o = variable_buffer_output (o, (char *)args, tmp - args);


      /* next */
      if (!cur)
        break;
      args = end;
      while (isspace ((unsigned char)*args))
        args++;
    }

  return o;
}
#endif

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
        if (!isblank (ch)
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
                while (s1 < e1 && isblank (*s1))
                  s1++;
                while (s2 < e2 && isblank (*s2))
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
            while (s1 < e1 && (isblank (*s1) || *s1 == '\n'))
              if (*s1++ == '\n')
                s1 = comp_cmds_strip_leading (s1, e1);
            while (s2 < e2 && (isblank (*s2) || *s2 == '\n'))
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
  if (!var1->recursive && !var2->recursive)
  {
    if (    var1->value_length == var2->value_length
        &&  !memcmp (var1->value, var2->value, var1->value_length))
      return variable_buffer_output (o, "", 0);     /* eq */

    /* ignore trailing and leading blanks */
    s1 = var1->value;
    e1 = s1 + var1->value_length;
    while (isblank ((unsigned char) *s1))
      s1++;
    while (e1 > s1 && isblank ((unsigned char) e1[-1]))
      e1--;

    s2 = var2->value;
    e2 = s2 + var2->value_length;
    while (isblank ((unsigned char) *s2))
      s2++;
    while (e2 > s2 && isblank ((unsigned char) e2[-1]))
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
  while (isblank ((unsigned char) *s1))
    s1++;
  while (e1 > s1 && isblank ((unsigned char) e1[-1]))
    e1--;

  s2 = var2->value;
  e2 = s2 + var2->value_length;
  while (isblank((unsigned char)*s2))
    s2++;
  while (e2 > s2 && isblank ((unsigned char) e2[-1]))
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
        while (isblank ((unsigned char) *s1))
          s1++;
      e1 = strchr (s1, '\0');
      while (e1 > s1 && isblank ((unsigned char) e1[-1]))
        e1--;
    }

  if (!x2)
    s2 += l;
  else
    {
      s2 = a2 = allocated_variable_expand ((char *)s2 + l);
      if (!l)
        while (isblank ((unsigned char) *s2))
          s2++;
      e2 = strchr (s2, '\0');
      while (e2 > s2 && isblank ((unsigned char) e2[-1]))
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
  while (isspace ((unsigned char)*s))
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
          error (NILF, _("$(%s): strptime(%s,%s,) -> %s\n"), funcname,
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
                  size_t comp_len = end ? (size_t)(end - comp) : strlen (comp);
                  if (!comp_len)
                    {
                      comp_len = 1;
                      src = ".";
                    }
                  if (len + comp_len + 2 + 4 < GET_PATH_MAX) /* +4 for .exe */
                    {
                      memcpy (buf, comp, comp_len);
                      buf [comp_len] = '/';
                      memcpy (&buf[comp_len + 1], cur, len);
                      buf[comp_len + 1 + len] = '\0';

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
       3rd argument according to the result. */
    rc = expr_eval_if_conditionals (argv[0], NULL);
    to_expand = rc == 0 ? argv[1] : argv[2];
    if (*to_expand)
      {
        char *expansion = expand_argument (to_expand, NULL);

        o = variable_buffer_output (o, expansion, strlen (expansion));

        free (expansion);
      }

    return o;
}

#endif /* CONFIG_WITH_IF_CONDITIONALS */

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
              while (lastitem > stack_var->value && isspace (lastitem[-1]))
                *--lastitem = '\0';
#ifdef CONFIG_WITH_VALUE_LENGTH
              stack_var->value_length = lastitem - stack_var->value;
#endif
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
  while (isspace (*str))
    str++;
  if (!*str)
    {
      error (NILF, _("bad number: empty\n"));
      return 0;
    }
  start = str;

  /* check for +/- */
  while (*str == '+' || *str == '-' || isspace (*str))
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
      error (NILF, _("bad number: '%s'\n"), start);
      return 0;
    }

  /* convert it! */
  while (*str && !isspace (*str))
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
          error (NILF, _("bad number: '%s' (base=%d, pos=%d)\n"), start, base, str - start);
          return 0;
        }
      num *= base;
      num += ch;
    }

  /* check trailing spaces. */
  while (isspace (*str))
    str++;
  if (*str)
    {
      error (NILF, _("bad number: '%s'\n"), start);
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
          error (NILF, _("divide by zero ('%s')\n"), argv[i]);
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
      error (NILF, _("divide by zero ('%s')\n"), argv[1]);
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
func_nanots (char *o, char **argv, const char *funcname)
{
  math_int ts;

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
          return func_nanots (o, argv, funcname);
        }
      ts = (math_int)((long double)pc.QuadPart / (long double)s_freq.QuadPart * 1000000000);
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

/* FIXME: Linux and others have the realtime clock_* api, detect and use it. */

#elif HAVE_GETTIMEOFDAY
  struct timeval tv;
  if (!gettimeofday (&tv, NULL))
    ts = (math_int)tv.tv_sec * 1000000000
       + tv.tv_usec * 1000;
  else
    {
      error (NILF, _("$(nanots): gettimeofday failed"));
      ts = 0;
    }

#else
# error "PORTME"
#endif

  return math_int_to_variable_buffer (o, ts);
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
      error (NILF, _("$(libpath): unknown variable `%s'"), argv[0]);
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
          error (NILF, _("$(libpath): failed to query `%s', rc=%d"), argv[0], rc);
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
          error (NILF, _("$(libpath): LIBPATH is read-only"));
          return variable_buffer_output (o, "", 0);
        }

      /* strip leading and trailing spaces and check for max length. */
      val = argv[1];
      while (isspace (*val))
        val++;
      end = strchr (val, '\0');
      while (end > val && isspace (end[-1]))
        end--;

      len = end - val;
      if (len >= len_max)
        {
          error (NILF, _("$(libpath): The new `%s' value is too long (%d bytes, max %d)"),
                 argv[0], len, len_max);
          return variable_buffer_output (o, "", 0);
        }

      /* make a stripped copy in low memory and try set it. */
      memcpy (buf, val, len);
      buf[len] = '\0';
      rc = DosSetExtLIBPATH (buf, fVar);
      if (rc != NO_ERROR)
        {
          error (NILF, _("$(libpath): failed to set `%s' to `%s', rc=%d"), argv[0], buf, rc);
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
      len = sprintf (buf, "alloc-cur: %5ld %6luKB (/%3luMB)  hash: %5lu %2lu%%",
                     make_stats_allocations,
                     make_stats_allocated / 1024,
                     make_stats_allocated_sum / (1024*1024),
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
          else if (!strcmp(argv[i], "allocated"))
            val = make_stats_allocated;
          else if (!strcmp(argv[i], "allocated_sum"))
            val = make_stats_allocated_sum;
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
      error (reading_file, _("$(%s ) was invoked recursivly"), funcname);
      return variable_buffer_output (o, "recursive", sizeof ("recursive") - 1);
    }
  if (*argv[0] == '\0')
    {
      error (reading_file, _("$(%s ) was invoked with an empty target name"), funcname);
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

      initialize_file_variables (file, 1 /* reading - FIXME: we don't know? */);
      set_file_variables (file); /* FIXME: this must *NOT* be done twice! */
      chop_commands (cmds);

      for (i = 0; i < cmds->ncommand_lines; i++)
        {
          char *p;
          char *in, *out, *ref;

          /* Skip it if it has a '%' prefix or is blank. */
          if (cmds->lines_flags[i] & COMMAND_GETTER_SKIP_IT)
            continue;
          p = cmds->command_lines[i];
          while (isblank ((unsigned char)*p))
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
                  char *p;

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
                          for (p = in - 1; p > ref && *p == '\\'; --p)
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
                                     && isblank ((unsigned char)out[-1]))
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
                error (reading_file, _("$(%s ) does not work reliably with $%c in all cases"), funcname, *ref);
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
          while (isblank ((unsigned char)*o)
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
func_breakpoint (char *o, char **argv, const char *funcname)
{
#ifdef _MSC_VER
  __debugbreak();
#elif defined(__i386__) || defined(__x86__) || defined(__X86__) || defined(_M_IX86) || defined(__i386) \
   || defined(__amd64__) || defined(__x86_64__) || defined(__AMD64__) || defined(_M_X64) || defined(__amd64)
  __asm__ __volatile__ ("int3\n\t");
#else
  char *p = (char *)0;
  *p = '\0';
#endif
  return o;
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


static struct function_table_entry function_table_init[] =
{
 /* Name/size */                    /* MIN MAX EXP? Function */
  { STRING_SIZE_TUPLE("abspath"),       0,  1,  1,  func_abspath},
  { STRING_SIZE_TUPLE("addprefix"),     2,  2,  1,  func_addsuffix_addprefix},
  { STRING_SIZE_TUPLE("addsuffix"),     2,  2,  1,  func_addsuffix_addprefix},
  { STRING_SIZE_TUPLE("basename"),      0,  1,  1,  func_basename_dir},
  { STRING_SIZE_TUPLE("dir"),           0,  1,  1,  func_basename_dir},
  { STRING_SIZE_TUPLE("notdir"),        0,  1,  1,  func_notdir_suffix},
  { STRING_SIZE_TUPLE("subst"),         3,  3,  1,  func_subst},
  { STRING_SIZE_TUPLE("suffix"),        0,  1,  1,  func_notdir_suffix},
  { STRING_SIZE_TUPLE("filter"),        2,  2,  1,  func_filter_filterout},
  { STRING_SIZE_TUPLE("filter-out"),    2,  2,  1,  func_filter_filterout},
  { STRING_SIZE_TUPLE("findstring"),    2,  2,  1,  func_findstring},
  { STRING_SIZE_TUPLE("firstword"),     0,  1,  1,  func_firstword},
  { STRING_SIZE_TUPLE("flavor"),        0,  1,  1,  func_flavor},
  { STRING_SIZE_TUPLE("join"),          2,  2,  1,  func_join},
  { STRING_SIZE_TUPLE("lastword"),      0,  1,  1,  func_lastword},
  { STRING_SIZE_TUPLE("patsubst"),      3,  3,  1,  func_patsubst},
  { STRING_SIZE_TUPLE("realpath"),      0,  1,  1,  func_realpath},
#ifdef CONFIG_WITH_RSORT
  { STRING_SIZE_TUPLE("rsort"),         0,  1,  1,  func_sort},
#endif
  { STRING_SIZE_TUPLE("shell"),         0,  1,  1,  func_shell},
  { STRING_SIZE_TUPLE("sort"),          0,  1,  1,  func_sort},
  { STRING_SIZE_TUPLE("strip"),         0,  1,  1,  func_strip},
  { STRING_SIZE_TUPLE("wildcard"),      0,  1,  1,  func_wildcard},
  { STRING_SIZE_TUPLE("word"),          2,  2,  1,  func_word},
  { STRING_SIZE_TUPLE("wordlist"),      3,  3,  1,  func_wordlist},
  { STRING_SIZE_TUPLE("words"),         0,  1,  1,  func_words},
  { STRING_SIZE_TUPLE("origin"),        0,  1,  1,  func_origin},
  { STRING_SIZE_TUPLE("foreach"),       3,  3,  0,  func_foreach},
  { STRING_SIZE_TUPLE("call"),          1,  0,  1,  func_call},
  { STRING_SIZE_TUPLE("info"),          0,  1,  1,  func_error},
  { STRING_SIZE_TUPLE("error"),         0,  1,  1,  func_error},
  { STRING_SIZE_TUPLE("warning"),       0,  1,  1,  func_error},
  { STRING_SIZE_TUPLE("if"),            2,  3,  0,  func_if},
  { STRING_SIZE_TUPLE("or"),            1,  0,  0,  func_or},
  { STRING_SIZE_TUPLE("and"),           1,  0,  0,  func_and},
  { STRING_SIZE_TUPLE("value"),         0,  1,  1,  func_value},
  { STRING_SIZE_TUPLE("eval"),          0,  1,  1,  func_eval},
#ifdef CONFIG_WITH_EVALPLUS
  { STRING_SIZE_TUPLE("evalctx"),       0,  1,  1,  func_evalctx},
  { STRING_SIZE_TUPLE("evalval"),       1,  1,  1,  func_evalval},
  { STRING_SIZE_TUPLE("evalvalctx"),    1,  1,  1,  func_evalval},
  { STRING_SIZE_TUPLE("evalcall"),      1,  0,  1,  func_call},
  { STRING_SIZE_TUPLE("evalcall2"),     1,  0,  1,  func_call},
  { STRING_SIZE_TUPLE("eval-opt-var"),  1,  0,  1,  func_eval_optimize_variable},
#endif
#ifdef EXPERIMENTAL
  { STRING_SIZE_TUPLE("eq"),            2,  2,  1,  func_eq},
  { STRING_SIZE_TUPLE("not"),           0,  1,  1,  func_not},
#endif
#ifdef CONFIG_WITH_LAZY_DEPS_VARS
  { STRING_SIZE_TUPLE("deps"),          1,  2,  1,  func_deps},
  { STRING_SIZE_TUPLE("deps-all"),      1,  2,  1,  func_deps},
  { STRING_SIZE_TUPLE("deps-newer"),    1,  2,  1,  func_deps_newer},
  { STRING_SIZE_TUPLE("deps-oo"),       1,  2,  1,  func_deps_order_only},
#endif
#ifdef CONFIG_WITH_DEFINED
  { STRING_SIZE_TUPLE("defined"),       1,  1,  1,  func_defined},
#endif
#ifdef CONFIG_WITH_TOUPPER_TOLOWER
  { STRING_SIZE_TUPLE("toupper"),       0,  1,  1,  func_toupper_tolower},
  { STRING_SIZE_TUPLE("tolower"),       0,  1,  1,  func_toupper_tolower},
#endif
#ifdef CONFIG_WITH_ABSPATHEX
  { STRING_SIZE_TUPLE("abspathex"),     0,  2,  1,  func_abspathex},
#endif
#ifdef CONFIG_WITH_XARGS
  { STRING_SIZE_TUPLE("xargs"),         2,  0,  1,  func_xargs},
#endif
#if defined(CONFIG_WITH_VALUE_LENGTH) && defined(CONFIG_WITH_COMPARE)
  { STRING_SIZE_TUPLE("comp-vars"),     3,  3,  1,  func_comp_vars},
  { STRING_SIZE_TUPLE("comp-cmds"),     3,  3,  1,  func_comp_vars},
  { STRING_SIZE_TUPLE("comp-cmds-ex"),  3,  3,  1,  func_comp_cmds_ex},
#endif
#ifdef CONFIG_WITH_DATE
  { STRING_SIZE_TUPLE("date"),          0,  1,  1,  func_date},
  { STRING_SIZE_TUPLE("date-utc"),      0,  3,  1,  func_date},
#endif
#ifdef CONFIG_WITH_FILE_SIZE
  { STRING_SIZE_TUPLE("file-size"),     1,  1,  1,  func_file_size},
#endif
#ifdef CONFIG_WITH_WHICH
  { STRING_SIZE_TUPLE("which"),         0,  0,  1,  func_which},
#endif
#ifdef CONFIG_WITH_IF_CONDITIONALS
  { STRING_SIZE_TUPLE("expr"),          1,  1,  0,  func_expr},
  { STRING_SIZE_TUPLE("if-expr"),       2,  3,  0,  func_if_expr},
#endif
#ifdef CONFIG_WITH_STACK
  { STRING_SIZE_TUPLE("stack-push"),    2,  2,  1,  func_stack_push},
  { STRING_SIZE_TUPLE("stack-pop"),     1,  1,  1,  func_stack_pop_top},
  { STRING_SIZE_TUPLE("stack-popv"),    1,  1,  1,  func_stack_pop_top},
  { STRING_SIZE_TUPLE("stack-top"),     1,  1,  1,  func_stack_pop_top},
#endif
#ifdef CONFIG_WITH_MATH
  { STRING_SIZE_TUPLE("int-add"),       2,  0,  1,  func_int_add},
  { STRING_SIZE_TUPLE("int-sub"),       2,  0,  1,  func_int_sub},
  { STRING_SIZE_TUPLE("int-mul"),       2,  0,  1,  func_int_mul},
  { STRING_SIZE_TUPLE("int-div"),       2,  0,  1,  func_int_div},
  { STRING_SIZE_TUPLE("int-mod"),       2,  2,  1,  func_int_mod},
  { STRING_SIZE_TUPLE("int-not"),       1,  1,  1,  func_int_not},
  { STRING_SIZE_TUPLE("int-and"),       2,  0,  1,  func_int_and},
  { STRING_SIZE_TUPLE("int-or"),        2,  0,  1,  func_int_or},
  { STRING_SIZE_TUPLE("int-xor"),       2,  0,  1,  func_int_xor},
  { STRING_SIZE_TUPLE("int-eq"),        2,  2,  1,  func_int_cmp},
  { STRING_SIZE_TUPLE("int-ne"),        2,  2,  1,  func_int_cmp},
  { STRING_SIZE_TUPLE("int-gt"),        2,  2,  1,  func_int_cmp},
  { STRING_SIZE_TUPLE("int-ge"),        2,  2,  1,  func_int_cmp},
  { STRING_SIZE_TUPLE("int-lt"),        2,  2,  1,  func_int_cmp},
  { STRING_SIZE_TUPLE("int-le"),        2,  2,  1,  func_int_cmp},
#endif
#ifdef CONFIG_WITH_NANOTS
  { STRING_SIZE_TUPLE("nanots"),        0,  0,  0,  func_nanots},
#endif
#ifdef CONFIG_WITH_OS2_LIBPATH
  { STRING_SIZE_TUPLE("libpath"),       1,  2,  1,  func_os2_libpath},
#endif
#ifdef CONFIG_WITH_MAKE_STATS
  { STRING_SIZE_TUPLE("make-stats"),    0,  0,  0,  func_make_stats},
#endif
#ifdef CONFIG_WITH_COMMANDS_FUNC
  { STRING_SIZE_TUPLE("commands"),      1,  1,  1,  func_commands},
  { STRING_SIZE_TUPLE("commands-sc"),   1,  1,  1,  func_commands},
  { STRING_SIZE_TUPLE("commands-usr"),  2,  2,  1,  func_commands},
#endif
#ifdef KMK_HELPERS
  { STRING_SIZE_TUPLE("kb-src-tool"),   1,  1,  0,  func_kbuild_source_tool},
  { STRING_SIZE_TUPLE("kb-obj-base"),   1,  1,  0,  func_kbuild_object_base},
  { STRING_SIZE_TUPLE("kb-obj-suff"),   1,  1,  0,  func_kbuild_object_suffix},
  { STRING_SIZE_TUPLE("kb-src-prop"),   3,  4,  0,  func_kbuild_source_prop},
  { STRING_SIZE_TUPLE("kb-src-one"),    0,  1,  0,  func_kbuild_source_one},
#endif
#ifdef KMK
  { STRING_SIZE_TUPLE("breakpoint"),    0,  0,  0,  func_breakpoint},
#endif
};

#define FUNCTION_TABLE_ENTRIES (sizeof (function_table_init) / sizeof (struct function_table_entry))


/* These must come after the definition of function_table.  */

static char *
expand_builtin_function (char *o, int argc, char **argv,
                         const struct function_table_entry *entry_p)
{
  if (argc < (int)entry_p->minimum_args)
    fatal (*expanding_var,
           _("insufficient number of arguments (%d) to function `%s'"),
           argc, entry_p->name);

  /* I suppose technically some function could do something with no
     arguments, but so far none do, so just test it for all functions here
     rather than in each one.  We can change it later if necessary.  */

  if (!argc)
    return o;

  if (!entry_p->func_ptr)
    fatal (*expanding_var,
           _("unimplemented on this platform: function `%s'"), entry_p->name);

  return entry_p->func_ptr (o, argv, entry_p->name);
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

  beg = next_token (beg + entry_p->len);

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
    fatal (*expanding_var,
	   _("unterminated call to function `%s': missing `%c'"),
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
  if (abeg)
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
handle_function (char **op, const char **stringp, const char *nameend, const char *eol)
{
  const char *fname = *stringp + 1;
  const struct function_table_entry *entry_p =
      lookup_function_in_hash_tab (fname, nameend - fname);
  if (!entry_p)
    return 0;
  return handle_function2 (entry_p, op, stringp);
}
#endif /* CONFIG_WITH_VALUE_LENGTH */


/* User-defined functions.  Expand the first argument as either a builtin
   function or a make variable, in the context of the rest of the arguments
   assigned to $1, $2, ... $N.  $0 is the name of the function.  */

static char *
func_call (char *o, char **argv, const char *funcname UNUSED)
{
  static int max_args = 0;
  char *fname;
  char *cp;
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

  /* There is no way to define a variable with a space in the name, so strip
     leading and trailing whitespace as a favor to the user.  */
  fname = argv[0];
  while (*fname != '\0' && isspace ((unsigned char)*fname))
    ++fname;

  cp = fname + strlen (fname) - 1;
  while (cp > fname && isspace ((unsigned char)*cp))
    --cp;
  cp[1] = '\0';

  /* Calling nothing is a no-op */
  if (*fname == '\0')
    return o;

  /* Are we invoking a builtin function?  */

#ifndef CONFIG_WITH_VALUE_LENGTH
  entry_p = lookup_function (fname);
#else
  entry_p = lookup_function (fname, cp - fname + 1);
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
    {
      char num[11];

      sprintf (num, "%d", i);
      define_variable (num, strlen (num), *argv, o_automatic, 0);
    }

  /* If the number of arguments we have is < max_args, it means we're inside
     a recursive invocation of $(call ...).  Fill in the remaining arguments
     in the new scope with the empty value, to hide them from this
     invocation.  */

  for (; i < max_args; ++i)
    {
      char num[11];

#ifndef CONFIG_WITH_VALUE_LENGTH
      sprintf (num, "%d", i);
      define_variable (num, strlen (num), "", o_automatic, 0);
#else
      define_variable (num, sprintf (num, "%d", i), "", o_automatic, 0);
#endif
    }

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
      const struct floc *reading_file_saved = reading_file;
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
      eval_buffer (o, eos);
      restore_variable_buffer (buf, len);
      reading_file = reading_file_saved;
    }
#endif /* CONFIG_WITH_EVALPLUS */

  max_args = saved_args;

  pop_variable_scope ();

  return o;
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
