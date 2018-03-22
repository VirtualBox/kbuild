/* $Id: nt_child_inject_standard_handles.h 3179 2018-03-22 19:50:04Z knut.osmundsen@oracle.com $ */
/** @file
 * Injecting standard handles into a child process.
 */

/*
 * Copyright (c) 2004-2018 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
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

#ifndef ___nt_child_inject_standard_handles
#define ___nt_child_inject_standard_handles

int nt_child_inject_standard_handles(HANDLE hProcess, BOOL pafReplace[3], HANDLE pahHandles[3], char *pszErr, size_t cbErr);

#endif

