/* $Id: kmkdocs.c 61 2003-12-13 23:18:38Z knut.osmundsen@oracle.com $
 *
 * kmk design and documentation.
 *
 * Copyright (c) 2002 knut st. osmundsen <bird@anduin.net>
 *
 *
 * This file is part of kBuild.
 *
 * kBuild is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * kBuild is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with kBuild; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

/** @design             kmk
 *
 * kmk is the make file interpreter and executer in kBuild. It is based on
 * FreeBSD make but some changes have been made.
 *
 *
 *
 * @subsection          Differences From BSD Make
 *
 * The main difference is that kmk doesn't make use of an external shell to execute
 * commands. It uses it's internal micro shell. This has a number of advantages
 * when it comes to portability. It also speeds up the execution a little bit.
 *
 * The other important difference is default makefile names and order. The names
 * and order is as follows:
 * <ol>
 *     <li>makefile.kmk
 *     <li>Makefile.kmk
 *     <li>makefile
 *     <li>Makefile
 * </ol>
 *
 *
 *
 *
 */
