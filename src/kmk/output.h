/* Output to stdout / stderr for GNU make
Copyright (C) 2013-2016 Free Software Foundation, Inc.
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

#ifndef INCLUDED_MAKE_OUTPUT_H
#define INCLUDED_MAKE_OUTPUT_H
#include <stdio.h> /* darwin*/

#ifdef CONFIG_WITH_OUTPUT_IN_MEMORY
/*  Output run. */
struct output_run
{
    unsigned int seqno;         /* For interleaving out/err output. */
    unsigned int len;           /* The length of the output. */
    struct output_run *next;    /* Pointer to the next run. */
};

/* Output segment. */
struct output_segment
{
    struct output_segment *next;
    size_t size;                 /* Segment size, everything included. */
    struct output_run runs[1];
};

/* Output memory buffer. */
struct output_membuf
{
    struct output_run     *head_run;
    struct output_run     *tail_run; /* Always in tail_seg. */
    struct output_segment *head_seg;
    struct output_segment *tail_seg;
    size_t left;                /* Number of bytes that can be appended to
                                   the tail_run.  */
    size_t total;               /* Total segment allocation size.  */
};
#endif /* CONFIG_WITH_OUTPUT_IN_MEMORY */

struct output
  {
#ifdef CONFIG_WITH_OUTPUT_IN_MEMORY
    struct output_membuf out;
    struct output_membuf err;
    unsigned int seqno;         /* The current run sequence number. */
#else
    int out;
    int err;
#endif
    unsigned int syncout:1;     /* True if we want to synchronize output.  */
 };

extern struct output *output_context;
extern unsigned int stdio_traced;

#define OUTPUT_SET(_new)    do{ output_context = (_new)->syncout ? (_new) : NULL; }while(0)
#define OUTPUT_UNSET()      do{ output_context = NULL; }while(0)

#define OUTPUT_TRACED()     do{ stdio_traced = 1; }while(0)
#define OUTPUT_IS_TRACED()  (!!stdio_traced)

FILE *output_tmpfile (char **, const char *);

/* Initialize and close a child output structure: if NULL do this program's
   output (this should only be done once).  */
void output_init (struct output *out);
void output_close (struct output *out);

/* In situations where output may be about to be displayed but we're not
   sure if we've set it up yet, call this.  */
void output_start (void);

/* Show a message on stdout or stderr.  Will start the output if needed.  */
void outputs (int is_err, const char *msg);
#ifdef CONFIG_WITH_OUTPUT_IN_MEMORY
ssize_t output_write_bin (struct output *out, int is_err, const char *src, size_t len);
ssize_t output_write_text (struct output *out, int is_err, const char *src, size_t len);
#endif

#ifndef NO_OUTPUT_SYNC
int output_tmpfd (void);
/* Dump any child output content to stdout, and reset it.  */
void output_dump (struct output *out);
#endif

#endif /* INLCUDED_MAKE_OUTPUT_H */

