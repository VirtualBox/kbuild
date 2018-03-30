/* Reentrant version of getopt.

Based on ../getopt*.*:

   Declarations for getopt.
Copyright (C) 1989-2016 Free Software Foundation, Inc.

NOTE: The canonical source of this file is maintained with the GNU C Library.
Bugs can be reported to bug-glibc@gnu.org.

GNU Make is free software; you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation; either version 3 of the License, or (at your option) any later
version.

GNU Make is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program.  If not, see <http://www.gnu.org/licenses/>.

Modifications:
  Copyright (c) 2018 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
*/

/* Not quite safe to mix when converting code. */
#ifdef _GETOPT_H
# define _GETOPT_H "getopt.h was included already"
# error "getopt.h was included already"
#endif

#ifndef INCLUDED_GETOPT_R_H
#define INCLUDED_GETOPT_R_H 1

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct getopt_state_r
{

/* For communication from `getopt' to the caller.
   When `getopt' finds an option that takes an argument,
   the argument value is returned here.
   Also, when `ordering' is RETURN_IN_ORDER,
   each non-option ARGV-element is returned here.  */

/*extern*/ char *optarg;

/* Index in ARGV of the next element to be scanned.
   This is used for communication to and from the caller
   and for communication between successive calls to `getopt'.

   On entry to `getopt', zero means this is the first call; initialize.

   When `getopt' returns -1, this is the index of the first of the
   non-option elements that the caller should itself scan.

   Otherwise, `optind' communicates from one call to the next
   how much of ARGV has been scanned so far.  */

/*extern*/ int optind;

/* Callers store zero here to inhibit the error message `getopt' prints
   for unrecognized options.  */

/*extern*/ int opterr;

/* Set to an option character which was unrecognized.  */

/*extern*/ int optopt;


/* Internal state: */

/* The next char to be scanned in the option-element
   in which the last option character we returned was found.
   This allows us to pick up the scan where we left off.

   If this is zero, or a null string, it means resume the scan
   by advancing to the next ARGV-element.  */

/*static*/ char *nextchar;

/* REQUIRE_ORDER, PERMUTE or RETURN_IN_ORDER, see getopt_r.c. */
/*static*/ int ordering;

/* Value of POSIXLY_CORRECT environment variable.  */
/*static*/ char *posixly_correct;

/* Describe the part of ARGV that contains non-options that have
   been skipped.  `first_nonopt' is the index in ARGV of the first of them;
   `last_nonopt' is the index after the last of them.  */

/*static*/ int first_nonopt;
/*static*/ int last_nonopt;

/* Mainly for asserting usage sanity. */
/*static*/ void *__getopt_initialized;

/* New internal state (to resubmitting same parameters in each call): */
  /* new: the argument vector length. */
  int argc;
  /* new: the argument vector. */
  char * const *argv;
  /* new: the short option string (can be NULL/empty). */
  const char *optstring;
  /* new: the short option string length. */
  size_t len_optstring;
  /* new: the long options (can be NULL) */
  const struct option *long_options;
  /* Output context for err.h. */
  struct KMKBUILTINCTX *pCtx;
} getopt_state_r;


#ifndef no_argument

/* Describe the long-named options requested by the application.
   The LONG_OPTIONS argument to getopt_long or getopt_long_only is a vector
   of `struct option' terminated by an element containing a name which is
   zero.

   The field `has_arg' is:
   no_argument		(or 0) if the option does not take an argument,
   required_argument	(or 1) if the option requires an argument,
   optional_argument 	(or 2) if the option takes an optional argument.

   If the field `flag' is not NULL, it points to a variable that is set
   to the value given in the field `val' when the option is found, but
   left unchanged if the option is not found.

   To have a long-named option do something other than set an `int' to
   a compiled-in constant, such as set a value from `optarg', set the
   option's `flag' field to zero and its `val' field to a nonzero
   value (the equivalent single-letter option character, if there is
   one).  For long options that have a zero `flag' field, `getopt'
   returns the contents of the `val' field.  */

struct option
{
#if defined (__STDC__) && __STDC__
  const char *name;
#else
  char *name;
#endif
  /* has_arg can't be an enum because some compilers complain about
     type mismatches in all the code that assumes it is an int.  */
  int has_arg;
  int *flag;
  int val;
};

/* Names for the values of the `has_arg' field of `struct option'.  */

#define	no_argument		0
#define required_argument	1
#define optional_argument	2

#endif /* Same as ../getopt.h.  Fix later? */

extern void getopt_initialize_r (struct getopt_state_r *gos, int argc,
                                 char *const *argv, const char *shortopts,
		                 const struct option *longopts,
                                 char **envp, struct KMKBUILTINCTX *pCtx);
extern int getopt_r (struct getopt_state_r *gos);
extern int getopt_long_r (struct getopt_state_r *gos, int *longind);
extern int getopt_long_only_r (struct getopt_state_r *gos, int *longind);

/* Internal only.  Users should not call this directly.  */
extern int _getopt_internal_r (struct getopt_state_r *gos,
                               const struct option *longopts,
                               int *longind, int long_only);

#ifdef	__cplusplus
}
#endif

#endif /* getopt_r.h */
