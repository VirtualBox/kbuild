/* Target file management for GNU Make.
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

#include <assert.h>

#include "dep.h"
#include "filedef.h"
#include "job.h"
#include "commands.h"
#include "variable.h"
#include "debug.h"
#include "hash.h"


/* Remember whether snap_deps has been invoked: we need this to be sure we
   don't add new rules (via $(eval ...)) afterwards.  In the future it would
   be nice to support this, but it means we'd need to re-run snap_deps() or
   at least its functionality... it might mean changing snap_deps() to be run
   per-file, so we can invoke it after the eval... or remembering which files
   in the hash have been snapped (a new boolean flag?) and having snap_deps()
   only work on files which have not yet been snapped. */
int snapped_deps = 0;

/* Hash table of files the makefile knows how to make.  */

#ifndef CONFIG_WITH_STRCACHE2
static unsigned long
file_hash_1 (const void *key)
{
  return_ISTRING_HASH_1 (((struct file const *) key)->hname);
}

static unsigned long
file_hash_2 (const void *key)
{
  return_ISTRING_HASH_2 (((struct file const *) key)->hname);
}
#endif /* !CONFIG_WITH_STRCACHE2 */

static int
file_hash_cmp (const void *x, const void *y)
{
#ifndef CONFIG_WITH_STRCACHE2
  return_ISTRING_COMPARE (((struct file const *) x)->hname,
			  ((struct file const *) y)->hname);
#else  /* CONFIG_WITH_STRCACHE2 */
  return ((struct file const *) x)->hname
      == ((struct file const *) y)->hname ? 0 : -1;
#endif /* CONFIG_WITH_STRCACHE2 */
}

#ifndef	FILE_BUCKETS
#define FILE_BUCKETS	1007
#endif
static struct hash_table files;

/* Whether or not .SECONDARY with no prerequisites was given.  */
static int all_secondary = 0;

/* Access the hash table of all file records.
   lookup_file  given a name, return the struct file * for that name,
                or nil if there is none.
*/

#ifndef KMK
struct file *
lookup_file (const char *name)
#else  /* KMK */
MY_INLINE struct file *
lookup_file_common (const char *name, int cached)
#endif /* KMK */
{
  struct file *f;
  struct file file_key;
#if defined(VMS) && !defined(WANT_CASE_SENSITIVE_TARGETS)
  char *lname;
#endif

  assert (*name != '\0');

  /* This is also done in parse_file_seq, so this is redundant
     for names read from makefiles.  It is here for names passed
     on the command line.  */
#ifdef VMS
# ifndef WANT_CASE_SENSITIVE_TARGETS
  if (*name != '.')
    {
      const char *n;
      char *ln;
      lname = xstrdup (name);
      for (n = name, ln = lname; *n != '\0'; ++n, ++ln)
        *ln = isupper ((unsigned char)*n) ? tolower ((unsigned char)*n) : *n;
      *ln = '\0';
      name = lname;
    }
# endif

  while (name[0] == '[' && name[1] == ']' && name[2] != '\0')
      name += 2;
#endif
  while (name[0] == '.' && name[1] == '/' && name[2] != '\0')
    {
      name += 2;
      while (*name == '/')
	/* Skip following slashes: ".//foo" is "foo", not "/foo".  */
	++name;
    }

  if (*name == '\0')
    /* It was all slashes after a dot.  */
#if defined(VMS)
    name = "[]";
#elif defined(_AMIGA)
    name = "";
#else
    name = "./";
#endif

#ifndef CONFIG_WITH_STRCACHE2
  file_key.hname = name;
  f = hash_find_item (&files, &file_key);
#else  /* CONFIG_WITH_STRCACHE2 */
  if (!cached)
    {
      file_key.hname = strcache2_lookup (&file_strcache, name, strlen (name));
      if (file_key.hname)
        f = hash_find_item_strcached (&files, &file_key);
      else
        f = NULL;
    }
  else
    {
      file_key.hname = name;
      f = hash_find_item_strcached (&files, &file_key);
    }

#endif /* CONFIG_WITH_STRCACHE2 */
#if defined(VMS) && !defined(WANT_CASE_SENSITIVE_TARGETS)
  if (*name != '.')
    free (lname);
#endif

  return f;
}

#ifdef KMK
/* Given a name, return the struct file * for that name,
  or nil if there is none. */

struct file *
lookup_file (const char *name)
{
  return lookup_file_common (name, 0 /* cached */);
}

/* Given a name in the strcache, return the struct file * for that name,
  or nil if there is none. */
struct file *
lookup_file_cached (const char *name)
{
  assert (strcache_iscached (name));
  return lookup_file_common (name, 1 /* cached */);
}
#endif /* KMK */


/* Look up a file record for file NAME and return it.
   Create a new record if one doesn't exist.  NAME will be stored in the
   new record so it should be constant or in the strcache etc.
 */

struct file *
enter_file (const char *name)
{
  struct file *f;
  struct file *new;
  struct file **file_slot;
  struct file file_key;

  assert (*name != '\0');
  assert (strcache_iscached (name));

#if defined(VMS) && !defined(WANT_CASE_SENSITIVE_TARGETS)
  if (*name != '.')
    {
      const char *n;
      char *lname, *ln;
      lname = xstrdup (name);
      for (n = name, ln = lname; *n != '\0'; ++n, ++ln)
        if (isupper ((unsigned char)*n))
          *ln = tolower ((unsigned char)*n);
        else
          *ln = *n;

      *ln = '\0';
      name = strcache_add (lname);
      free (lname);
    }
#endif

  file_key.hname = name;
#ifndef CONFIG_WITH_STRCACHE2
  file_slot = (struct file **) hash_find_slot (&files, &file_key);
#else  /* CONFIG_WITH_STRCACHE2 */
  file_slot = (struct file **) hash_find_slot_strcached (&files, &file_key);
#endif /* CONFIG_WITH_STRCACHE2 */
  f = *file_slot;
  if (! HASH_VACANT (f) && !f->double_colon)
    return f;

#ifndef CONFIG_WITH_ALLOC_CACHES
  new = xmalloc (sizeof (struct file));
  memset (new, '\0', sizeof (struct file));
#else
  new = alloccache_calloc (&file_cache);
#endif
  new->name = new->hname = name;
  new->update_status = -1;

  if (HASH_VACANT (f))
    {
      new->last = new;
      hash_insert_at (&files, new, file_slot);
    }
  else
    {
      /* There is already a double-colon entry for this file.  */
      new->double_colon = f;
      f->last->prev = new;
      f->last = new;
    }

#ifdef CONFIG_WITH_2ND_TARGET_EXPANSION
  /* Check if the name needs 2nd expansion or not. */
  if (second_target_expansion && strchr (name, '$') != NULL)
    new->need_2nd_target_expansion = 1;
#endif

  return new;
}

/* Rehash FILE to NAME.  This is not as simple as resetting
   the `hname' member, since it must be put in a new hash bucket,
   and possibly merged with an existing file called NAME.  */

void
rehash_file (struct file *from_file, const char *to_hname)
{
  struct file file_key;
  struct file **file_slot;
  struct file *to_file;
  struct file *deleted_file;
  struct file *f;

#ifdef CONFIG_WITH_STRCACHE2
  assert (strcache_iscached (to_hname));
  assert (strcache_iscached (from_file->hname));
#endif

  /* If it's already that name, we're done.  */
  file_key.hname = to_hname;
  if (! file_hash_cmp (from_file, &file_key))
    return;

  /* Find the end of the renamed list for the "from" file.  */
  file_key.hname = from_file->hname;
  while (from_file->renamed != 0)
    from_file = from_file->renamed;
  if (file_hash_cmp (from_file, &file_key))
    /* hname changed unexpectedly!! */
    abort ();

  /* Remove the "from" file from the hash.  */
#ifndef CONFIG_WITH_STRCACHE2
  deleted_file = hash_delete (&files, from_file);
#else
  deleted_file = hash_delete_strcached (&files, from_file);
#endif
  if (deleted_file != from_file)
    /* from_file isn't the one stored in files */
    abort ();

  /* Find where the newly renamed file will go in the hash.  */
  file_key.hname = to_hname;
#ifndef CONFIG_WITH_STRCACHE2
  file_slot = (struct file **) hash_find_slot (&files, &file_key);
#else  /* CONFIG_WITH_STRCACHE2 */
  file_slot = (struct file **) hash_find_slot_strcached (&files, &file_key);
#endif /* CONFIG_WITH_STRCACHE2 */
  to_file = *file_slot;

  /* Change the hash name for this file.  */
  from_file->hname = to_hname;
  for (f = from_file->double_colon; f != 0; f = f->prev)
    f->hname = to_hname;

  /* If the new name doesn't exist yet just set it to the renamed file.  */
  if (HASH_VACANT (to_file))
    {
      hash_insert_at (&files, from_file, file_slot);
      return;
    }

  /* TO_FILE already exists under TO_HNAME.
     We must retain TO_FILE and merge FROM_FILE into it.  */

  if (from_file->cmds != 0)
    {
      if (to_file->cmds == 0)
        to_file->cmds = from_file->cmds;
      else if (from_file->cmds != to_file->cmds)
        {
          /* We have two sets of commands.  We will go with the
             one given in the rule explicitly mentioning this name,
             but give a message to let the user know what's going on.  */
          if (to_file->cmds->fileinfo.filenm != 0)
            error (&from_file->cmds->fileinfo,
                   _("Commands were specified for file `%s' at %s:%lu,"),
                   from_file->name, to_file->cmds->fileinfo.filenm,
                   to_file->cmds->fileinfo.lineno);
          else
            error (&from_file->cmds->fileinfo,
                   _("Commands for file `%s' were found by implicit rule search,"),
                   from_file->name);
          error (&from_file->cmds->fileinfo,
                 _("but `%s' is now considered the same file as `%s'."),
                 from_file->name, to_hname);
          error (&from_file->cmds->fileinfo,
                 _("Commands for `%s' will be ignored in favor of those for `%s'."),
                 to_hname, from_file->name);
        }
    }

  /* Merge the dependencies of the two files.  */

  if (to_file->deps == 0)
    to_file->deps = from_file->deps;
  else
    {
      struct dep *deps = to_file->deps;
      while (deps->next != 0)
        deps = deps->next;
      deps->next = from_file->deps;
    }

  merge_variable_set_lists (&to_file->variables, from_file->variables);

  if (to_file->double_colon && from_file->is_target && !from_file->double_colon)
    fatal (NILF, _("can't rename single-colon `%s' to double-colon `%s'"),
           from_file->name, to_hname);
  if (!to_file->double_colon  && from_file->double_colon)
    {
      if (to_file->is_target)
        fatal (NILF, _("can't rename double-colon `%s' to single-colon `%s'"),
               from_file->name, to_hname);
      else
        to_file->double_colon = from_file->double_colon;
    }

  if (from_file->last_mtime > to_file->last_mtime)
    /* %%% Kludge so -W wins on a file that gets vpathized.  */
    to_file->last_mtime = from_file->last_mtime;

  to_file->mtime_before_update = from_file->mtime_before_update;

#define MERGE(field) to_file->field |= from_file->field
  MERGE (precious);
  MERGE (tried_implicit);
  MERGE (updating);
  MERGE (updated);
  MERGE (is_target);
  MERGE (cmd_target);
  MERGE (phony);
  MERGE (ignore_vpath);
#undef MERGE

  from_file->renamed = to_file;
}

/* Rename FILE to NAME.  This is not as simple as resetting
   the `name' member, since it must be put in a new hash bucket,
   and possibly merged with an existing file called NAME.  */

void
rename_file (struct file *from_file, const char *to_hname)
{
  rehash_file (from_file, to_hname);
  while (from_file)
    {
      from_file->name = from_file->hname;
      from_file = from_file->prev;
    }
}

#ifdef CONFIG_WITH_2ND_TARGET_EXPANSION
/* Performs secondary target name expansion and then renames
   the file using rename_file. */
static void
do_2nd_target_expansion (struct file *f)
{
   unsigned int len;
   char *tmp_name = allocated_variable_expand_2 (
       f->name, strcache2_get_len (&file_strcache, f->name), &len);
   const char *name = strcache_add_len (tmp_name, len);
   free (tmp_name);
   rename_file (f, name);
}
#endif /* CONFIG_WITH_2ND_TARGET_EXPANSION */

/* Remove all nonprecious intermediate files.
   If SIG is nonzero, this was caused by a fatal signal,
   meaning that a different message will be printed, and
   the message will go to stderr rather than stdout.  */

void
remove_intermediates (int sig)
{
  struct file **file_slot;
  struct file **file_end;
  int doneany = 0;

  /* If there's no way we will ever remove anything anyway, punt early.  */
  if (question_flag || touch_flag || all_secondary)
    return;

  if (sig && just_print_flag)
    return;

  file_slot = (struct file **) files.ht_vec;
  file_end = file_slot + files.ht_size;
  for ( ; file_slot < file_end; file_slot++)
    if (! HASH_VACANT (*file_slot))
      {
	struct file *f = *file_slot;
        /* Is this file eligible for automatic deletion?
           Yes, IFF: it's marked intermediate, it's not secondary, it wasn't
           given on the command-line, and it's either a -include makefile or
           it's not precious.  */
	if (f->intermediate && (f->dontcare || !f->precious)
	    && !f->secondary && !f->cmd_target)
	  {
	    int status;
	    if (f->update_status == -1)
	      /* If nothing would have created this file yet,
		 don't print an "rm" command for it.  */
	      continue;
	    if (just_print_flag)
	      status = 0;
	    else
	      {
		status = unlink (f->name);
		if (status < 0 && errno == ENOENT)
		  continue;
	      }
	    if (!f->dontcare)
	      {
		if (sig)
		  error (NILF, _("*** Deleting intermediate file `%s'"), f->name);
		else
		  {
		    if (! doneany)
		      DB (DB_BASIC, (_("Removing intermediate files...\n")));
		    if (!silent_flag)
		      {
			if (! doneany)
			  {
			    fputs ("rm ", stdout);
			    doneany = 1;
			  }
			else
			  putchar (' ');
			fputs (f->name, stdout);
			fflush (stdout);
		      }
		  }
		if (status < 0)
		  perror_with_name ("unlink: ", f->name);
	      }
	  }
      }

  if (doneany && !sig)
    {
      putchar ('\n');
      fflush (stdout);
    }
}

struct dep *
parse_prereqs (char *p)
{
#ifndef CONFIG_WITH_ALLOC_CACHES
  struct dep *new = (struct dep *)
    multi_glob (parse_file_seq (&p, '|', sizeof (struct dep), 1),
                sizeof (struct dep));
#else
  struct dep *new = (struct dep *)
    multi_glob (parse_file_seq (&p, '|', &dep_cache, 1), &dep_cache);
#endif

  if (*p)
    {
      /* Files that follow '|' are "order-only" prerequisites that satisfy the
         dependency by existing: their modification times are irrelevant.  */
      struct dep *ood;

      ++p;
#ifndef CONFIG_WITH_ALLOC_CACHES
      ood = (struct dep *)
        multi_glob (parse_file_seq (&p, '\0', sizeof (struct dep), 1),
                    sizeof (struct dep));
#else
      ood = (struct dep *)
        multi_glob (parse_file_seq (&p, '\0', &dep_cache, 1), &dep_cache);
#endif
      if (! new)
        new = ood;
      else
        {
          struct dep *dp;
          for (dp = new; dp->next != NULL; dp = dp->next)
            ;
          dp->next = ood;
        }

      for (; ood != NULL; ood = ood->next)
        ood->ignore_mtime = 1;
    }

  return new;
}

/* Set the intermediate flag.  */

static void
set_intermediate (const void *item)
{
  struct file *f = (struct file *) item;
  f->intermediate = 1;
}

/* Expand and parse each dependency line. */
static void
expand_deps (struct file *f)
{
  struct dep *d;
  struct dep *old = f->deps;
  const char *file_stem = f->stem;
  unsigned int last_dep_has_cmds = f->updating;
  int initialized = 0;

  f->updating = 0;
  f->deps = 0;

  for (d = old; d != 0; d = d->next)
    {
      size_t buffer_offset; /* bird */
      struct dep *new, *d1;
      char *p;
#ifdef CONFIG_WITH_VALUE_LENGTH
      unsigned int len;
#endif

      if (! d->name)
        continue;

#ifdef CONFIG_WITH_INCLUDEDEP
      /* Dependencies loaded by includedep are ready for use and we skip
         the expensive parsing and globbing for them. To avoid wasting
         lots of time walking the f->deps chain, we will advance D and
         process all subsequent includedep records. */

      if (d->includedep)
        {
          new = d1 = alloc_dep();
          d1->staticpattern = 0;
          d1->need_2nd_expansion = 0;
          d1->includedep = 1;
          d1->file = lookup_file (d->name);
          if (d1->file == 0)
            d1->file = enter_file (d->name);

          while (d->next && d->next->includedep)
            {
              d = d->next;
              d1 = d1->next = alloc_dep();
              d1->staticpattern = 0;
              d1->need_2nd_expansion = 0;
              d1->includedep = 1;
              d1->file = lookup_file (d->name);
              if (d1->file == 0)
                d1->file = enter_file (d->name);
            }
        }
      else
        {
#endif

      /* Create the dependency list.
         If we're not doing 2nd expansion, then it's just the name.  We will
         still need to massage it though.  */
      if (! d->need_2nd_expansion)
        {
          p = variable_expand ("");
          buffer_offset = p - variable_buffer;
#ifndef CONFIG_WITH_STRCACHE2
          variable_buffer_output (p, d->name, strlen (d->name) + 1);
#else
          len = strcache2_get_len (&file_strcache, d->name);
          variable_buffer_output (p, d->name, len + 1);
#endif
          p = variable_buffer + buffer_offset; /* bird - variable_buffer may have been reallocated. (observed it) */
        }
      else
        {
          /* If it's from a static pattern rule, convert the patterns into
             "$*" so they'll expand properly.  */
          if (d->staticpattern)
            {
              char *o;
              char *buffer = variable_expand ("");
              buffer_offset = buffer - variable_buffer; /* bird */

              o = subst_expand (buffer, d->name, "%", "$*", 1, 2, 0);
              buffer = variable_buffer + buffer_offset; /* bird - variable_buffer may have been reallocated. */

              d->name = strcache_add_len (buffer, o - buffer);
              d->staticpattern = 0; /* Clear staticpattern so that we don't
                                       re-expand %s below. */
            }

          /* We are going to do second expansion so initialize file variables
             for the file. Since the stem for static pattern rules comes from
             individual dep lines, we will temporarily set f->stem to d->stem.
          */
          if (!initialized)
            {
              initialize_file_variables (f, 0);
              initialized = 1;
            }

          if (d->stem != 0)
            f->stem = d->stem;

          set_file_variables (f);

#ifndef CONFIG_WITH_STRCACHE2
          p = variable_expand_for_file (d->name, f);
#else
          len = strcache2_get_len (&file_strcache, d->name);
          p = variable_expand_for_file_2 (NULL, d->name, len, f, &len);
#endif

          if (d->stem != 0)
            f->stem = file_stem;
        }

      /* Parse the prerequisites.  */
      new = parse_prereqs (p);

      /* If this dep list was from a static pattern rule, expand the %s.  We
         use patsubst_expand to translate the prerequisites' patterns into
         plain prerequisite names.  */
      if (new && d->staticpattern)
        {
          const char *pattern = "%";
          char *buffer = variable_expand ("");
          const size_t buffer_offset = buffer - variable_buffer; /* bird */
          struct dep *dp = new, *dl = 0;

          while (dp != 0)
            {
              char *percent;
#ifndef KMK
              int nl = strlen (dp->name) + 1;
              char *nm = alloca (nl);
              memcpy (nm, dp->name, nl);
              percent = find_percent (nm);
#else  /* KMK - don't make a stack copy unless it's actually required! */
              unsigned int nl = strcache2_get_len (&file_strcache, dp->name);
              char *nm;
              percent = memchr (dp->name, '%', nl);
              if (percent)
                {
                  nm = alloca (nl + 1);
                  memcpy (nm, dp->name, nl + 1);
                  percent = find_percent (nm);
                }
#endif /* KMK */
              if (percent)
                {
                  char *o;

                  /* We have to handle empty stems specially, because that
                     would be equivalent to $(patsubst %,dp->name,) which
                     will always be empty.  */
                  if (d->stem[0] == '\0')
                    {
                      memmove (percent, percent+1, strlen (percent));
                      o = variable_buffer_output (buffer, nm, strlen (nm) + 1);
                    }
                  else
                    o = patsubst_expand_pat (buffer, d->stem, pattern, nm,
                                             pattern+1, percent+1);
                  buffer = variable_buffer + buffer_offset; /* bird - variable_buffer may have been reallocated. */


                  /* If the name expanded to the empty string, ignore it.  */
                  if (buffer[0] == '\0')
                    {
                      struct dep *df = dp;
                      if (dp == new)
                        dp = new = new->next;
                      else
                        dp = dl->next = dp->next;
                      free_dep (df);
                      continue;
                    }

                  /* Save the name.  */
                  dp->name = strcache_add_len (buffer, o - buffer);
                }
              dl = dp;
              dp = dp->next;
            }
        }

      /* Enter them as files. */
      for (d1 = new; d1 != 0; d1 = d1->next)
        {
          d1->file = lookup_file (d1->name);
          if (d1->file == 0)
            d1->file = enter_file (d1->name);
          d1->name = 0;
          d1->staticpattern = 0;
          d1->need_2nd_expansion = 0;
        }

#ifdef CONFIG_WITH_INCLUDEDEP
        }
#endif

      /* Add newly parsed deps to f->deps. If this is the last dependency
         line and this target has commands then put it in front so the
         last dependency line (the one with commands) ends up being the
         first. This is important because people expect $< to hold first
         prerequisite from the rule with commands. If it is not the last
         dependency line or the rule does not have commands then link it
         at the end so it appears in makefile order.  */

      if (new != 0)
        {
          if (d->next == 0 && last_dep_has_cmds)
            {
              struct dep **d_ptr;
              for (d_ptr = &new; *d_ptr; d_ptr = &(*d_ptr)->next)
                ;

              *d_ptr = f->deps;
              f->deps = new;
            }
          else
            {
              struct dep **d_ptr;
              for (d_ptr = &f->deps; *d_ptr; d_ptr = &(*d_ptr)->next)
                ;

              *d_ptr = new;
            }
        }
    }

  free_dep_chain (old);
}

/* For each dependency of each file, make the `struct dep' point
   at the appropriate `struct file' (which may have to be created).

   Also mark the files depended on by .PRECIOUS, .PHONY, .SILENT,
   and various other special targets.  */

void
snap_deps (void)
{
  struct file *f;
  struct file *f2;
  struct dep *d;
  struct file **file_slot_0;
  struct file **file_slot;
  struct file **file_end;

  /* Perform second expansion and enter each dependency name as a file. */

  /* Expand .SUFFIXES first; it's dependencies are used for $$* calculation. */
  for (f = lookup_file (".SUFFIXES"); f != 0; f = f->prev)
    expand_deps (f);

#ifdef CONFIG_WITH_2ND_TARGET_EXPANSION
  /* Perform 2nd target expansion on files which requires this. This will
     be re-inserting (delete+insert) hash table entries so we have to use
     hash_dump(). */
  if (second_target_expansion)
    {
# ifdef KMK /* turn on warnings here. */
      int save = warn_undefined_variables_flag;
      warn_undefined_variables_flag = 1;
# endif

      file_slot_0 = (struct file **) hash_dump (&files, 0, 0);
      file_end = file_slot_0 + files.ht_fill;
      for (file_slot = file_slot_0; file_slot < file_end; file_slot++)
        for (f = *file_slot; f != 0; f = f->prev)
          if (f->need_2nd_target_expansion)
            do_2nd_target_expansion (f);
      free (file_slot_0);

# ifdef KMK
      warn_undefined_variables_flag = save;
# endif

      /* Disable second target expansion now since we won't expand files
         entered after this point. (Saves CPU cycles in enter_file()). */
      second_target_expansion = 0;
    }
#endif /* CONFIG_WITH_2ND_TARGET_EXPANSION */

#ifdef CONFIG_WITH_INCLUDEDEP
  /* Process any queued includedep files.  Since includedep is supposed
     to be *simple* stuff, we can do this after the second target expansion
     and thereby save a little time.  */
  incdep_flush_and_term ();
#endif

  /* For every target that's not .SUFFIXES, expand its dependencies.
     We must use hash_dump (), because within this loop we might add new files
     to the table, possibly causing an in-situ table expansion.  */
  file_slot_0 = (struct file **) hash_dump (&files, 0, 0);
  file_end = file_slot_0 + files.ht_fill;
  for (file_slot = file_slot_0; file_slot < file_end; file_slot++)
    for (f = *file_slot; f != 0; f = f->prev)
      if (strcmp (f->name, ".SUFFIXES") != 0)
        expand_deps (f);
  free (file_slot_0);

  /* Now manage all the special targets.  */

  for (f = lookup_file (".PRECIOUS"); f != 0; f = f->prev)
    for (d = f->deps; d != 0; d = d->next)
      for (f2 = d->file; f2 != 0; f2 = f2->prev)
	f2->precious = 1;

  for (f = lookup_file (".LOW_RESOLUTION_TIME"); f != 0; f = f->prev)
    for (d = f->deps; d != 0; d = d->next)
      for (f2 = d->file; f2 != 0; f2 = f2->prev)
	f2->low_resolution_time = 1;

  for (f = lookup_file (".PHONY"); f != 0; f = f->prev)
    for (d = f->deps; d != 0; d = d->next)
      for (f2 = d->file; f2 != 0; f2 = f2->prev)
	{
	  /* Mark this file as phony nonexistent target.  */
	  f2->phony = 1;
          f2->is_target = 1;
	  f2->last_mtime = NONEXISTENT_MTIME;
	  f2->mtime_before_update = NONEXISTENT_MTIME;
	}

  for (f = lookup_file (".INTERMEDIATE"); f != 0; f = f->prev)
    /* Mark .INTERMEDIATE deps as intermediate files.  */
    for (d = f->deps; d != 0; d = d->next)
      for (f2 = d->file; f2 != 0; f2 = f2->prev)
        f2->intermediate = 1;
    /* .INTERMEDIATE with no deps does nothing.
       Marking all files as intermediates is useless since the goal targets
       would be deleted after they are built.  */

  for (f = lookup_file (".SECONDARY"); f != 0; f = f->prev)
    /* Mark .SECONDARY deps as both intermediate and secondary.  */
    if (f->deps)
      for (d = f->deps; d != 0; d = d->next)
        for (f2 = d->file; f2 != 0; f2 = f2->prev)
          f2->intermediate = f2->secondary = 1;
    /* .SECONDARY with no deps listed marks *all* files that way.  */
    else
      {
        all_secondary = 1;
        hash_map (&files, set_intermediate);
      }

  f = lookup_file (".EXPORT_ALL_VARIABLES");
  if (f != 0 && f->is_target)
    export_all_variables = 1;

  f = lookup_file (".IGNORE");
  if (f != 0 && f->is_target)
    {
      if (f->deps == 0)
	ignore_errors_flag = 1;
      else
	for (d = f->deps; d != 0; d = d->next)
	  for (f2 = d->file; f2 != 0; f2 = f2->prev)
	    f2->command_flags |= COMMANDS_NOERROR;
    }

  f = lookup_file (".SILENT");
  if (f != 0 && f->is_target)
    {
      if (f->deps == 0)
	silent_flag = 1;
      else
	for (d = f->deps; d != 0; d = d->next)
	  for (f2 = d->file; f2 != 0; f2 = f2->prev)
	    f2->command_flags |= COMMANDS_SILENT;
    }

  f = lookup_file (".NOTPARALLEL");
  if (f != 0 && f->is_target)
#ifndef CONFIG_WITH_EXTENDED_NOTPARALLEL
    not_parallel = 1;
#else /* CONFIG_WITH_EXTENDED_NOTPARALLEL */
    {
      if (f->deps == 0)
        {
          DB (DB_KMK, (_("not_parallel -1\n")));
          not_parallel = -1;
        }
      else
        for (d = f->deps; d != 0; d = d->next)
          for (f2 = d->file; f2 != 0; f2 = f2->prev)
            f2->command_flags |= COMMANDS_NOTPARALLEL;
    }
#endif /* CONFIG_WITH_EXTENDED_NOTPARALLEL */

#ifndef NO_MINUS_C_MINUS_O
  /* If .POSIX was defined, remove OUTPUT_OPTION to comply.  */
  /* This needs more work: what if the user sets this in the makefile?
  if (posix_pedantic)
    define_variable (STRING_SIZE_TUPLE("OUTPUT_OPTION"), "", o_default, 1);
  */
#endif

  /* Remember that we've done this. */
  snapped_deps = 1;
}

/* Set the `command_state' member of FILE and all its `also_make's.  */

void
set_command_state (struct file *file, enum cmd_state state)
{
  struct dep *d;

  file->command_state = state;

  for (d = file->also_make; d != 0; d = d->next)
    d->file->command_state = state;

#ifdef CONFIG_WITH_EXPLICIT_MULTITARGET
  if (file->multi_head)
    for (file = file->multi_head; file != 0; file = file->multi_next)
      file->command_state = state;
#endif
}

/* Convert an external file timestamp to internal form.  */

FILE_TIMESTAMP
file_timestamp_cons (const char *fname, time_t s, int ns)
{
  int offset = ORDINARY_MTIME_MIN + (FILE_TIMESTAMP_HI_RES ? ns : 0);
  FILE_TIMESTAMP product = (FILE_TIMESTAMP) s << FILE_TIMESTAMP_LO_BITS;
  FILE_TIMESTAMP ts = product + offset;

  if (! (s <= FILE_TIMESTAMP_S (ORDINARY_MTIME_MAX)
	 && product <= ts && ts <= ORDINARY_MTIME_MAX))
    {
      char buf[FILE_TIMESTAMP_PRINT_LEN_BOUND + 1];
      ts = s <= OLD_MTIME ? ORDINARY_MTIME_MIN : ORDINARY_MTIME_MAX;
      file_timestamp_sprintf (buf, ts);
      error (NILF, _("%s: Timestamp out of range; substituting %s"),
	     fname ? fname : _("Current time"), buf);
    }

  return ts;
}

/* Return the current time as a file timestamp, setting *RESOLUTION to
   its resolution.  */
FILE_TIMESTAMP
file_timestamp_now (int *resolution)
{
  int r;
  time_t s;
  int ns;

  /* Don't bother with high-resolution clocks if file timestamps have
     only one-second resolution.  The code below should work, but it's
     not worth the hassle of debugging it on hosts where it fails.  */
#if FILE_TIMESTAMP_HI_RES
# if HAVE_CLOCK_GETTIME && defined CLOCK_REALTIME
  {
    struct timespec timespec;
    if (clock_gettime (CLOCK_REALTIME, &timespec) == 0)
      {
	r = 1;
	s = timespec.tv_sec;
	ns = timespec.tv_nsec;
	goto got_time;
      }
  }
# endif
# if HAVE_GETTIMEOFDAY
  {
    struct timeval timeval;
    if (gettimeofday (&timeval, 0) == 0)
      {
	r = 1000;
	s = timeval.tv_sec;
	ns = timeval.tv_usec * 1000;
	goto got_time;
      }
  }
# endif
#endif

  r = 1000000000;
  s = time ((time_t *) 0);
  ns = 0;

#if FILE_TIMESTAMP_HI_RES
 got_time:
#endif
  *resolution = r;
  return file_timestamp_cons (0, s, ns);
}

/* Place into the buffer P a printable representation of the file
   timestamp TS.  */
void
file_timestamp_sprintf (char *p, FILE_TIMESTAMP ts)
{
  time_t t = FILE_TIMESTAMP_S (ts);
  struct tm *tm = localtime (&t);

  if (tm)
    sprintf (p, "%04d-%02d-%02d %02d:%02d:%02d",
	     tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
	     tm->tm_hour, tm->tm_min, tm->tm_sec);
  else if (t < 0)
    sprintf (p, "%ld", (long) t);
  else
    sprintf (p, "%lu", (unsigned long) t);
  p += strlen (p);

  /* Append nanoseconds as a fraction, but remove trailing zeros.  We don't
     know the actual timestamp resolution, since clock_getres applies only to
     local times, whereas this timestamp might come from a remote filesystem.
     So removing trailing zeros is the best guess that we can do.  */
  sprintf (p, ".%09d", FILE_TIMESTAMP_NS (ts));
  p += strlen (p) - 1;
  while (*p == '0')
    p--;
  p += *p != '.';

  *p = '\0';
}

/* Print the data base of files.  */

static void
print_file (const void *item)
{
  const struct file *f = item;
  struct dep *d;
  struct dep *ood = 0;

  putchar ('\n');
  if (!f->is_target)
    puts (_("# Not a target:"));
#ifdef CONFIG_WITH_EXPLICIT_MULTITARGET
  if (f->multi_head)
    {
      const struct file *f2;
      if (f->multi_head == f)
        {
          int multi_maybe = -1;
          assert (!f->multi_maybe);
          assert (!f->double_colon);

          printf ("%s", f->name);
          for (f2 = f->multi_next; f2 != 0; f2 = f2->multi_next)
            {
              printf (" %s%s", f2->multi_maybe != multi_maybe
                      ? f2->multi_maybe ? "+| " : "+ " : "",
                      f2->name);
              multi_maybe = f2->multi_maybe;
            }
          putchar (':');
        }
      else
        printf ("%s:%s", f->name, f->double_colon ? ":" : "");
    }
  else
#endif
    printf ("%s:%s", f->name, f->double_colon ? ":" : "");

  /* Print all normal dependencies; note any order-only deps.  */
  for (d = f->deps; d != 0; d = d->next)
    if (! d->ignore_mtime)
      printf (" %s", dep_name (d));
    else if (! ood)
      ood = d;

  /* Print order-only deps, if we have any.  */
  if (ood)
    {
      printf (" | %s", dep_name (ood));
      for (d = ood->next; d != 0; d = d->next)
        if (d->ignore_mtime)
          printf (" %s", dep_name (d));
    }

  putchar ('\n');

#ifdef CONFIG_WITH_EXPLICIT_MULTITARGET
  if (f->multi_head && f->multi_head != f)
    {
      const struct file *f2;
      fputs (_("#  In multi target list:"), stdout);
      for (f2 = f->multi_head; f2 != 0; f2 = f2->multi_next)
        printf (" %s%s", f2->name, f == f2 ? "(*)" : "");
      putchar ('\n');
      if (f->multi_maybe)
        puts (_("#  File is an optional multi target member."));
    }
#endif

  if (f->precious)
    puts (_("#  Precious file (prerequisite of .PRECIOUS)."));
  if (f->phony)
    puts (_("#  Phony target (prerequisite of .PHONY)."));
  if (f->cmd_target)
    puts (_("#  Command-line target."));
  if (f->dontcare)
    puts (_("#  A default, MAKEFILES, or -include/sinclude makefile."));
  puts (f->tried_implicit
        ? _("#  Implicit rule search has been done.")
        : _("#  Implicit rule search has not been done."));
  if (f->stem != 0)
    printf (_("#  Implicit/static pattern stem: `%s'\n"), f->stem);
  if (f->intermediate)
    puts (_("#  File is an intermediate prerequisite."));
  if (f->also_make != 0)
    {
      fputs (_("#  Also makes:"), stdout);
      for (d = f->also_make; d != 0; d = d->next)
	printf (" %s", dep_name (d));
      putchar ('\n');
    }
  if (f->last_mtime == UNKNOWN_MTIME)
    puts (_("#  Modification time never checked."));
  else if (f->last_mtime == NONEXISTENT_MTIME)
    puts (_("#  File does not exist."));
  else if (f->last_mtime == OLD_MTIME)
    puts (_("#  File is very old."));
  else
    {
      char buf[FILE_TIMESTAMP_PRINT_LEN_BOUND + 1];
      file_timestamp_sprintf (buf, f->last_mtime);
      printf (_("#  Last modified %s\n"), buf);
    }
  puts (f->updated
        ? _("#  File has been updated.") : _("#  File has not been updated."));
  switch (f->command_state)
    {
    case cs_running:
      puts (_("#  Commands currently running (THIS IS A BUG)."));
      break;
    case cs_deps_running:
      puts (_("#  Dependencies commands running (THIS IS A BUG)."));
      break;
    case cs_not_started:
    case cs_finished:
      switch (f->update_status)
	{
	case -1:
	  break;
	case 0:
	  puts (_("#  Successfully updated."));
	  break;
	case 1:
	  assert (question_flag);
	  puts (_("#  Needs to be updated (-q is set)."));
	  break;
	case 2:
	  puts (_("#  Failed to be updated."));
	  break;
	default:
	  puts (_("#  Invalid value in `update_status' member!"));
	  fflush (stdout);
	  fflush (stderr);
	  abort ();
	}
      break;
    default:
      puts (_("#  Invalid value in `command_state' member!"));
      fflush (stdout);
      fflush (stderr);
      abort ();
    }

  if (f->variables != 0)
    print_file_variables (f);

  if (f->cmds != 0)
    print_commands (f->cmds);

  if (f->prev)
    print_file ((const void *) f->prev);
}

void
print_file_data_base (void)
{
  puts (_("\n# Files"));

  hash_map (&files, print_file);

  fputs (_("\n# files hash-table stats:\n# "), stdout);
  hash_print_stats (&files, stdout);
}

/* Verify the integrity of the data base of files.  */

#define VERIFY_CACHED(_p,_n) \
    do{\
        if (_p->_n && _p->_n[0] && !strcache_iscached (_p->_n)) \
          printf ("%s: Field %s not cached: %s\n", _p->name, # _n, _p->_n); \
    }while(0)

static void
verify_file (const void *item)
{
  const struct file *f = item;
  const struct dep *d;

  VERIFY_CACHED (f, name);
  VERIFY_CACHED (f, hname);
  VERIFY_CACHED (f, vpath);
  VERIFY_CACHED (f, stem);

  /* Check the deps.  */
  for (d = f->deps; d != 0; d = d->next)
    {
      VERIFY_CACHED (d, name);
      VERIFY_CACHED (d, stem);
    }
}

void
verify_file_data_base (void)
{
  hash_map (&files, verify_file);
}

#define EXPANSION_INCREMENT(_l)  ((((_l) / 500) + 1) * 500)

char *
build_target_list (char *value)
{
  static unsigned long last_targ_count = 0;

  if (files.ht_fill != last_targ_count)
    {
      unsigned long max = EXPANSION_INCREMENT (strlen (value));
      unsigned long len;
      char *p;
      struct file **fp = (struct file **) files.ht_vec;
      struct file **end = &fp[files.ht_size];

      /* Make sure we have at least MAX bytes in the allocated buffer.  */
      value = xrealloc (value, max);

      p = value;
      len = 0;
      for (; fp < end; ++fp)
        if (!HASH_VACANT (*fp) && (*fp)->is_target)
          {
            struct file *f = *fp;
            int l = strlen (f->name);

            len += l + 1;
            if (len > max)
              {
                unsigned long off = p - value;

                max += EXPANSION_INCREMENT (l + 1);
                value = xrealloc (value, max);
                p = &value[off];
              }

            memcpy (p, f->name, l);
            p += l;
            *(p++) = ' ';
          }
      *(p-1) = '\0';

      last_targ_count = files.ht_fill;
    }

  return value;
}

void
init_hash_files (void)
{
#ifndef CONFIG_WITH_STRCACHE2
# ifdef KMK
  hash_init (&files, /*65535*/ 32755, file_hash_1, file_hash_2, file_hash_cmp);
# else
  hash_init (&files, 1000, file_hash_1, file_hash_2, file_hash_cmp);
# endif
#else
# ifdef KMK
  hash_init_strcached (&files, 32755, &file_strcache,
                       offsetof (struct file, hname));
# else
  hash_init_strcached (&files, 1000, &file_strcache,
                       offsetof (struct file, hname));
# endif
#endif
}

/* EOF */
