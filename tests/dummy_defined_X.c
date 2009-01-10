/* $Id: dummy_defined_X.c 2243 2009-01-10 02:24:02Z knut.osmundsen@oracle.com $ */
/** @file
 * Tests - Dummy test program checking that X == 42, possibly doing this via y.
 */

/*
 * Copyright (c) 2008-2009 knut st. osmundsen <bird-kBuild-spamix@anduin.net>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifndef X
# error "X isn't defined, test the is busted."
#endif

#ifndef y
# define y 42
#endif

#if X != 42
# error "X != 42"
#endif

int main()
{
    return 0;
}

