#/bin/sh

# $Id: $
## @file
#
# kmk_append fake script for bootstrapping.
#
# Copyright (c) 2007 knut st. osmundsen <bird-src-spam@anduin.net>
#
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


set -e
set -x

# 
# Parse args.
#
NEWLINE=0
echo "$1"
while test "$1" = "-n"  -o  "$1" = "-v"  -o  "$1" = "-nv"  -o "$1" = "-vn";
do
    case "$1" in
        -n)
            NEWLINE=1
            ;;
        *)
            echo "gnumake-append.sh: Unsupported argument: $1"
            exit 1;
    esac
    shift
done

FILE="$1"
if test -z "$FILE"; then
    echo "gnumake-append.sh: No arguments given."
    echo "syntax: gnumake-append.sh [-n] <file> [string [more strings]]"
    exit 1
fi
shift


#
# Do work
#
if test "$#" -eq "0"; then
    echo "" >> "$FILE"
else
    if test "$NEWLINE" = "0"; then
        echo "$*" >> "$FILE"
    else
        while test "$#" -ne "0";
	do
            echo "$1" >> "$FILE"
            shift
        done
    fi        
fi
exit 0;

