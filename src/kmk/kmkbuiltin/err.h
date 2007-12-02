/* $Id: err.h 1309 2007-12-02 04:53:40Z knut.osmundsen@oracle.com $ */
/** @file
 *
 * Override err.h stuff so we get the program names right.
 *
 * Copyright (c) 2005-2007 knut st. osmundsen <bird-kBuild-spam@anduin.net>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with This program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
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

