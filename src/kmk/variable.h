/* Definitions for using variables in GNU Make.
Copyright (C) 1988, 1989, 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997,
1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006, 2007 Free Software
Foundation, Inc.
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

#include "hash.h"

/* Codes in a variable definition saying where the definition came from.
   Increasing numeric values signify less-overridable definitions.  */
enum variable_origin
  {
    o_default,		/* Variable from the default set.  */
    o_env,		/* Variable from environment.  */
    o_file,		/* Variable given in a makefile.  */
    o_env_override,	/* Variable from environment, if -e.  */
    o_command,		/* Variable given by user.  */
    o_override, 	/* Variable from an `override' directive.  */
#ifdef CONFIG_WITH_LOCAL_VARIABLES
    o_local,            /* Variable from an 'local' directive.  */
#endif
    o_automatic,	/* Automatic variable -- cannot be set.  */
    o_invalid		/* Core dump time.  */
  };

enum variable_flavor
  {
    f_bogus,            /* Bogus (error) */
    f_simple,           /* Simple definition (:=) */
    f_recursive,        /* Recursive definition (=) */
    f_append,           /* Appending definition (+=) */
#ifdef CONFIG_WITH_PREPEND_ASSIGNMENT
    f_prepend,          /* Prepending definition (>=) */
#endif
    f_conditional       /* Conditional definition (?=) */
  };

/* Structure that represents one variable definition.
   Each bucket of the hash table is a chain of these,
   chained through `next'.  */

#define EXP_COUNT_BITS  15      /* This gets all the bitfields into 32 bits */
#define EXP_COUNT_MAX   ((1<<EXP_COUNT_BITS)-1)
#ifdef CONFIG_WITH_VALUE_LENGTH
#define VAR_ALIGN_VALUE_ALLOC(len)  ( ((len) + (unsigned int)15) & ~(unsigned int)15 )
#endif

struct variable
  {
#ifndef CONFIG_WITH_STRCACHE2
    char *name;			/* Variable name.  */
#else
    const char *name;		/* Variable name (in varaible_strcache).  */
#endif
    int length;			/* strlen (name) */
#ifdef CONFIG_WITH_VALUE_LENGTH
    unsigned int value_length;	/* The length of the value.  */
    unsigned int value_alloc_len; /* The amount of memory we've actually allocated. */
    /* FIXME: make lengths unsigned! */
#endif
    char *value;		/* Variable value.  */
    struct floc fileinfo;       /* Where the variable was defined.  */
    unsigned int recursive:1;	/* Gets recursively re-evaluated.  */
    unsigned int append:1;	/* Nonzero if an appending target-specific
                                   variable.  */
    unsigned int conditional:1; /* Nonzero if set with a ?=. */
    unsigned int per_target:1;	/* Nonzero if a target-specific variable.  */
    unsigned int special:1;     /* Nonzero if this is a special variable. */
    unsigned int exportable:1;  /* Nonzero if the variable _could_ be
                                   exported.  */
    unsigned int expanding:1;	/* Nonzero if currently being expanded.  */
    unsigned int private_var:1; /* Nonzero avoids inheritance of this
                                   target-specific variable.  */
    unsigned int exp_count:EXP_COUNT_BITS;
                                /* If >1, allow this many self-referential
                                   expansions.  */
#ifdef CONFIG_WITH_RDONLY_VARIABLE_VALUE
    unsigned int rdonly_val:1;  /* VALUE is read only (strcache/const). */
#endif
    enum variable_flavor
      flavor ENUM_BITFIELD (3);	/* Variable flavor.  */
    enum variable_origin
#ifdef CONFIG_WITH_LOCAL_VARIABLES
      origin ENUM_BITFIELD (4);	/* Variable origin.  */
#else
      origin ENUM_BITFIELD (3);	/* Variable origin.  */
#endif
    enum variable_export
      {
	v_export,		/* Export this variable.  */
	v_noexport,		/* Don't export this variable.  */
	v_ifset,		/* Export it if it has a non-default value.  */
	v_default		/* Decide in target_environment.  */
      } export ENUM_BITFIELD (2);
#ifdef CONFIG_WITH_MAKE_STATS
    unsigned int changes;
    unsigned int reallocs;
#endif
  };

/* Structure that represents a variable set.  */

struct variable_set
  {
    struct hash_table table;	/* Hash table of variables.  */
  };

/* Structure that represents a list of variable sets.  */

struct variable_set_list
  {
    struct variable_set_list *next;	/* Link in the chain.  */
    struct variable_set *set;		/* Variable set.  */
    int next_is_parent;                 /* True if next is a parent target.  */
  };

/* Structure used for pattern-specific variables.  */

struct pattern_var
  {
    struct pattern_var *next;
    const char *suffix;
    const char *target;
    unsigned int len;
    struct variable variable;
  };

extern char *variable_buffer;
extern struct variable_set_list *current_variable_set_list;
extern struct variable *default_goal_var;

#ifdef KMK
extern unsigned int variable_buffer_length;
# define VARIABLE_BUFFER_ZONE   5
#endif

/* expand.c */
#ifndef KMK
char *
variable_buffer_output (char *ptr, const char *string, unsigned int length);
#else /* KMK */
/* Subroutine of variable_expand and friends:
   The text to add is LENGTH chars starting at STRING to the variable_buffer.
   The text is added to the buffer at PTR, and the updated pointer into
   the buffer is returned as the value.  Thus, the value returned by
   each call to variable_buffer_output should be the first argument to
   the following call.  */

__inline static char *
variable_buffer_output (char *ptr, const char *string, unsigned int length)
{
  register unsigned int newlen = length + (ptr - variable_buffer);

  if ((newlen + VARIABLE_BUFFER_ZONE) > variable_buffer_length)
    {
      unsigned int offset = ptr - variable_buffer;
      variable_buffer_length = variable_buffer_length <= 1024
                             ? 2048 : variable_buffer_length * 4;
      if (variable_buffer_length < newlen + 100)
          variable_buffer_length = (newlen + 100 + 1023) & ~1023U;
      variable_buffer = xrealloc (variable_buffer, variable_buffer_length);
      ptr = variable_buffer + offset;
    }

# ifndef _MSC_VER
  switch (length)
    {
      case 4: ptr[3] = string[3];
      case 3: ptr[2] = string[2];
      case 2: ptr[1] = string[1];
      case 1: ptr[0] = string[0];
      case 0:
          break;
      default:
          memcpy (ptr, string, length);
          break;
    }
# else
  memcpy (ptr, string, length);
# endif
  return ptr + length;
}

#endif /* KMK */
char *variable_expand (const char *line);
char *variable_expand_for_file (const char *line, struct file *file);
#if defined (CONFIG_WITH_VALUE_LENGTH) || defined (CONFIG_WITH_COMMANDS_FUNC)
char *variable_expand_for_file_2 (char *o, const char *line, unsigned int lenght,
                                  struct file *file, unsigned int *value_lenp);
#endif
char *allocated_variable_expand_for_file (const char *line, struct file *file);
#ifndef CONFIG_WITH_VALUE_LENGTH
#define	allocated_variable_expand(line) \
  allocated_variable_expand_for_file (line, (struct file *) 0)
#else  /* CONFIG_WITH_VALUE_LENGTH */
# define allocated_variable_expand(line) \
  allocated_variable_expand_2 (line, -1, NULL)
char *allocated_variable_expand_2 (const char *line, unsigned int length, unsigned int *value_lenp);
char *allocated_variable_expand_3 (const char *line, unsigned int length,
                                   unsigned int *value_lenp, unsigned int *buffer_lengthp);
void recycle_variable_buffer (char *buffer, unsigned int length);
#endif /* CONFIG_WITH_VALUE_LENGTH */
char *expand_argument (const char *str, const char *end);
#ifndef CONFIG_WITH_VALUE_LENGTH
char *
variable_expand_string (char *line, const char *string, long length);
#else  /* CONFIG_WITH_VALUE_LENGTH */
char *
variable_expand_string_2 (char *line, const char *string, long length, char **eol);
__inline static char *
variable_expand_string (char *line, const char *string, long length)
{
    char *ignored;
    return variable_expand_string_2 (line, string, length, &ignored);
}
#endif /* CONFIG_WITH_VALUE_LENGTH */
void install_variable_buffer (char **bufp, unsigned int *lenp);
void restore_variable_buffer (char *buf, unsigned int len);
#ifdef CONFIG_WITH_VALUE_LENGTH
void append_expanded_string_to_variable (struct variable *v, const char *value,
                                         unsigned int value_len, int append);
#endif

/* function.c */
#ifndef CONFIG_WITH_VALUE_LENGTH
int handle_function (char **op, const char **stringp);
#else
int handle_function (char **op, const char **stringp, const char *nameend, const char *eol);
#endif
int pattern_matches (const char *pattern, const char *percent, const char *str);
char *subst_expand (char *o, const char *text, const char *subst,
                    const char *replace, unsigned int slen, unsigned int rlen,
                    int by_word);
char *patsubst_expand_pat (char *o, const char *text, const char *pattern,
                           const char *replace, const char *pattern_percent,
                           const char *replace_percent);
char *patsubst_expand (char *o, const char *text, char *pattern, char *replace);
#ifdef CONFIG_WITH_COMMANDS_FUNC
char *func_commands (char *o, char **argv, const char *funcname);
#endif
#if defined (CONFIG_WITH_VALUE_LENGTH)
/* Avoid calling handle_function for every variable, do the
   basic checks in variable_expand_string_2. */
extern char func_char_map[256];
# define MAX_FUNCTION_LENGTH    12
# define MIN_FUNCTION_LENGTH    2
MY_INLINE const char *
may_be_function_name (const char *name, const char *eos)
{
  unsigned char ch;
  unsigned int len = name - eos;

  /* Minimum length is MIN + whitespace. Check this directly.
     ASSUMES: MIN_FUNCTION_LENGTH == 2 */

  if (MY_PREDICT_TRUE(len < MIN_FUNCTION_LENGTH + 1
                      || !func_char_map[(int)(name[0])]
                      || !func_char_map[(int)(name[1])]))
    return 0;
  if (MY_PREDICT_TRUE(!func_char_map[ch = name[2]]))
    return isspace (ch) ? name + 2 : 0;

  name += 3;
  if (len > MAX_FUNCTION_LENGTH)
    len = MAX_FUNCTION_LENGTH - 3;
  else if (len == 3)
    len -= 3;
  if (!len)
    return 0;

  /* Loop over the remaining possiblities. */

  while (func_char_map[ch = *name])
    {
      if (!len--)
        return 0;
      name++;
    }
  if (ch == '\0' || isblank (ch))
    return name;
  return 0;
}
#endif /* CONFIG_WITH_VALUE_LENGTH */

/* expand.c */
#ifndef CONFIG_WITH_VALUE_LENGTH
char *recursively_expand_for_file (struct variable *v, struct file *file);
#define recursively_expand(v)   recursively_expand_for_file (v, NULL)
#else
char *recursively_expand_for_file (struct variable *v, struct file *file,
                                   unsigned int *value_lenp);
#define recursively_expand(v)   recursively_expand_for_file (v, NULL, NULL)
#endif

/* variable.c */
struct variable_set_list *create_new_variable_set (void);
void free_variable_set (struct variable_set_list *);
struct variable_set_list *push_new_variable_scope (void);
void pop_variable_scope (void);
void define_automatic_variables (void);
void initialize_file_variables (struct file *file, int reading);
void print_file_variables (const struct file *file);
void print_variable_set (struct variable_set *set, char *prefix);
void merge_variable_set_lists (struct variable_set_list **to_list,
                               struct variable_set_list *from_list);
#ifndef CONFIG_WITH_VALUE_LENGTH
struct variable *do_variable_definition (const struct floc *flocp,
                                         const char *name, const char *value,
                                         enum variable_origin origin,
                                         enum variable_flavor flavor,
                                         int target_var);
#else  /* CONFIG_WITH_VALUE_LENGTH */
# define do_variable_definition(flocp, varname, value, origin, flavor, target_var) \
    do_variable_definition_2 ((flocp), (varname), (value), ~0U, 0, NULL, \
                              (origin), (flavor), (target_var))
struct variable *do_variable_definition_2 (const struct floc *flocp,
                                           const char *varname,
                                           const char *value,
                                           unsigned int value_len,
                                           int simple_value, char *free_value,
                                           enum variable_origin origin,
                                           enum variable_flavor flavor,
                                           int target_var);
#endif /* CONFIG_WITH_VALUE_LENGTH */
char *parse_variable_definition (const char *line,
                                          enum variable_flavor *flavor);
struct variable *assign_variable_definition (struct variable *v, char *line IF_WITH_VALUE_LENGTH_PARAM(char *eos));
struct variable *try_variable_definition (const struct floc *flocp, char *line
                                          IF_WITH_VALUE_LENGTH_PARAM(char *eos),
                                          enum variable_origin origin,
                                          int target_var);
void init_hash_global_variable_set (void);
void hash_init_function_table (void);
struct variable *lookup_variable (const char *name, unsigned int length);
struct variable *lookup_variable_in_set (const char *name, unsigned int length,
                                         const struct variable_set *set);

#ifdef CONFIG_WITH_VALUE_LENGTH
void append_string_to_variable (struct variable *v, const char *value,
                                unsigned int value_len, int append);

struct variable *define_variable_in_set (const char *name, unsigned int length,
                                         const char *value,
                                         unsigned int value_length,
                                         int duplicate_value,
                                         enum variable_origin origin,
                                         int recursive,
                                         struct variable_set *set,
                                         const struct floc *flocp);

/* Define a variable in the current variable set.  */

#define define_variable(n,l,v,o,r) \
          define_variable_in_set((n),(l),(v),~0U,1,(o),(r),\
                                 current_variable_set_list->set,NILF)

#define define_variable_vl(n,l,v,vl,dv,o,r) \
          define_variable_in_set((n),(l),(v),(vl),(dv),(o),(r),\
                                 current_variable_set_list->set,NILF)

/* Define a variable with a constant name in the current variable set.  */

#define define_variable_cname(n,v,o,r) \
          define_variable_in_set((n),(sizeof (n) - 1),(v),~0U,1,(o),(r),\
                                 current_variable_set_list->set,NILF)

/* Define a variable with a location in the current variable set.  */

#define define_variable_loc(n,l,v,o,r,f) \
          define_variable_in_set((n),(l),(v),~0U,1,(o),(r),\
                                 current_variable_set_list->set,(f))

/* Define a variable with a location in the global variable set.  */

#define define_variable_global(n,l,v,o,r,f) \
          define_variable_in_set((n),(l),(v),~0U,1,(o),(r),NULL,(f))

#define define_variable_vl_global(n,l,v,vl,dv,o,r,f) \
          define_variable_in_set((n),(l),(v),(vl),(dv),(o),(r),NULL,(f))

/* Define a variable in FILE's variable set.  */

#define define_variable_for_file(n,l,v,o,r,f) \
          define_variable_in_set((n),(l),(v),~0U,1,(o),(r),(f)->variables->set,NILF)

#else  /* !CONFIG_WITH_VALUE_LENGTH */

struct variable *define_variable_in_set (const char *name, unsigned int length,
                                         const char *value,
                                         enum variable_origin origin,
                                         int recursive,
                                         struct variable_set *set,
                                         const struct floc *flocp);

/* Define a variable in the current variable set.  */

#define define_variable(n,l,v,o,r) \
          define_variable_in_set((n),(l),(v),(o),(r),\
                                 current_variable_set_list->set,NILF)           /* force merge conflict */

/* Define a variable with a constant name in the current variable set.  */

#define define_variable_cname(n,v,o,r) \
          define_variable_in_set((n),(sizeof (n) - 1),(v),(o),(r),\
                                 current_variable_set_list->set,NILF)           /* force merge conflict */

/* Define a variable with a location in the current variable set.  */

#define define_variable_loc(n,l,v,o,r,f) \
          define_variable_in_set((n),(l),(v),(o),(r),\
                                 current_variable_set_list->set,(f))            /* force merge conflict */

/* Define a variable with a location in the global variable set.  */

#define define_variable_global(n,l,v,o,r,f) \
          define_variable_in_set((n),(l),(v),(o),(r),NULL,(f))                  /* force merge conflict */

/* Define a variable in FILE's variable set.  */

#define define_variable_for_file(n,l,v,o,r,f) \
          define_variable_in_set((n),(l),(v),(o),(r),(f)->variables->set,NILF)  /* force merge conflict */

#endif /* !CONFIG_WITH_VALUE_LENGTH */

void undefine_variable_in_set (const char *name, unsigned int length,
                                         enum variable_origin origin,
                                         struct variable_set *set);

/* Remove variable from the current variable set. */

#define undefine_variable_global(n,l,o) \
          undefine_variable_in_set((n),(l),(o),NULL)

/* Warn that NAME is an undefined variable.  */

#define warn_undefined(n,l) do{\
                              if (warn_undefined_variables_flag) \
                                error (reading_file, \
                                       _("warning: undefined variable `%.*s'"), \
                                (int)(l), (n)); \
                              }while(0)

char **target_environment (struct file *file);

struct pattern_var *create_pattern_var (const char *target,
                                        const char *suffix);

extern int export_all_variables;
#ifdef CONFIG_WITH_STRCACHE2
extern struct strcache2 variable_strcache;
#endif

#ifdef KMK
# define MAKELEVEL_NAME "KMK_LEVEL"
#else
#define MAKELEVEL_NAME "MAKELEVEL"
#endif
#define MAKELEVEL_LENGTH (sizeof (MAKELEVEL_NAME) - 1)

