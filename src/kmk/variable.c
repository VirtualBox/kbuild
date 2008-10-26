/* Internals of variables for GNU Make.
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
#include "rule.h"
#ifdef WINDOWS32
#include "pathstuff.h"
#endif
#include "hash.h"
#ifdef KMK
# include "kbuild.h"
#endif
#ifdef CONFIG_WITH_STRCACHE2
# include <stddef.h>
#endif

/* Chain of all pattern-specific variables.  */

static struct pattern_var *pattern_vars;

/* Pointer to last struct in the chain, so we can add onto the end.  */

static struct pattern_var *last_pattern_var;

/* Create a new pattern-specific variable struct.  */

struct pattern_var *
create_pattern_var (const char *target, const char *suffix)
{
  register struct pattern_var *p = xmalloc (sizeof (struct pattern_var));

  if (last_pattern_var != 0)
    last_pattern_var->next = p;
  else
    pattern_vars = p;
  last_pattern_var = p;
  p->next = 0;

  p->target = target;
  p->len = strlen (target);
  p->suffix = suffix + 1;

  return p;
}

/* Look up a target in the pattern-specific variable list.  */

static struct pattern_var *
lookup_pattern_var (struct pattern_var *start, const char *target)
{
  struct pattern_var *p;
  unsigned int targlen = strlen(target);

  for (p = start ? start->next : pattern_vars; p != 0; p = p->next)
    {
      const char *stem;
      unsigned int stemlen;

      if (p->len > targlen)
        /* It can't possibly match.  */
        continue;

      /* From the lengths of the filename and the pattern parts,
         find the stem: the part of the filename that matches the %.  */
      stem = target + (p->suffix - p->target - 1);
      stemlen = targlen - p->len + 1;

      /* Compare the text in the pattern before the stem, if any.  */
      if (stem > target && !strneq (p->target, target, stem - target))
        continue;

      /* Compare the text in the pattern after the stem, if any.
         We could test simply using streq, but this way we compare the
         first two characters immediately.  This saves time in the very
         common case where the first character matches because it is a
         period.  */
      if (*p->suffix == stem[stemlen]
          && (*p->suffix == '\0' || streq (&p->suffix[1], &stem[stemlen+1])))
        break;
    }

  return p;
}

#ifdef CONFIG_WITH_STRCACHE2
struct strcache2 variable_strcache;
#endif

/* Hash table of all global variable definitions.  */

#ifndef CONFIG_WITH_STRCACHE2
static unsigned long
variable_hash_1 (const void *keyv)
{
  struct variable const *key = (struct variable const *) keyv;
  return_STRING_N_HASH_1 (key->name, key->length);
}

static unsigned long
variable_hash_2 (const void *keyv)
{
  struct variable const *key = (struct variable const *) keyv;
  return_STRING_N_HASH_2 (key->name, key->length);
}

static int
variable_hash_cmp (const void *xv, const void *yv)
{
  struct variable const *x = (struct variable const *) xv;
  struct variable const *y = (struct variable const *) yv;
  int result = x->length - y->length;
  if (result)
    return result;

  return_STRING_N_COMPARE (x->name, y->name, x->length);
}
#endif /* !CONFIG_WITH_STRCACHE2 */

#ifndef	VARIABLE_BUCKETS
# ifdef KMK /* Move to Makefile.kmk? (insanely high, but wtf, it gets the collitions down) */
#  define VARIABLE_BUCKETS		65535
# else  /*!KMK*/
#define VARIABLE_BUCKETS		523
# endif /*!KMK*/
#endif
#ifndef	PERFILE_VARIABLE_BUCKETS
# ifdef KMK /* Move to Makefile.kmk? */
#  define PERFILE_VARIABLE_BUCKETS	127
# else
#define	PERFILE_VARIABLE_BUCKETS	23
# endif
#endif
#ifndef	SMALL_SCOPE_VARIABLE_BUCKETS
# ifdef KMK /* Move to Makefile.kmk? */
#  define SMALL_SCOPE_VARIABLE_BUCKETS  63
# else
#define	SMALL_SCOPE_VARIABLE_BUCKETS	13
# endif
#endif

static struct variable_set global_variable_set;
static struct variable_set_list global_setlist
  = { 0, &global_variable_set };
struct variable_set_list *current_variable_set_list = &global_setlist;

/* Implement variables.  */

void
init_hash_global_variable_set (void)
{
#ifndef CONFIG_WITH_STRCACHE2
  hash_init (&global_variable_set.table, VARIABLE_BUCKETS,
	     variable_hash_1, variable_hash_2, variable_hash_cmp);
#else  /* CONFIG_WITH_STRCACHE2 */
  strcache2_init (&variable_strcache, "variable", 65536, 0, 0, 0);
  hash_init_strcached (&global_variable_set.table, VARIABLE_BUCKETS,
                       &variable_strcache, offsetof (struct variable, name));
#endif /* CONFIG_WITH_STRCACHE2 */
}

/* Define variable named NAME with value VALUE in SET.  VALUE is copied.
   LENGTH is the length of NAME, which does not need to be null-terminated.
   ORIGIN specifies the origin of the variable (makefile, command line
   or environment).
   If RECURSIVE is nonzero a flag is set in the variable saying
   that it should be recursively re-expanded.  */

#ifdef CONFIG_WITH_VALUE_LENGTH
struct variable *
define_variable_in_set (const char *name, unsigned int length,
                        const char *value, unsigned int value_len,
                        int duplicate_value, enum variable_origin origin,
                        int recursive, struct variable_set *set,
                        const struct floc *flocp)
#else
struct variable *
define_variable_in_set (const char *name, unsigned int length,
                        const char *value, enum variable_origin origin,
                        int recursive, struct variable_set *set,
                        const struct floc *flocp)
#endif
{
  struct variable *v;
  struct variable **var_slot;
  struct variable var_key;

  if (set == NULL)
    set = &global_variable_set;

#ifndef CONFIG_WITH_STRCACHE2
  var_key.name = (char *) name;
  var_key.length = length;
  var_slot = (struct variable **) hash_find_slot (&set->table, &var_key);

  if (env_overrides && origin == o_env)
    origin = o_env_override;

  v = *var_slot;
#else  /* CONFIG_WITH_STRCACHE2 */
  var_key.name = name = strcache2_add (&variable_strcache, name, length);
  var_key.length = length;
  if (   set != &global_variable_set
      || !(v = strcache2_get_user_val (&variable_strcache, var_key.name)))
    {
      var_slot = (struct variable **) hash_find_slot_strcached (&set->table, &var_key);
      v = *var_slot;
    }
  else
    {
      assert (!v || (v->name == name && !HASH_VACANT (v)));
      var_slot = 0;
    }
#endif /* CONFIG_WITH_STRCACHE2 */
  if (! HASH_VACANT (v))
    {
      if (env_overrides && v->origin == o_env)
	/* V came from in the environment.  Since it was defined
	   before the switches were parsed, it wasn't affected by -e.  */
	v->origin = o_env_override;

      /* A variable of this name is already defined.
	 If the old definition is from a stronger source
	 than this one, don't redefine it.  */
      if ((int) origin >= (int) v->origin)
	{
#ifdef CONFIG_WITH_VALUE_LENGTH
          if (value_len == ~0U)
            value_len = strlen (value);
          else
            assert (value_len == strlen (value));
          if (!duplicate_value || duplicate_value == -1)
            {
# ifdef CONFIG_WITH_RDONLY_VARIABLE_VALUE
              if (v->value != 0 && !v->rdonly_val)
                  free (v->value);
              v->rdonly_val = duplicate_value == -1;
              v->value = (char *)value;
              v->value_alloc_len = 0;
# else
              if (v->value != 0)
                free (v->value);
              v->value = (char *)value;
              v->value_alloc_len = value_len + 1;
# endif
            }
          else
            {
              if ((unsigned int)v->value_alloc_len <= value_len)
                {
# ifdef CONFIG_WITH_RDONLY_VARIABLE_VALUE
                  if (v->rdonly_val)
                    v->rdonly_val = 0;
                  else
# endif
                    free (v->value);
                  v->value_alloc_len = (value_len + 0x40) & ~0x3f;
                  v->value = xmalloc (v->value_alloc_len);
                }
              memcpy (v->value, value, value_len + 1);
            }
          v->value_length = value_len;
#else  /* !CONFIG_WITH_VALUE_LENGTH */
          if (v->value != 0)
            free (v->value);
	  v->value = xstrdup (value);
#endif /* !CONFIG_WITH_VALUE_LENGTH */
          if (flocp != 0)
            v->fileinfo = *flocp;
          else
            v->fileinfo.filenm = 0;
	  v->origin = origin;
	  v->recursive = recursive;
	}
      return v;
    }

  /* Create a new variable definition and add it to the hash table.  */

#ifndef CONFIG_WITH_ALLOC_CACHES
  v = xmalloc (sizeof (struct variable));
#else
  v = alloccache_alloc (&variable_cache);
#endif
#ifndef CONFIG_WITH_STRCACHE2
  v->name = savestring (name, length);
#else
  v->name = name; /* already cached. */
#endif
  v->length = length;
  hash_insert_at (&set->table, v, var_slot);
#ifdef CONFIG_WITH_VALUE_LENGTH
  if (value_len == ~0U)
    value_len = strlen (value);
  else
    assert (value_len == strlen (value));
  v->value_length = value_len;
  if (!duplicate_value || duplicate_value == -1)
    {
# ifdef CONFIG_WITH_RDONLY_VARIABLE_VALUE
      v->rdonly_val = duplicate_value == -1;
      v->value_alloc_len = v->rdonly_val ? 0 : value_len + 1;
# endif
      v->value = (char *)value;
    }
  else
    {
# ifdef CONFIG_WITH_RDONLY_VARIABLE_VALUE
      v->rdonly_val = 0;
# endif
      v->value_alloc_len = (value_len + 32) & ~31;
      v->value = xmalloc (v->value_alloc_len);
      memcpy (v->value, value, value_len + 1);
    }
#else
  v->value = xstrdup (value);
#endif
  if (flocp != 0)
    v->fileinfo = *flocp;
  else
    v->fileinfo.filenm = 0;
  v->origin = origin;
  v->recursive = recursive;
  v->special = 0;
  v->expanding = 0;
  v->exp_count = 0;
  v->per_target = 0;
  v->append = 0;
  v->export = v_default;

  v->exportable = 1;
  if (*name != '_' && (*name < 'A' || *name > 'Z')
      && (*name < 'a' || *name > 'z'))
    v->exportable = 0;
  else
    {
      for (++name; *name != '\0'; ++name)
        if (*name != '_' && (*name < 'a' || *name > 'z')
            && (*name < 'A' || *name > 'Z') && !ISDIGIT(*name))
          break;

      if (*name != '\0')
        v->exportable = 0;
    }

#ifdef CONFIG_WITH_STRCACHE2
  /* If it's the global set, remember the variable. */
  if (set == &global_variable_set)
    strcache2_set_user_val (&variable_strcache, v->name, v);
#endif
  return v;
}

/* If the variable passed in is "special", handle its special nature.
   Currently there are two such variables, both used for introspection:
   .VARIABLES expands to a list of all the variables defined in this instance
   of make.
   .TARGETS expands to a list of all the targets defined in this
   instance of make.
   Returns the variable reference passed in.  */

#define EXPANSION_INCREMENT(_l)  ((((_l) / 500) + 1) * 500)

static struct variable *
handle_special_var (struct variable *var)
{
  static unsigned long last_var_count = 0;


  /* This one actually turns out to be very hard, due to the way the parser
     records targets.  The way it works is that target information is collected
     internally until make knows the target is completely specified.  It unitl
     it sees that some new construct (a new target or variable) is defined that
     it knows the previous one is done.  In short, this means that if you do
     this:

       all:

       TARGS := $(.TARGETS)

     then $(TARGS) won't contain "all", because it's not until after the
     variable is created that the previous target is completed.

     Changing this would be a major pain.  I think a less complex way to do it
     would be to pre-define the target files as soon as the first line is
     parsed, then come back and do the rest of the definition as now.  That
     would allow $(.TARGETS) to be correct without a major change to the way
     the parser works.

  if (streq (var->name, ".TARGETS"))
    var->value = build_target_list (var->value);
  else
  */

  if (streq (var->name, ".VARIABLES")
      && global_variable_set.table.ht_fill != last_var_count)
    {
#ifndef CONFIG_WITH_VALUE_LENGTH
      unsigned long max = EXPANSION_INCREMENT (strlen (var->value));
#else
      unsigned long max = EXPANSION_INCREMENT (var->value_length);
#endif
      unsigned long len;
      char *p;
      struct variable **vp = (struct variable **) global_variable_set.table.ht_vec;
      struct variable **end = &vp[global_variable_set.table.ht_size];

      /* Make sure we have at least MAX bytes in the allocated buffer.  */
      var->value = xrealloc (var->value, max);

      /* Walk through the hash of variables, constructing a list of names.  */
      p = var->value;
      len = 0;
      for (; vp < end; ++vp)
        if (!HASH_VACANT (*vp))
          {
            struct variable *v = *vp;
            int l = v->length;

            len += l + 1;
            if (len > max)
              {
                unsigned long off = p - var->value;

                max += EXPANSION_INCREMENT (l + 1);
                var->value = xrealloc (var->value, max);
                p = &var->value[off];
              }

            memcpy (p, v->name, l);
            p += l;
            *(p++) = ' ';
          }
      *(p-1) = '\0';
#ifdef CONFIG_WITH_VALUE_LENGTH
      var->value_length = p - var->value - 1;
      var->value_alloc_len = max;
#endif

      /* Remember how many variables are in our current count.  Since we never
         remove variables from the list, this is a reliable way to know whether
         the list is up to date or needs to be recomputed.  */

      last_var_count = global_variable_set.table.ht_fill;
    }

  return var;
}


#ifdef KMK /* bird: speed */
MY_INLINE struct variable *
lookup_cached_variable (const char *name)
{
  const struct variable_set_list *setlist = current_variable_set_list;
  struct hash_table *ht;
  unsigned int hash_1;
  unsigned int hash_2;
  unsigned int idx;
  struct variable *v;

  /* first set, first entry, both unrolled. */

  if (setlist->set == &global_variable_set)
    {
      v = (struct variable *) strcache2_get_user_val (&variable_strcache, name);
      if (MY_PREDICT_TRUE (v))
        return MY_PREDICT_FALSE (v->special) ? handle_special_var (v) : v;
      assert (setlist->next == 0);
      return 0;
    }

  hash_1 = strcache2_calc_ptr_hash (&variable_strcache, name);
  ht = &setlist->set->table;
  MAKE_STATS (ht->ht_lookups++);
  idx = hash_1 & (ht->ht_size - 1);
  v = ht->ht_vec[idx];
  if (v != 0)
    {
      if (   (void *)v != hash_deleted_item
          && v->name == name)
        return MY_PREDICT_FALSE (v->special) ? handle_special_var (v) : v;

      /* the rest of the loop  */
      hash_2 = strcache2_get_hash (&variable_strcache, name) | 1;
      for (;;)
        {
          idx += hash_2;
          idx &= (ht->ht_size - 1);
          v = (struct variable *) ht->ht_vec[idx];
          MAKE_STATS (ht->ht_collisions++); /* there are hardly any deletions, so don't bother with not counting deleted clashes. */

          if (v == 0)
            break;
          if (   (void *)v != hash_deleted_item
              && v->name == name)
            return MY_PREDICT_FALSE (v->special) ? handle_special_var (v) : v;
        } /* inner collision loop */
    }
  else
    hash_2 = strcache2_get_hash (&variable_strcache, name) | 1;


  /* The other sets, if any. */

  setlist = setlist->next;
  while (setlist)
    {
      if (setlist->set == &global_variable_set)
        {
          v = (struct variable *) strcache2_get_user_val (&variable_strcache, name);
          if (MY_PREDICT_TRUE (v))
            return MY_PREDICT_FALSE (v->special) ? handle_special_var (v) : v;
          assert (setlist->next == 0);
          return 0;
        }

      /* first iteration unrolled */
      ht = &setlist->set->table;
      MAKE_STATS (ht->ht_lookups++);
      idx = hash_1 & (ht->ht_size - 1);
      v = ht->ht_vec[idx];
      if (v != 0)
        {
          if (   (void *)v != hash_deleted_item
              && v->name == name)
            return MY_PREDICT_FALSE (v->special) ? handle_special_var (v) : v;

          /* the rest of the loop  */
          for (;;)
            {
              idx += hash_2;
              idx &= (ht->ht_size - 1);
              v = (struct variable *) ht->ht_vec[idx];
              MAKE_STATS (ht->ht_collisions++); /* see reason above */

              if (v == 0)
                break;
              if (   (void *)v != hash_deleted_item
                  && v->name == name)
                return MY_PREDICT_FALSE (v->special) ? handle_special_var (v) : v;
            } /* inner collision loop */
        }

      /* next */
      setlist = setlist->next;
    }

  return 0;
}

# ifndef NDEBUG
struct variable *
lookup_variable_for_assert (const char *name, unsigned int length)
{
  const struct variable_set_list *setlist;
  struct variable var_key;
  var_key.name = name;
  var_key.length = length;

  for (setlist = current_variable_set_list;
       setlist != 0; setlist = setlist->next)
    {
      struct variable *v;
      v = (struct variable *) hash_find_item_strcached (&setlist->set->table, &var_key);
      if (v)
        return MY_PREDICT_FALSE (v->special) ? handle_special_var (v) : v;
    }
  return 0;
}
# endif  /* !NDEBUG */
#endif /* KMK - need for speed */

/* Lookup a variable whose name is a string starting at NAME
   and with LENGTH chars.  NAME need not be null-terminated.
   Returns address of the `struct variable' containing all info
   on the variable, or nil if no such variable is defined.  */

struct variable *
lookup_variable (const char *name, unsigned int length)
{
#ifndef KMK
  const struct variable_set_list *setlist;
  struct variable var_key;
#else /* KMK */
  struct variable *v;
#endif /* KMK */
#ifdef CONFIG_WITH_STRCACHE2
  const char *cached_name;

  /* lookup the name in the string case, if it's not there it won't
     be in any of the sets either. */
  cached_name = strcache2_lookup (&variable_strcache, name, length);
  if (!cached_name)
    return NULL;
  name = cached_name;
#endif /* CONFIG_WITH_STRCACHE2 */
#ifndef KMK

  var_key.name = (char *) name;
  var_key.length = length;

  for (setlist = current_variable_set_list;
       setlist != 0; setlist = setlist->next)
    {
      const struct variable_set *set = setlist->set;
      struct variable *v;

# ifndef CONFIG_WITH_STRCACHE2
      v = (struct variable *) hash_find_item ((struct hash_table *) &set->table, &var_key);
# else  /* CONFIG_WITH_STRCACHE2 */
      v = (struct variable *) hash_find_item_strcached ((struct hash_table *) &set->table, &var_key);
# endif /* CONFIG_WITH_STRCACHE2 */
      if (v)
	return v->special ? handle_special_var (v) : v;
    }

#else  /* KMK - need for speed */

  v = lookup_cached_variable (name);
  assert (lookup_variable_for_assert(name, length) == v);
#ifdef VMS
  if (v)
#endif
    return v;
#endif /* KMK - need for speed */
#ifdef VMS
  /* since we don't read envp[] on startup, try to get the
     variable via getenv() here.  */
  {
    char *vname = alloca (length + 1);
    char *value;
    strncpy (vname, name, length);
    vname[length] = 0;
    value = getenv (vname);
    if (value != 0)
      {
        char *sptr;
        int scnt;

        sptr = value;
        scnt = 0;

        while ((sptr = strchr (sptr, '$')))
          {
            scnt++;
            sptr++;
          }

        if (scnt > 0)
          {
            char *nvalue;
            char *nptr;

            nvalue = alloca (strlen (value) + scnt + 1);
            sptr = value;
            nptr = nvalue;

            while (*sptr)
              {
                if (*sptr == '$')
                  {
                    *nptr++ = '$';
                    *nptr++ = '$';
                  }
                else
                  {
                    *nptr++ = *sptr;
                  }
                sptr++;
              }

            *nptr = '\0';
            return define_variable (vname, length, nvalue, o_env, 1);

          }

        return define_variable (vname, length, value, o_env, 1);
      }
  }
#endif /* VMS */

#if !defined (KMK) || defined(VMS)
  return 0;
#endif
}

/* Lookup a variable whose name is a string starting at NAME
   and with LENGTH chars in set SET.  NAME need not be null-terminated.
   Returns address of the `struct variable' containing all info
   on the variable, or nil if no such variable is defined.  */

struct variable *
lookup_variable_in_set (const char *name, unsigned int length,
                        const struct variable_set *set)
{
  struct variable var_key;
#ifndef CONFIG_WITH_STRCACHE2
  var_key.name = (char *) name;
  var_key.length = length;

  return (struct variable *) hash_find_item ((struct hash_table *) &set->table, &var_key);
#else  /* CONFIG_WITH_STRCACHE2 */
  const char *cached_name;

  /* lookup the name in the string case, if it's not there it won't
     be in any of the sets either.  Optimize lookups in the global set. */
  cached_name = strcache2_lookup(&variable_strcache, name, length);
  if (!cached_name)
    return NULL;

  if (set == &global_variable_set)
    {
      struct variable *v;
      v = strcache2_get_user_val (&variable_strcache, cached_name);
      assert (!v || v->name == cached_name);
      return v;
    }

  var_key.name = cached_name;
  var_key.length = length;

  return (struct variable *) hash_find_item_strcached (
    (struct hash_table *) &set->table, &var_key);
#endif /* CONFIG_WITH_STRCACHE2 */
}

/* Initialize FILE's variable set list.  If FILE already has a variable set
   list, the topmost variable set is left intact, but the the rest of the
   chain is replaced with FILE->parent's setlist.  If FILE is a double-colon
   rule, then we will use the "root" double-colon target's variable set as the
   parent of FILE's variable set.

   If we're READING a makefile, don't do the pattern variable search now,
   since the pattern variable might not have been defined yet.  */

void
initialize_file_variables (struct file *file, int reading)
{
  struct variable_set_list *l = file->variables;

  if (l == 0)
    {
#ifndef CONFIG_WITH_ALLOC_CACHES
      l = (struct variable_set_list *)
	xmalloc (sizeof (struct variable_set_list));
      l->set = xmalloc (sizeof (struct variable_set));
#else  /* CONFIG_WITH_ALLOC_CACHES */
      l = (struct variable_set_list *)
        alloccache_alloc (&variable_set_list_cache);
      l->set = (struct variable_set *)
        alloccache_alloc (&variable_set_cache);
#endif /* CONFIG_WITH_ALLOC_CACHES */
#ifndef CONFIG_WITH_STRCACHE2
      hash_init (&l->set->table, PERFILE_VARIABLE_BUCKETS,
                 variable_hash_1, variable_hash_2, variable_hash_cmp);
#else  /* CONFIG_WITH_STRCACHE2 */
      hash_init_strcached (&l->set->table, PERFILE_VARIABLE_BUCKETS,
                           &variable_strcache, offsetof (struct variable, name));
#endif /* CONFIG_WITH_STRCACHE2 */
      file->variables = l;
    }

  /* If this is a double-colon, then our "parent" is the "root" target for
     this double-colon rule.  Since that rule has the same name, parent,
     etc. we can just use its variables as the "next" for ours.  */

  if (file->double_colon && file->double_colon != file)
    {
      initialize_file_variables (file->double_colon, reading);
      l->next = file->double_colon->variables;
      return;
    }

  if (file->parent == 0)
    l->next = &global_setlist;
  else
    {
      initialize_file_variables (file->parent, reading);
      l->next = file->parent->variables;
    }

  /* If we're not reading makefiles and we haven't looked yet, see if
     we can find pattern variables for this target.  */

  if (!reading && !file->pat_searched)
    {
      struct pattern_var *p;

      p = lookup_pattern_var (0, file->name);
      if (p != 0)
        {
          struct variable_set_list *global = current_variable_set_list;

          /* We found at least one.  Set up a new variable set to accumulate
             all the pattern variables that match this target.  */

          file->pat_variables = create_new_variable_set ();
          current_variable_set_list = file->pat_variables;

          do
            {
              /* We found one, so insert it into the set.  */

              struct variable *v;

              if (p->variable.flavor == f_simple)
                {
                  v = define_variable_loc (
                    p->variable.name, strlen (p->variable.name),
                    p->variable.value, p->variable.origin,
                    0, &p->variable.fileinfo);

                  v->flavor = f_simple;
                }
              else
                {
#ifndef CONFIG_WITH_VALUE_LENGTH
                  v = do_variable_definition (
                    &p->variable.fileinfo, p->variable.name,
                    p->variable.value, p->variable.origin,
                    p->variable.flavor, 1);
#else
                  v = do_variable_definition_2 (
                    &p->variable.fileinfo, p->variable.name,
                    p->variable.value, p->variable.value_length, 0, 0,
                    p->variable.origin, p->variable.flavor, 1);
#endif
                }

              /* Also mark it as a per-target and copy export status. */
              v->per_target = p->variable.per_target;
              v->export = p->variable.export;
            }
          while ((p = lookup_pattern_var (p, file->name)) != 0);

          current_variable_set_list = global;
        }
      file->pat_searched = 1;
    }

  /* If we have a pattern variable match, set it up.  */

  if (file->pat_variables != 0)
    {
      file->pat_variables->next = l->next;
      l->next = file->pat_variables;
    }
}

/* Pop the top set off the current variable set list,
   and free all its storage.  */

struct variable_set_list *
create_new_variable_set (void)
{
  register struct variable_set_list *setlist;
  register struct variable_set *set;

#ifndef CONFIG_WITH_ALLOC_CACHES
  set = xmalloc (sizeof (struct variable_set));
#else
  set = (struct variable_set *) alloccache_alloc (&variable_set_cache);
#endif
#ifndef CONFIG_WITH_STRCACHE2
  hash_init (&set->table, SMALL_SCOPE_VARIABLE_BUCKETS,
	     variable_hash_1, variable_hash_2, variable_hash_cmp);
#else  /* CONFIG_WITH_STRCACHE2 */
  hash_init_strcached (&set->table, SMALL_SCOPE_VARIABLE_BUCKETS,
                       &variable_strcache, offsetof (struct variable, name));
#endif /* CONFIG_WITH_STRCACHE2 */

#ifndef CONFIG_WITH_ALLOC_CACHES
  setlist = (struct variable_set_list *)
    xmalloc (sizeof (struct variable_set_list));
#else
  setlist = (struct variable_set_list *)
    alloccache_alloc (&variable_set_list_cache);
#endif
  setlist->set = set;
  setlist->next = current_variable_set_list;

  return setlist;
}

static void
free_variable_name_and_value (const void *item)
{
  struct variable *v = (struct variable *) item;
#ifndef CONFIG_WITH_STRCACHE2
  free (v->name);
#endif
  free (v->value);
}

void
free_variable_set (struct variable_set_list *list)
{
  hash_map (&list->set->table, free_variable_name_and_value);
#ifndef CONFIG_WITH_ALLOC_CACHES
  hash_free (&list->set->table, 1);
  free (list->set);
  free (list);
#else
  hash_free_cached (&list->set->table, 1, &variable_cache);
  alloccache_free (&variable_set_cache, list->set);
  alloccache_free (&variable_set_list_cache, list);
#endif
}

/* Create a new variable set and push it on the current setlist.
   If we're pushing a global scope (that is, the current scope is the global
   scope) then we need to "push" it the other way: file variable sets point
   directly to the global_setlist so we need to replace that with the new one.
 */

struct variable_set_list *
push_new_variable_scope (void)
{
  current_variable_set_list = create_new_variable_set();
  if (current_variable_set_list->next == &global_setlist)
    {
      /* It was the global, so instead of new -> &global we want to replace
         &global with the new one and have &global -> new, with current still
         pointing to &global  */
      struct variable_set *set = current_variable_set_list->set;
      current_variable_set_list->set = global_setlist.set;
      global_setlist.set = set;
      current_variable_set_list->next = global_setlist.next;
      global_setlist.next = current_variable_set_list;
      current_variable_set_list = &global_setlist;
    }
  return (current_variable_set_list);
}

void
pop_variable_scope (void)
{
  struct variable_set_list *setlist;
  struct variable_set *set;

  /* Can't call this if there's no scope to pop!  */
  assert(current_variable_set_list->next != NULL);

  if (current_variable_set_list != &global_setlist)
    {
      /* We're not pointing to the global setlist, so pop this one.  */
      setlist = current_variable_set_list;
      set = setlist->set;
      current_variable_set_list = setlist->next;
    }
  else
    {
      /* This set is the one in the global_setlist, but there is another global
         set beyond that.  We want to copy that set to global_setlist, then
         delete what used to be in global_setlist.  */
      setlist = global_setlist.next;
      set = global_setlist.set;
      global_setlist.set = setlist->set;
      global_setlist.next = setlist->next;
    }

  /* Free the one we no longer need.  */
#ifndef CONFIG_WITH_ALLOC_CACHES
  free (setlist);
  hash_map (&set->table, free_variable_name_and_value);
  hash_free (&set->table, 1);
  free (set);
#else
  alloccache_free (&variable_set_list_cache, setlist);
  hash_map (&set->table, free_variable_name_and_value);
  hash_free_cached (&set->table, 1, &variable_cache);
  alloccache_free (&variable_set_cache, set);
#endif
}

/* Merge FROM_SET into TO_SET, freeing unused storage in FROM_SET.  */

static void
merge_variable_sets (struct variable_set *to_set,
                     struct variable_set *from_set)
{
  struct variable **from_var_slot = (struct variable **) from_set->table.ht_vec;
  struct variable **from_var_end = from_var_slot + from_set->table.ht_size;

  for ( ; from_var_slot < from_var_end; from_var_slot++)
    if (! HASH_VACANT (*from_var_slot))
      {
	struct variable *from_var = *from_var_slot;
	struct variable **to_var_slot
#ifndef CONFIG_WITH_STRCACHE2
	  = (struct variable **) hash_find_slot (&to_set->table, *from_var_slot);
#else  /* CONFIG_WITH_STRCACHE2 */
	  = (struct variable **) hash_find_slot_strcached (&to_set->table,
                                                           *from_var_slot);
#endif /* CONFIG_WITH_STRCACHE2 */
	if (HASH_VACANT (*to_var_slot))
	  hash_insert_at (&to_set->table, from_var, to_var_slot);
	else
	  {
	    /* GKM FIXME: delete in from_set->table */
	    free (from_var->value);
	    free (from_var);
	  }
      }
}

/* Merge SETLIST1 into SETLIST0, freeing unused storage in SETLIST1.  */

void
merge_variable_set_lists (struct variable_set_list **setlist0,
                          struct variable_set_list *setlist1)
{
  struct variable_set_list *to = *setlist0;
  struct variable_set_list *last0 = 0;

  /* If there's nothing to merge, stop now.  */
  if (!setlist1)
    return;

  /* This loop relies on the fact that all setlists terminate with the global
     setlist (before NULL).  If that's not true, arguably we SHOULD die.  */
  if (to)
    while (setlist1 != &global_setlist && to != &global_setlist)
      {
        struct variable_set_list *from = setlist1;
        setlist1 = setlist1->next;

        merge_variable_sets (to->set, from->set);

        last0 = to;
        to = to->next;
      }

  if (setlist1 != &global_setlist)
    {
      if (last0 == 0)
	*setlist0 = setlist1;
      else
	last0->next = setlist1;
    }
}

/* Define the automatic variables, and record the addresses
   of their structures so we can change their values quickly.  */

void
define_automatic_variables (void)
{
#if defined(WINDOWS32) || defined(__EMX__)
  extern char* default_shell;
#else
  extern char default_shell[];
#endif
  register struct variable *v;
#ifndef KMK
  char buf[200];
#else
  char buf[1024];
  const char *val;
  struct variable *envvar1;
  struct variable *envvar2;
#endif

  sprintf (buf, "%u", makelevel);
  (void) define_variable (MAKELEVEL_NAME, MAKELEVEL_LENGTH, buf, o_env, 0);

  sprintf (buf, "%s%s%s",
	   version_string,
	   (remote_description == 0 || remote_description[0] == '\0')
	   ? "" : "-",
	   (remote_description == 0 || remote_description[0] == '\0')
	   ? "" : remote_description);
#ifndef KMK
  (void) define_variable ("MAKE_VERSION", 12, buf, o_default, 0);
#else /* KMK */

  /* Define KMK_VERSION to indicate kMk. */
  (void) define_variable ("KMK_VERSION", 11, buf, o_default, 0);

  /* Define KBUILD_VERSION* */
  sprintf (buf, "%d", KBUILD_VERSION_MAJOR);
  define_variable ("KBUILD_VERSION_MAJOR", sizeof ("KBUILD_VERSION_MAJOR") - 1,
                   buf, o_default, 0);
  sprintf (buf, "%d", KBUILD_VERSION_MINOR);
  define_variable ("KBUILD_VERSION_MINOR", sizeof("KBUILD_VERSION_MINOR") - 1,
                   buf, o_default, 0);
  sprintf (buf, "%d", KBUILD_VERSION_PATCH);
  define_variable ("KBUILD_VERSION_PATCH", sizeof ("KBUILD_VERSION_PATCH") - 1,
                   buf, o_default, 0);
  sprintf (buf, "%d", KBUILD_SVN_REV);
  define_variable ("KBUILD_KMK_REVISION", sizeof ("KBUILD_KMK_REVISION") - 1,
                   buf, o_default, 0);

  sprintf (buf, "%d.%d.%d-r%d", KBUILD_VERSION_MAJOR, KBUILD_VERSION_MINOR,
           KBUILD_VERSION_PATCH, KBUILD_SVN_REV);
  define_variable ("KBUILD_VERSION", sizeof ("KBUILD_VERSION") - 1,
                   buf, o_default, 0);

  /* The host defaults. The BUILD_* stuff will be replaced by KBUILD_* soon. */
  envvar1 = lookup_variable (STRING_SIZE_TUPLE ("KBUILD_HOST"));
  envvar2 = lookup_variable (STRING_SIZE_TUPLE ("BUILD_PLATFORM"));
  val = envvar1 ? envvar1->value : envvar2 ? envvar2->value : KBUILD_HOST;
  if (envvar1 && envvar2 && strcmp (envvar1->value, envvar2->value))
    error (NULL, _("KBUILD_HOST and BUILD_PLATFORM differs, using KBUILD_HOST=%s."), val);
  if (!envvar1)
    define_variable ("KBUILD_HOST", sizeof ("KBUILD_HOST") - 1,
                     val, o_default, 0);
  if (!envvar2)
    define_variable ("BUILD_PLATFORM", sizeof ("BUILD_PLATFORM") - 1,
                     val, o_default, 0);

  envvar1 = lookup_variable (STRING_SIZE_TUPLE ("KBUILD_HOST_ARCH"));
  envvar2 = lookup_variable (STRING_SIZE_TUPLE ("BUILD_PLATFORM_ARCH"));
  val = envvar1 ? envvar1->value : envvar2 ? envvar2->value : KBUILD_HOST_ARCH;
  if (envvar1 && envvar2 && strcmp (envvar1->value, envvar2->value))
    error (NULL, _("KBUILD_HOST_ARCH and BUILD_PLATFORM_ARCH differs, using KBUILD_HOST_ARCH=%s."), val);
  if (!envvar1)
    define_variable ("KBUILD_HOST_ARCH", sizeof ("KBUILD_HOST_ARCH") - 1,
                     val, o_default, 0);
  if (!envvar2)
    define_variable ("BUILD_PLATFORM_ARCH", sizeof ("BUILD_PLATFORM_ARCH") - 1,
                     val, o_default, 0);

  envvar1 = lookup_variable (STRING_SIZE_TUPLE ("KBUILD_HOST_CPU"));
  envvar2 = lookup_variable (STRING_SIZE_TUPLE ("BUILD_PLATFORM_CPU"));
  val = envvar1 ? envvar1->value : envvar2 ? envvar2->value : KBUILD_HOST_CPU;
  if (envvar1 && envvar2 && strcmp (envvar1->value, envvar2->value))
    error (NULL, _("KBUILD_HOST_CPU and BUILD_PLATFORM_CPU differs, using KBUILD_HOST_CPU=%s."), val);
  if (!envvar1)
    define_variable ("KBUILD_HOST_CPU", sizeof ("KBUILD_HOST_CPU") - 1,
                     val, o_default, 0);
  if (!envvar2)
    define_variable ("BUILD_PLATFORM_CPU", sizeof ("BUILD_PLATFORM_CPU") - 1,
                     val, o_default, 0);

  /* The kBuild locations. */
  define_variable ("KBUILD_PATH", sizeof ("KBUILD_PATH") - 1,
                   get_kbuild_path (), o_default, 0);
  define_variable ("KBUILD_BIN_PATH", sizeof ("KBUILD_BIN_PATH") - 1,
                   get_kbuild_bin_path (), o_default, 0);

  define_variable ("PATH_KBUILD", sizeof ("PATH_KBUILD") - 1,
                   get_kbuild_path (), o_default, 0);
  define_variable ("PATH_KBUILD_BIN", sizeof ("PATH_KBUILD_BIN") - 1,
                   get_kbuild_bin_path (), o_default, 0);

  /* Define KMK_FEATURES to indicate various working KMK features. */
# if defined (CONFIG_WITH_RSORT) \
  && defined (CONFIG_WITH_ABSPATHEX) \
  && defined (CONFIG_WITH_TOUPPER_TOLOWER) \
  && defined (CONFIG_WITH_DEFINED) \
  && defined (CONFIG_WITH_VALUE_LENGTH) && defined (CONFIG_WITH_COMPARE) \
  && defined (CONFIG_WITH_STACK) \
  && defined (CONFIG_WITH_MATH) \
  && defined (CONFIG_WITH_XARGS) \
  && defined (CONFIG_WITH_EXPLICIT_MULTITARGET) \
  && defined (CONFIG_WITH_PREPEND_ASSIGNMENT) \
  && defined (CONFIG_WITH_SET_CONDITIONALS) \
  && defined (CONFIG_WITH_DATE) \
  && defined (CONFIG_WITH_FILE_SIZE) \
  && defined (CONFIG_WITH_WHICH) \
  && defined (CONFIG_WITH_EVALPLUS) \
  && (defined (CONFIG_WITH_MAKE_STATS) || defined (CONFIG_WITH_MINIMAL_STATS)) \
  && defined (CONFIG_WITH_COMMANDS_FUNC) \
  && defined (KMK_HELPERS)
  (void) define_variable ("KMK_FEATURES", 12,
                          "append-dash-n abspath includedep-queue"
                          " rsort"
                          " abspathex"
                          " toupper tolower"
                          " defined"
                          " comp-vars comp-cmds comp-cmds-ex"
                          " stack"
                          " math-int"
                          " xargs"
                          " explicit-multitarget"
                          " prepend-assignment"
                          " set-conditionals"
                          " date"
                          " file-size"
                          " expr if-expr"
                          " which"
                          " evalctx evalval evalvalctx evalcall evalcall2 eval-opt-var"
                          " make-stats"
                          " commands"
                          " kb-src-tool kb-obj-base kb-obj-suff kb-src-prop kb-src-one "
                          , o_default, 0);
# else /* MSC can't deal with strings mixed with #if/#endif, thus the slow way. */
#  error "All features should be enabled by default!"
  strcpy (buf, "append-dash-n abspath includedep-queue");
#  if defined (CONFIG_WITH_RSORT)
  strcat (buf, " rsort");
#  endif
#  if defined (CONFIG_WITH_ABSPATHEX)
  strcat (buf, " abspathex");
#  endif
#  if defined (CONFIG_WITH_TOUPPER_TOLOWER)
  strcat (buf, " toupper tolower");
#  endif
#  if defined (CONFIG_WITH_DEFINED)
  strcat (buf, " defined");
#  endif
#  if defined (CONFIG_WITH_VALUE_LENGTH) && defined(CONFIG_WITH_COMPARE)
  strcat (buf, " comp-vars comp-cmds comp-cmds-ex");
#  endif
#  if defined (CONFIG_WITH_STACK)
  strcat (buf, " stack");
#  endif
#  if defined (CONFIG_WITH_MATH)
  strcat (buf, " math-int");
#  endif
#  if defined (CONFIG_WITH_XARGS)
  strcat (buf, " xargs");
#  endif
#  if defined (CONFIG_WITH_EXPLICIT_MULTITARGET)
  strcat (buf, " explicit-multitarget");
#  endif
#  if defined (CONFIG_WITH_PREPEND_ASSIGNMENT)
  strcat (buf, " prepend-assignment");
#  endif
#  if defined (CONFIG_WITH_SET_CONDITIONALS)
  strcat (buf, " set-conditionals");
#  endif
#  if defined (CONFIG_WITH_DATE)
  strcat (buf, " date");
#  endif
#  if defined (CONFIG_WITH_FILE_SIZE)
  strcat (buf, " file-size");
#  endif
#  if defined (CONFIG_WITH_IF_CONDITIONALS)
  strcat (buf, " expr if-expr");
#  endif
#  if defined (CONFIG_WITH_WHICH)
  strcat (buf, " which");
#  endif
#  if defined (CONFIG_WITH_EVALPLUS)
  strcat (buf, " evalctx evalval evalvalctx evalcall evalcall2 eval-opt-var");
#  endif
#  if defined (CONFIG_WITH_MAKE_STATS) || defined (CONFIG_WITH_MINIMAL_STATS)
  strcat (buf, " make-stats");
#  endif
#  if defined (CONFIG_WITH_COMMANDS_FUNC)
  strcat (buf, " commands");
#  endif
#  if defined (KMK_HELPERS)
  strcat (buf, " kb-src-tool kb-obj-base kb-obj-suff kb-src-prop kb-src-one");
#  endif
  (void) define_variable ("KMK_FEATURES", 12, buf, o_default, 0);
# endif

#endif /* KMK */

#ifdef CONFIG_WITH_KMK_BUILTIN
  /* The supported kMk Builtin commands. */
  (void) define_variable ("KMK_BUILTIN", 11, "append cat chmod cp cmp echo expr install kDepIDB ln md5sum mkdir mv printf rm rmdir test", o_default, 0);
#endif

#ifdef  __MSDOS__
  /* Allow to specify a special shell just for Make,
     and use $COMSPEC as the default $SHELL when appropriate.  */
  {
    static char shell_str[] = "SHELL";
    const int shlen = sizeof (shell_str) - 1;
    struct variable *mshp = lookup_variable ("MAKESHELL", 9);
    struct variable *comp = lookup_variable ("COMSPEC", 7);

    /* Make $MAKESHELL override $SHELL even if -e is in effect.  */
    if (mshp)
      (void) define_variable (shell_str, shlen,
			      mshp->value, o_env_override, 0);
    else if (comp)
      {
	/* $COMSPEC shouldn't override $SHELL.  */
	struct variable *shp = lookup_variable (shell_str, shlen);

	if (!shp)
	  (void) define_variable (shell_str, shlen, comp->value, o_env, 0);
      }
  }
#elif defined(__EMX__)
  {
    static char shell_str[] = "SHELL";
    const int shlen = sizeof (shell_str) - 1;
    struct variable *shell = lookup_variable (shell_str, shlen);
    struct variable *replace = lookup_variable ("MAKESHELL", 9);

    /* if $MAKESHELL is defined in the environment assume o_env_override */
    if (replace && *replace->value && replace->origin == o_env)
      replace->origin = o_env_override;

    /* if $MAKESHELL is not defined use $SHELL but only if the variable
       did not come from the environment */
    if (!replace || !*replace->value)
      if (shell && *shell->value && (shell->origin == o_env
	  || shell->origin == o_env_override))
	{
	  /* overwrite whatever we got from the environment */
	  free(shell->value);
	  shell->value = xstrdup (default_shell);
	  shell->origin = o_default;
	}

    /* Some people do not like cmd to be used as the default
       if $SHELL is not defined in the Makefile.
       With -DNO_CMD_DEFAULT you can turn off this behaviour */
# ifndef NO_CMD_DEFAULT
    /* otherwise use $COMSPEC */
    if (!replace || !*replace->value)
      replace = lookup_variable ("COMSPEC", 7);

    /* otherwise use $OS2_SHELL */
    if (!replace || !*replace->value)
      replace = lookup_variable ("OS2_SHELL", 9);
# else
#   warning NO_CMD_DEFAULT: GNU make will not use CMD.EXE as default shell
# endif

    if (replace && *replace->value)
      /* overwrite $SHELL */
      (void) define_variable (shell_str, shlen, replace->value,
			      replace->origin, 0);
    else
      /* provide a definition if there is none */
      (void) define_variable (shell_str, shlen, default_shell,
			      o_default, 0);
  }

#endif

  /* This won't override any definition, but it will provide one if there
     isn't one there.  */
  v = define_variable ("SHELL", 5, default_shell, o_default, 0);

  /* On MSDOS we do use SHELL from environment, since it isn't a standard
     environment variable on MSDOS, so whoever sets it, does that on purpose.
     On OS/2 we do not use SHELL from environment but we have already handled
     that problem above. */
#if !defined(__MSDOS__) && !defined(__EMX__)
  /* Don't let SHELL come from the environment.  */
  if (*v->value == '\0' || v->origin == o_env || v->origin == o_env_override)
    {
#ifdef CONFIG_WITH_RDONLY_VARIABLE_VALUE
      if (v->rdonly_val)
        v->rdonly_val = 0;
      else
#endif
        free (v->value);
      v->origin = o_file;
      v->value = xstrdup (default_shell);
#ifdef CONFIG_WITH_VALUE_LENGTH
      v->value_length = strlen (v->value);
      v->value_alloc_len = v->value_length + 1;
#endif
    }
#endif

  /* Make sure MAKEFILES gets exported if it is set.  */
  v = define_variable ("MAKEFILES", 9, "", o_default, 0);
  v->export = v_ifset;

  /* Define the magic D and F variables in terms of
     the automatic variables they are variations of.  */

#ifdef VMS
  define_variable ("@D", 2, "$(dir $@)", o_automatic, 1);
  define_variable ("%D", 2, "$(dir $%)", o_automatic, 1);
  define_variable ("*D", 2, "$(dir $*)", o_automatic, 1);
  define_variable ("<D", 2, "$(dir $<)", o_automatic, 1);
  define_variable ("?D", 2, "$(dir $?)", o_automatic, 1);
  define_variable ("^D", 2, "$(dir $^)", o_automatic, 1);
  define_variable ("+D", 2, "$(dir $+)", o_automatic, 1);
#else
  define_variable ("@D", 2, "$(patsubst %/,%,$(dir $@))", o_automatic, 1);
  define_variable ("%D", 2, "$(patsubst %/,%,$(dir $%))", o_automatic, 1);
  define_variable ("*D", 2, "$(patsubst %/,%,$(dir $*))", o_automatic, 1);
  define_variable ("<D", 2, "$(patsubst %/,%,$(dir $<))", o_automatic, 1);
  define_variable ("?D", 2, "$(patsubst %/,%,$(dir $?))", o_automatic, 1);
  define_variable ("^D", 2, "$(patsubst %/,%,$(dir $^))", o_automatic, 1);
  define_variable ("+D", 2, "$(patsubst %/,%,$(dir $+))", o_automatic, 1);
#endif
  define_variable ("@F", 2, "$(notdir $@)", o_automatic, 1);
  define_variable ("%F", 2, "$(notdir $%)", o_automatic, 1);
  define_variable ("*F", 2, "$(notdir $*)", o_automatic, 1);
  define_variable ("<F", 2, "$(notdir $<)", o_automatic, 1);
  define_variable ("?F", 2, "$(notdir $?)", o_automatic, 1);
  define_variable ("^F", 2, "$(notdir $^)", o_automatic, 1);
  define_variable ("+F", 2, "$(notdir $+)", o_automatic, 1);
#ifdef CONFIG_WITH_LAZY_DEPS_VARS
  define_variable ("^", 1, "$(deps $@)", o_automatic, 1);
  define_variable ("+", 1, "$(deps-all $@)", o_automatic, 1);
  define_variable ("?", 1, "$(deps-newer $@)", o_automatic, 1);
  define_variable ("|", 1, "$(deps-oo $@)", o_automatic, 1);
#endif /* CONFIG_WITH_LAZY_DEPS_VARS */
}

int export_all_variables;

/* Create a new environment for FILE's commands.
   If FILE is nil, this is for the `shell' function.
   The child's MAKELEVEL variable is incremented.  */

char **
target_environment (struct file *file)
{
  struct variable_set_list *set_list;
  register struct variable_set_list *s;
  struct hash_table table;
  struct variable **v_slot;
  struct variable **v_end;
  struct variable makelevel_key;
  char **result_0;
  char **result;
#ifdef CONFIG_WITH_STRCACHE2
  const char *cached_name;
#endif

  if (file == 0)
    set_list = current_variable_set_list;
  else
    set_list = file->variables;

#ifndef CONFIG_WITH_STRCACHE2
  hash_init (&table, VARIABLE_BUCKETS,
	     variable_hash_1, variable_hash_2, variable_hash_cmp);
#else  /* CONFIG_WITH_STRCACHE2 */
  hash_init_strcached (&table, VARIABLE_BUCKETS,
                       &variable_strcache, offsetof (struct variable, name));
#endif /* CONFIG_WITH_STRCACHE2 */

  /* Run through all the variable sets in the list,
     accumulating variables in TABLE.  */
  for (s = set_list; s != 0; s = s->next)
    {
      struct variable_set *set = s->set;
      v_slot = (struct variable **) set->table.ht_vec;
      v_end = v_slot + set->table.ht_size;
      for ( ; v_slot < v_end; v_slot++)
	if (! HASH_VACANT (*v_slot))
	  {
	    struct variable **new_slot;
	    struct variable *v = *v_slot;

	    /* If this is a per-target variable and it hasn't been touched
	       already then look up the global version and take its export
	       value.  */
	    if (v->per_target && v->export == v_default)
	      {
		struct variable *gv;

#ifndef CONFIG_WITH_VALUE_LENGTH
		gv = lookup_variable_in_set (v->name, strlen(v->name),
                                             &global_variable_set);
#else
                assert ((int)strlen(v->name) == v->length);
                gv = lookup_variable_in_set (v->name, v->length,
                                                     &global_variable_set);
#endif
		if (gv)
		  v->export = gv->export;
	      }

	    switch (v->export)
	      {
	      case v_default:
		if (v->origin == o_default || v->origin == o_automatic)
		  /* Only export default variables by explicit request.  */
		  continue;

                /* The variable doesn't have a name that can be exported.  */
                if (! v->exportable)
                  continue;

		if (! export_all_variables
		    && v->origin != o_command
		    && v->origin != o_env && v->origin != o_env_override)
		  continue;
		break;

	      case v_export:
		break;

	      case v_noexport:
                /* If this is the SHELL variable and it's not exported, then
                   add the value from our original environment.  */
                if (streq (v->name, "SHELL"))
                  {
                    extern struct variable shell_var;
                    v = &shell_var;
                    break;
                  }
                continue;

	      case v_ifset:
		if (v->origin == o_default)
		  continue;
		break;
	      }

#ifndef CONFIG_WITH_STRCACHE2
	    new_slot = (struct variable **) hash_find_slot (&table, v);
#else  /* CONFIG_WITH_STRCACHE2 */
	    assert (strcache2_is_cached (&variable_strcache, v->name));
	    new_slot = (struct variable **) hash_find_slot_strcached (&table, v);
#endif /* CONFIG_WITH_STRCACHE2 */
	    if (HASH_VACANT (*new_slot))
	      hash_insert_at (&table, v, new_slot);
	  }
    }

#ifndef CONFIG_WITH_STRCACHE2
  makelevel_key.name = MAKELEVEL_NAME;
  makelevel_key.length = MAKELEVEL_LENGTH;
  hash_delete (&table, &makelevel_key);
#else  /* CONFIG_WITH_STRCACHE2 */
  /* lookup the name in the string case, if it's not there it won't
     be in any of the sets either. */
  cached_name = strcache2_lookup (&variable_strcache,
                                  MAKELEVEL_NAME, MAKELEVEL_LENGTH);
  if (cached_name)
    {
      makelevel_key.name = cached_name;
      makelevel_key.length = MAKELEVEL_LENGTH;
      hash_delete_strcached (&table, &makelevel_key);
    }
#endif /* CONFIG_WITH_STRCACHE2 */

  result = result_0 = xmalloc ((table.ht_fill + 2) * sizeof (char *));

  v_slot = (struct variable **) table.ht_vec;
  v_end = v_slot + table.ht_size;
  for ( ; v_slot < v_end; v_slot++)
    if (! HASH_VACANT (*v_slot))
      {
	struct variable *v = *v_slot;

	/* If V is recursively expanded and didn't come from the environment,
	   expand its value.  If it came from the environment, it should
	   go back into the environment unchanged.  */
	if (v->recursive
	    && v->origin != o_env && v->origin != o_env_override)
	  {
#ifndef CONFIG_WITH_VALUE_LENGTH
	    char *value = recursively_expand_for_file (v, file);
#else
	    char *value = recursively_expand_for_file (v, file, NULL);
#endif
#ifdef WINDOWS32
	    if (strcmp(v->name, "Path") == 0 ||
		strcmp(v->name, "PATH") == 0)
	      convert_Path_to_windows32(value, ';');
#endif
	    *result++ = xstrdup (concat (v->name, "=", value));
	    free (value);
	  }
	else
	  {
#ifdef WINDOWS32
            if (strcmp(v->name, "Path") == 0 ||
                strcmp(v->name, "PATH") == 0)
              convert_Path_to_windows32(v->value, ';');
#endif
	    *result++ = xstrdup (concat (v->name, "=", v->value));
	  }
      }

  *result = xmalloc (100);
  sprintf (*result, "%s=%u", MAKELEVEL_NAME, makelevel + 1);
  *++result = 0;

  hash_free (&table, 0);

  return result_0;
}

#ifdef CONFIG_WITH_VALUE_LENGTH
/* Worker function for do_variable_definition_append() and
   append_expanded_string_to_variable().
   The APPEND argument indicates whether it's an append or prepend operation. */
void append_string_to_variable (struct variable *v, const char *value, unsigned int value_len, int append)
{
  /* The previous definition of the variable was recursive.
     The new value is the unexpanded old and new values. */
  unsigned int new_value_len = value_len + (v->value_length != 0 ? 1 + v->value_length : 0);
  int done_1st_prepend_copy = 0;

  /* Drop empty strings. Use $(NO_SUCH_VARIABLE) if a space is wanted. */
  if (!value_len)
      return;

  /* adjust the size. */
  if ((unsigned)v->value_alloc_len <= new_value_len + 1)
    {
      v->value_alloc_len *= 2;
      if ((unsigned)v->value_alloc_len < new_value_len + 1)
          v->value_alloc_len = (new_value_len + 1 + value_len + 0x7f) + ~0x7fU;
# ifdef CONFIG_WITH_RDONLY_VARIABLE_VALUE
      if ((append || !v->value_length) && !v->rdonly_val)
# else
      if (append || !v->value_length)
# endif
        v->value = xrealloc (v->value, v->value_alloc_len);
      else
        {
          /* avoid the extra memcpy the xrealloc may have to do */
          char *new_buf = xmalloc (v->value_alloc_len);
          memcpy (&new_buf[value_len + 1], v->value, v->value_length + 1);
          done_1st_prepend_copy = 1;
# ifdef CONFIG_WITH_RDONLY_VARIABLE_VALUE
          if (v->rdonly_val)
            v->rdonly_val = 0;
          else
# endif
            free (v->value);
          v->value = new_buf;
        }
    }

  /* insert the new bits */
  if (v->value_length != 0)
    {
      if (append)
        {
          v->value[v->value_length] = ' ';
          memcpy (&v->value[v->value_length + 1], value, value_len + 1);
        }
      else
        {
          if (!done_1st_prepend_copy)
            memmove (&v->value[value_len + 1], v->value, v->value_length + 1);
          v->value[value_len] = ' ';
          memcpy (v->value, value, value_len);
        }
    }
  else
    memcpy (v->value, value, value_len + 1);
  v->value_length = new_value_len;
}

static struct variable *
do_variable_definition_append (const struct floc *flocp, struct variable *v,
                               const char *value, unsigned int value_len,
                               int simple_value, enum variable_origin origin,
                               int append)
{
  if (env_overrides && origin == o_env)
    origin = o_env_override;

  if (env_overrides && v->origin == o_env)
    /* V came from in the environment.  Since it was defined
       before the switches were parsed, it wasn't affected by -e.  */
    v->origin = o_env_override;

  /* A variable of this name is already defined.
     If the old definition is from a stronger source
     than this one, don't redefine it.  */
  if ((int) origin < (int) v->origin)
    return v;
  v->origin = origin;

  /* location */
  if (flocp != 0)
    v->fileinfo = *flocp;

  /* The juicy bits, append the specified value to the variable
     This is a heavily exercised code path in kBuild. */
  if (value_len == ~0U)
    value_len = strlen (value);
  if (v->recursive || simple_value)
    append_string_to_variable (v, value, value_len, append);
  else
    /* The previous definition of the variable was simple.
       The new value comes from the old value, which was expanded
       when it was set; and from the expanded new value. */
    append_expanded_string_to_variable (v, value, value_len, append);

  /* update the variable */
  return v;
}
#endif /* CONFIG_WITH_VALUE_LENGTH */

/* Given a variable, a value, and a flavor, define the variable.
   See the try_variable_definition() function for details on the parameters. */

struct variable *
#ifndef CONFIG_WITH_VALUE_LENGTH
do_variable_definition (const struct floc *flocp, const char *varname,
                        const char *value, enum variable_origin origin,
                        enum variable_flavor flavor, int target_var)
#else  /* CONFIG_WITH_VALUE_LENGTH */
do_variable_definition_2 (const struct floc *flocp,
                          const char *varname, const char *value,
                          unsigned int value_len, int simple_value,
                          char *free_value,
                          enum variable_origin origin,
                          enum variable_flavor flavor,
                          int target_var)
#endif /* CONFIG_WITH_VALUE_LENGTH */
{
  const char *p;
  char *alloc_value = NULL;
  struct variable *v;
  int append = 0;
  int conditional = 0;
  const size_t varname_len = strlen (varname); /* bird */
#ifdef CONFIG_WITH_VALUE_LENGTH
  assert (value_len == ~0U || value_len == strlen (value));
#endif

  /* Calculate the variable's new value in VALUE.  */

  switch (flavor)
    {
    default:
    case f_bogus:
      /* Should not be possible.  */
      abort ();
    case f_simple:
      /* A simple variable definition "var := value".  Expand the value.
         We have to allocate memory since otherwise it'll clobber the
	 variable buffer, and we may still need that if we're looking at a
         target-specific variable.  */
#ifndef CONFIG_WITH_VALUE_LENGTH
      p = alloc_value = allocated_variable_expand (value);
#else  /* CONFIG_WITH_VALUE_LENGTH */
      if (!simple_value)
        p = alloc_value = allocated_variable_expand_2 (value, value_len, &value_len);
      else
      {
        if (value_len == ~0U)
          value_len = strlen (value);
        if (!free_value)
          p = alloc_value = savestring (value, value_len);
        else
          {
            assert (value == free_value);
            p = alloc_value = free_value;
            free_value = 0;
          }
      }
#endif /* CONFIG_WITH_VALUE_LENGTH */
      break;
    case f_conditional:
      /* A conditional variable definition "var ?= value".
         The value is set IFF the variable is not defined yet. */
      v = lookup_variable (varname, varname_len);
      if (v)
        return v;

      conditional = 1;
      flavor = f_recursive;
      /* FALLTHROUGH */
    case f_recursive:
      /* A recursive variable definition "var = value".
	 The value is used verbatim.  */
      p = value;
      break;
#ifdef CONFIG_WITH_PREPEND_ASSIGNMENT
    case f_append:
    case f_prepend:
      {
        const enum variable_flavor org_flavor = flavor;
#else
    case f_append:
      {
#endif

#ifdef CONFIG_WITH_LOCAL_VARIABLES
          /* If we have += but we're in a target or local variable context,
             we want to append only with other variables in the context of
             this target.  */
        if (target_var || origin == o_local)
#else
        /* If we have += but we're in a target variable context, we want to
           append only with other variables in the context of this target.  */
        if (target_var)
#endif
          {
            append = 1;
            v = lookup_variable_in_set (varname, varname_len,
                                        current_variable_set_list->set);

            /* Don't append from the global set if a previous non-appending
               target-specific variable definition exists. */
            if (v && !v->append)
              append = 0;
          }
        else
          v = lookup_variable (varname, varname_len);

        if (v == 0)
          {
            /* There was no old value.
               This becomes a normal recursive definition.  */
            p = value;
            flavor = f_recursive;
          }
        else
          {
#ifdef CONFIG_WITH_VALUE_LENGTH
            v->append = append;
            v = do_variable_definition_append (flocp, v, value, value_len,
                                               simple_value, origin,
# ifdef CONFIG_WITH_PREPEND_ASSIGNMENT
                                               org_flavor == f_append);
# else
                                               1);
# endif
            if (free_value)
               free (free_value);
            return v;
#else /* !CONFIG_WITH_VALUE_LENGTH */

            /* Paste the old and new values together in VALUE.  */

            unsigned int oldlen, vallen;
            const char *val;
            char *tp;

            val = value;
            if (v->recursive)
              /* The previous definition of the variable was recursive.
                 The new value is the unexpanded old and new values. */
              flavor = f_recursive;
            else
              /* The previous definition of the variable was simple.
                 The new value comes from the old value, which was expanded
                 when it was set; and from the expanded new value.  Allocate
                 memory for the expansion as we may still need the rest of the
                 buffer if we're looking at a target-specific variable.  */
              val = alloc_value = allocated_variable_expand (val);

            oldlen = strlen (v->value);
            vallen = strlen (val);
            tp = alloca (oldlen + 1 + vallen + 1);
# ifdef CONFIG_WITH_PREPEND_ASSIGNMENT
            if (org_flavor == f_prepend)
              {
                memcpy (tp, val, vallen);
                tp[oldlen] = ' ';
                memcpy (&tp[oldlen + 1], v->value, oldlen + 1);
              }
            else
# endif /* CONFIG_WITH_PREPEND_ASSIGNMENT */
              {
                memcpy (tp, v->value, oldlen);
                tp[oldlen] = ' ';
                memcpy (&tp[oldlen + 1], val, vallen + 1);
              }
            p = tp;
#endif /* !CONFIG_WITH_VALUE_LENGTH */
          }
      }
    }

#ifdef __MSDOS__
  /* Many Unix Makefiles include a line saying "SHELL=/bin/sh", but
     non-Unix systems don't conform to this default configuration (in
     fact, most of them don't even have `/bin').  On the other hand,
     $SHELL in the environment, if set, points to the real pathname of
     the shell.
     Therefore, we generally won't let lines like "SHELL=/bin/sh" from
     the Makefile override $SHELL from the environment.  But first, we
     look for the basename of the shell in the directory where SHELL=
     points, and along the $PATH; if it is found in any of these places,
     we define $SHELL to be the actual pathname of the shell.  Thus, if
     you have bash.exe installed as d:/unix/bash.exe, and d:/unix is on
     your $PATH, then SHELL=/usr/local/bin/bash will have the effect of
     defining SHELL to be "d:/unix/bash.exe".  */
  if ((origin == o_file || origin == o_override)
      && strcmp (varname, "SHELL") == 0)
    {
      PATH_VAR (shellpath);
      extern char * __dosexec_find_on_path (const char *, char *[], char *);

      /* See if we can find "/bin/sh.exe", "/bin/sh.com", etc.  */
      if (__dosexec_find_on_path (p, NULL, shellpath))
	{
	  char *tp;

	  for (tp = shellpath; *tp; tp++)
            if (*tp == '\\')
              *tp = '/';

	  v = define_variable_loc (varname, varname_len,
                                   shellpath, origin, flavor == f_recursive,
                                   flocp);
	}
      else
	{
	  const char *shellbase, *bslash;
	  struct variable *pathv = lookup_variable ("PATH", 4);
	  char *path_string;
	  char *fake_env[2];
	  size_t pathlen = 0;

	  shellbase = strrchr (p, '/');
	  bslash = strrchr (p, '\\');
	  if (!shellbase || bslash > shellbase)
	    shellbase = bslash;
	  if (!shellbase && p[1] == ':')
	    shellbase = p + 1;
	  if (shellbase)
	    shellbase++;
	  else
	    shellbase = p;

	  /* Search for the basename of the shell (with standard
	     executable extensions) along the $PATH.  */
	  if (pathv)
	    pathlen = strlen (pathv->value);
	  path_string = xmalloc (5 + pathlen + 2 + 1);
	  /* On MSDOS, current directory is considered as part of $PATH.  */
	  sprintf (path_string, "PATH=.;%s", pathv ? pathv->value : "");
	  fake_env[0] = path_string;
	  fake_env[1] = 0;
	  if (__dosexec_find_on_path (shellbase, fake_env, shellpath))
	    {
	      char *tp;

	      for (tp = shellpath; *tp; tp++)
                if (*tp == '\\')
                  *tp = '/';

	      v = define_variable_loc (varname, varname_len,
                                       shellpath, origin,
                                       flavor == f_recursive, flocp);
	    }
	  else
	    v = lookup_variable (varname, varname_len);

	  free (path_string);
	}
    }
  else
#endif /* __MSDOS__ */
#ifdef WINDOWS32
  if (   varname_len == sizeof("SHELL") - 1 /* bird */
      && (origin == o_file || origin == o_override || origin == o_command)
      && streq (varname, "SHELL"))
    {
      extern char *default_shell;

      /* Call shell locator function. If it returns TRUE, then
	 set no_default_sh_exe to indicate sh was found and
         set new value for SHELL variable.  */

      if (find_and_set_default_shell (p))
        {
          v = define_variable_in_set (varname, varname_len, default_shell,
# ifdef CONFIG_WITH_VALUE_LENGTH
                                      ~0U, 1 /* duplicate_value */,
# endif
                                      origin, flavor == f_recursive,
                                      (target_var
                                       ? current_variable_set_list->set
                                       : NULL),
                                      flocp);
          no_default_sh_exe = 0;
        }
      else
        v = lookup_variable (varname, varname_len);
    }
  else
#endif

  /* If we are defining variables inside an $(eval ...), we might have a
     different variable context pushed, not the global context (maybe we're
     inside a $(call ...) or something.  Since this function is only ever
     invoked in places where we want to define globally visible variables,
     make sure we define this variable in the global set.  */

  v = define_variable_in_set (varname, varname_len, p,
#ifdef CONFIG_WITH_VALUE_LENGTH
                              value_len, !alloc_value,
#endif
                              origin, flavor == f_recursive,
#ifdef CONFIG_WITH_LOCAL_VARIABLES
                              (target_var || origin == o_local
#else
                              (target_var
#endif
                               ? current_variable_set_list->set : NULL),
                              flocp);
  v->append = append;
  v->conditional = conditional;

#ifndef CONFIG_WITH_VALUE_LENGTH
  if (alloc_value)
    free (alloc_value);
#else
  if (free_value)
    free (free_value);
#endif

  return v;
}

/* Try to interpret LINE (a null-terminated string) as a variable definition.

   ORIGIN may be o_file, o_override, o_env, o_env_override,
   or o_command specifying that the variable definition comes
   from a makefile, an override directive, the environment with
   or without the -e switch, or the command line.

   See the comments for parse_variable_definition().

   If LINE was recognized as a variable definition, a pointer to its `struct
   variable' is returned.  If LINE is not a variable definition, NULL is
   returned.  */

struct variable *
#ifndef CONFIG_WITH_VALUE_LENGTH
parse_variable_definition (struct variable *v, char *line)
#else
parse_variable_definition (struct variable *v, char *line, char *eos)
#endif
{
  register int c;
  register char *p = line;
  register char *beg;
  register char *end;
  enum variable_flavor flavor = f_bogus;
#ifndef CONFIG_WITH_VALUE_LENGTH
  char *name;
#endif

  while (1)
    {
      c = *p++;
      if (c == '\0' || c == '#')
	return 0;
      if (c == '=')
	{
	  end = p - 1;
	  flavor = f_recursive;
	  break;
	}
      else if (c == ':')
	if (*p == '=')
	  {
	    end = p++ - 1;
	    flavor = f_simple;
	    break;
	  }
	else
	  /* A colon other than := is a rule line, not a variable defn.  */
	  return 0;
      else if (c == '+' && *p == '=')
	{
	  end = p++ - 1;
	  flavor = f_append;
	  break;
	}
#ifdef CONFIG_WITH_PREPEND_ASSIGNMENT
      else if (c == '<' && *p == '=')
        {
          end = p++ - 1;
          flavor = f_prepend;
          break;
        }
#endif
      else if (c == '?' && *p == '=')
        {
          end = p++ - 1;
          flavor = f_conditional;
          break;
        }
      else if (c == '$')
	{
	  /* This might begin a variable expansion reference.  Make sure we
	     don't misrecognize chars inside the reference as =, := or +=.  */
	  char closeparen;
	  int count;
	  c = *p++;
	  if (c == '(')
	    closeparen = ')';
	  else if (c == '{')
	    closeparen = '}';
	  else
	    continue;		/* Nope.  */

	  /* P now points past the opening paren or brace.
	     Count parens or braces until it is matched.  */
	  count = 0;
	  for (; *p != '\0'; ++p)
	    {
	      if (*p == c)
		++count;
	      else if (*p == closeparen && --count < 0)
		{
		  ++p;
		  break;
		}
	    }
	}
    }
  v->flavor = flavor;

  beg = next_token (line);
  while (end > beg && isblank ((unsigned char)end[-1]))
    --end;
  p = next_token (p);
  v->value = p;
#ifdef CONFIG_WITH_VALUE_LENGTH
  v->value_alloc_len = -1;
  v->value_length = eos != NULL ? eos - p : -1;
  assert (eos == NULL || strchr (p, '\0') == eos);
# ifdef CONFIG_WITH_RDONLY_VARIABLE_VALUE
  v->rdonly_val = 0;
# endif
#endif

  /* Expand the name, so "$(foo)bar = baz" works.  */
#ifndef CONFIG_WITH_VALUE_LENGTH
  name = alloca (end - beg + 1);
  memcpy (name, beg, end - beg);
  name[end - beg] = '\0';
  v->name = allocated_variable_expand (name);
#else  /* CONFIG_WITH_VALUE_LENGTH */
  v->name = allocated_variable_expand_2 (beg, end - beg, NULL);
#endif /* CONFIG_WITH_VALUE_LENGTH */

  if (v->name[0] == '\0')
    fatal (&v->fileinfo, _("empty variable name"));

  return v;
}

/* Try to interpret LINE (a null-terminated string) as a variable definition.

   ORIGIN may be o_file, o_override, o_env, o_env_override, o_local,
   or o_command specifying that the variable definition comes
   from a makefile, an override directive, the environment with
   or without the -e switch, or the command line.

   See the comments for parse_variable_definition().

   If LINE was recognized as a variable definition, a pointer to its `struct
   variable' is returned.  If LINE is not a variable definition, NULL is
   returned.  */

struct variable *
#ifndef CONFIG_WITH_VALUE_LENGTH
try_variable_definition (const struct floc *flocp, char *line,
                         enum variable_origin origin, int target_var)
#else
try_variable_definition (const struct floc *flocp, char *line, char *eos,
                         enum variable_origin origin, int target_var)
#endif
{
  struct variable v;
  struct variable *vp;

  if (flocp != 0)
    v.fileinfo = *flocp;
  else
    v.fileinfo.filenm = 0;

#ifndef CONFIG_WITH_VALUE_LENGTH
  if (!parse_variable_definition (&v, line))
    return 0;

  vp = do_variable_definition (flocp, v.name, v.value,
                               origin, v.flavor, target_var);
#else
  if (!parse_variable_definition (&v, line, eos))
    return 0;

  vp = do_variable_definition_2 (flocp, v.name, v.value,
                                 v.value_length != -1 ? (unsigned int)v.value_length : ~0U, /* FIXME */
                                 0, NULL, origin, v.flavor, target_var);
#endif

#ifndef CONFIG_WITH_STRCACHE2
  free (v.name);
#else
  free ((char *)v.name);
#endif

  return vp;
}

/* Print information for variable V, prefixing it with PREFIX.  */

static void
print_variable (const void *item, void *arg)
{
  const struct variable *v = item;
  const char *prefix = arg;
  const char *origin;

  switch (v->origin)
    {
    case o_default:
      origin = _("default");
      break;
    case o_env:
      origin = _("environment");
      break;
    case o_file:
      origin = _("makefile");
      break;
    case o_env_override:
      origin = _("environment under -e");
      break;
    case o_command:
      origin = _("command line");
      break;
    case o_override:
      origin = _("`override' directive");
      break;
    case o_automatic:
      origin = _("automatic");
      break;
#ifdef CONFIG_WITH_LOCAL_VARIABLES
    case o_local:
      origin = _("`local' directive");
      break;
#endif
    case o_invalid:
    default:
      abort ();
    }
  fputs ("# ", stdout);
  fputs (origin, stdout);
  if (v->fileinfo.filenm)
    printf (_(" (from `%s', line %lu)"),
            v->fileinfo.filenm, v->fileinfo.lineno);
  putchar ('\n');
  fputs (prefix, stdout);

  /* Is this a `define'?  */
  if (v->recursive && strchr (v->value, '\n') != 0)
    printf ("define %s\n%s\nendef\n", v->name, v->value);
  else
    {
      register char *p;

      printf ("%s %s= ", v->name, v->recursive ? v->append ? "+" : "" : ":");

      /* Check if the value is just whitespace.  */
      p = next_token (v->value);
      if (p != v->value && *p == '\0')
	/* All whitespace.  */
	printf ("$(subst ,,%s)", v->value);
      else if (v->recursive)
	fputs (v->value, stdout);
      else
	/* Double up dollar signs.  */
	for (p = v->value; *p != '\0'; ++p)
	  {
	    if (*p == '$')
	      putchar ('$');
	    putchar (*p);
	  }
      putchar ('\n');
    }
}


/* Print all the variables in SET.  PREFIX is printed before
   the actual variable definitions (everything else is comments).  */

void
print_variable_set (struct variable_set *set, char *prefix)
{
  hash_map_arg (&set->table, print_variable, prefix);

  fputs (_("# variable set hash-table stats:\n"), stdout);
  fputs ("# ", stdout);
  hash_print_stats (&set->table, stdout);
  putc ('\n', stdout);
}

/* Print the data base of variables.  */

void
print_variable_data_base (void)
{
  puts (_("\n# Variables\n"));

  print_variable_set (&global_variable_set, "");

  puts (_("\n# Pattern-specific Variable Values"));

  {
    struct pattern_var *p;
    int rules = 0;

    for (p = pattern_vars; p != 0; p = p->next)
      {
        ++rules;
        printf ("\n%s :\n", p->target);
        print_variable (&p->variable, "# ");
      }

    if (rules == 0)
      puts (_("\n# No pattern-specific variable values."));
    else
      printf (_("\n# %u pattern-specific variable values"), rules);
  }

#ifdef CONFIG_WITH_STRCACHE2
  strcache2_print_stats (&variable_strcache, "# ");
#endif
}

#ifdef CONFIG_WITH_PRINT_STATS_SWITCH
void
print_variable_stats (void)
{
  fputs (_("\n# Global variable hash-table stats:\n# "), stdout);
  hash_print_stats (&global_variable_set.table, stdout);
  fputs ("\n", stdout);
}
#endif

/* Print all the local variables of FILE.  */

void
print_file_variables (const struct file *file)
{
  if (file->variables != 0)
    print_variable_set (file->variables->set, "# ");
}

#ifdef WINDOWS32
void
sync_Path_environment (void)
{
  char *path = allocated_variable_expand ("$(PATH)");
  static char *environ_path = NULL;

  if (!path)
    return;

  /*
   * If done this before, don't leak memory unnecessarily.
   * Free the previous entry before allocating new one.
   */
  if (environ_path)
    free (environ_path);

  /*
   * Create something WINDOWS32 world can grok
   */
  convert_Path_to_windows32 (path, ';');
  environ_path = xstrdup (concat ("PATH", "=", path));
  putenv (environ_path);
  free (path);
}
#endif
