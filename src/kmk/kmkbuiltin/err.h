/* $Id: err.h 2019 2008-11-02 00:21:05Z knut.osmundsen@oracle.com $ */
/** @file
 * Override err.h stuff so we get the program names right.
 */

/*
 * Copyright (c) 2005-2008 knut st. osmundsen <bird-src-spam@anduin.net>
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

extern const char *g_progname;
int err(int eval, const char *fmt, ...);
int errx(int eval, const char *fmt, ...);
void warn(const char *fmt, ...);
void warnx(const char *fmt, ...);

#endif

