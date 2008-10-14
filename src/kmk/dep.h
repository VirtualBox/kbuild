/* Definitions of dependency data structures for GNU Make.
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

/* Flag bits for the second argument to `read_makefile'.
   These flags are saved in the `changed' field of each
   `struct dep' in the chain returned by `read_all_makefiles'.  */

#define RM_NO_DEFAULT_GOAL	(1 << 0) /* Do not set default goal.  */
#define RM_INCLUDED		(1 << 1) /* Search makefile search path.  */
#define RM_DONTCARE		(1 << 2) /* No error if it doesn't exist.  */
#define RM_NO_TILDE		(1 << 3) /* Don't expand ~ in file name.  */
#define RM_NOFLAG		0

/* Structure representing one dependency of a file.
   Each struct file's `deps' points to a chain of these,
   chained through the `next'. `stem' is the stem for this
   dep line of static pattern rule or NULL.

   Note that the first two words of this match a struct nameseq.  */

struct dep
  {
    struct dep *next;
    const char *name;
    const char *stem;
    struct file *file;
    unsigned int changed : 8;
    unsigned int ignore_mtime : 1;
    unsigned int staticpattern : 1;
    unsigned int need_2nd_expansion : 1;
#ifdef CONFIG_WITH_INCLUDEDEP
    unsigned int includedep : 1;
#endif
  };


/* Structure used in chains of names, for parsing and globbing.  */

struct nameseq
  {
    struct nameseq *next;
    const char *name;
  };



#ifndef CONFIG_WITH_ALLOC_CACHES
struct nameseq *multi_glob (struct nameseq *chain, unsigned int size);
#else
struct nameseq *multi_glob (struct nameseq *chain, struct alloccache *cache);
#endif
#ifdef VMS
struct nameseq *parse_file_seq ();
#else
# ifndef CONFIG_WITH_ALLOC_CACHES
struct nameseq *parse_file_seq (char **stringp, int stopchar, unsigned int size, int strip);
# else
struct nameseq *parse_file_seq (char **stringp, int stopchar, struct alloccache *cache, int strip);
# endif
#endif
char *tilde_expand (const char *name);

#ifndef NO_ARCHIVES
struct nameseq *ar_glob (const char *arname, const char *member_pattern, unsigned int size);
#endif

#define dep_name(d) ((d)->name == 0 ? (d)->file->name : (d)->name)

struct dep *alloc_dep (void);
void free_dep (struct dep *d);
struct dep *copy_dep_chain (const struct dep *d);
void free_dep_chain (struct dep *d);
void free_ns_chain (struct nameseq *n);
struct dep *read_all_makefiles (const char **makefiles);
#ifndef CONFIG_WITH_VALUE_LENGTH
int eval_buffer (char *buffer);
#else
int eval_buffer (char *buffer, char *eos);
#endif
int update_goal_chain (struct dep *goals);
void uniquize_deps (struct dep *);

#ifdef CONFIG_WITH_INCLUDEDEP
/* incdep.c */
enum incdep_op { incdep_read_it, incdep_queue, incdep_flush };
void eval_include_dep (const char *name, struct floc *f, enum incdep_op op);
void incdep_flush_and_term (void);

/* read.c */
void record_files (struct nameseq *filenames, const char *pattern,
                   const char *pattern_percent, struct dep *deps,
                   unsigned int cmds_started, char *commands,
                   unsigned int commands_idx, int two_colon,
                   const struct floc *flocp);
#endif

