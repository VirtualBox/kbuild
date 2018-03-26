/* $Id: err.h 3192 2018-03-26 20:25:56Z knut.osmundsen@oracle.com $ */
/** @file
 * Override err.h stuff so we get the program names right.
 */

/*
 * Copyright (c) 2005-2010 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
 *
 * This file is part of kBuild.
 *
 * kBuild is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * kBuild is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with kBuild.  If not, see <http://www.gnu.org/licenses/>
 *
 */

#ifndef ___err_h
#define ___err_h

#include "../kmkbuiltin.h"

int  err(PKMKBUILTINCTX pCtx, int eval, const char *fmt, ...);
int  errx(PKMKBUILTINCTX pCtx, int eval, const char *fmt, ...);
void warn(PKMKBUILTINCTX pCtx, const char *fmt, ...);
void warnx(PKMKBUILTINCTX pCtx, const char *fmt, ...);
void kmk_builtin_ctx_printf(PKMKBUILTINCTX pCtx, int fIsErr, const char *pszFormat, ...);

#endif

