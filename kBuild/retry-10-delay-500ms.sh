#!/usr/bin/env kmk_ash
# $Id: retry-10-delay-500ms.sh 3597 2023-06-16 20:56:17Z knut.osmundsen@oracle.com $
## @file
# kBuild Helper Script - Retry execution of command 10 times with 0.5 seconds delay.
#

#
# Copyright (c) 2023 knut st. osmundsen <bird-kBuild-spam-xxiii@anduin.net>
#
# This file is part of kBuild.
#
# kBuild is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# kBuild is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with kBuild; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
#
#
# As a special exception you are granted permission to include this file, via
# the kmk include directive, as you wish without this in itself causing the
# resulting makefile, program or whatever to be covered by the GPL license.
# This exception does not however invalidate any other reasons why the makefile,
# program, whatever should not be covered the GPL.
#
#

# First try...
"$@" && exit 0
MY_EXITCODE=$?

# Retry up to five times with a little delay between each.
for MY_RETRY in 1 2 3 4 5 6 7 8 9;
do
    echo "retry-10-delay-500ms.sh: Retry #${MY_RETRY} failed: ${MY_EXITCODE} (cmd: $*)"
    kmk_sleep 500ms
    "$@" && exit 0
    MY_EXITCODE=$?
done
echo "retry-10-delay-500ms.sh: Giving up after 10 tries: ${MY_EXITCODE} (cmd: $*)" >&2
exit ${MY_EXITCODE}

