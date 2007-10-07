/* $Id: $ */
/** @file
 *
 * The shell instance methods.
 *
 * Copyright (c) 2007 knut st. osmundsen <bird-src-spam@anduin.net>
 *
 *
 * This file is part of kBuild.
 *
 * kBuild is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * kBuild is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with kBuild; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include "shinstance.h"


/**
 * Creates a root shell instance.
 *
 * @param   inherit     The shell to inherit from. If NULL inherit from environment and such.
 * @param   argc        The argument count.
 * @param   argv        The argument vector.
 *
 * @returns pointer to root shell on success, NULL on failure.
 */
shinstance *sh_create_root_shell(shinstance *inherit, int argc, char **argv)
{
    shinstance *psh;

    psh = malloc(sizeof(*psh));
    if (psh)
    {
        memset(psh, 0, sizeof(*psh));

        /* memalloc.c */
        psh->stacknleft = MINSIZE;
        psh->herefd = -1;
        psh->stackp = &psh->stackbase;
        psh->stacknxt = psh->stackbase.space;

        /* input.c */
        psh->plinno = 1;
        psh->init_editline = 0;
        psh->parsefile = &psh->basepf;

        /* output.c */
        psh->output.bufsize = OUTBUFSIZ;
        psh->output.fd = 1;
        psh->errout.bufsize = 100;
        psh->errout.fd = 2;
        psh->memout.fd = MEM_OUT;
        psh->out1 = &psh->output;
        psh->out2 = &psh->errout;

        /* jobs.c */
        psh->backgndpid = -1;
#if JOBS
        psh->curjob = -1;
#endif
        psh->ttyfd = -1;

    }
    return psh;
}


