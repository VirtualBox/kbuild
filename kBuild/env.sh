#!/bin/bash
# $Id: env.sh 557 2006-09-28 15:56:25Z knut.osmundsen@oracle.com $
## @file
#
# Environment setup script.
#
# Copyright (c) 2005 knut st. osmundsen <bird@innotek.de>
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
#set -x

# kBuild path.
if [ -z "$PATH_KBUILD" ]; then
    PATH_KBUILD=`dirname "$0"`
    PATH_KBUILD=`cd "$PATH_KBUILD" ; /bin/pwd`
fi
if [ ! -f "$PATH_KBUILD/footer.kmk" -o ! -f "$PATH_KBUILD/header.kmk" -o ! -f "$PATH_KBUILD/rules.kmk" ]; then
    echo "$0: error: PATH_KBUILD ($PATH_KBUILD) is not pointing to a popluated kBuild directory.";
    sleep 1;
    exit 1;
fi
export PATH_KBUILD
echo "dbg: PATH_KBUILD=$PATH_KBUILD"

# Type.
if [ -z "$BUILD_TYPE" ]; then
    BUILD_TYPE=debug
fi
export BUILD_TYPE
echo "dbg: BUILD_TYPE=$BUILD_TYPE"


# Host platform.
if [ -z "$BUILD_PLATFORM_CPU" ]; then
    BUILD_PLATFORM_CPU=`uname -m`
    case "$BUILD_PLATFORM_CPU" in
        x86_64|AMD64)
           BUILD_PLATFORM_CPU='k8'
           ;;
    esac
fi
export BUILD_PLATFORM_CPU
echo "dbg: BUILD_PLATFORM_CPU=$BUILD_PLATFORM_CPU"

if [ -z "$BUILD_PLATFORM_ARCH" ]; then
    case "$BUILD_PLATFORM_CPU" in
        i[3456789]86)
            BUILD_PLATFORM_ARCH='x86'
            ;;
        k8|k8l|k9|k10)
            BUILD_PLATFORM_ARCH='amd64'
            ;;
        *)  echo "$0: unknown cpu/arch - $BUILD_PLATFORM_CPU"
            sleep 1
            exit 1
            ;;
    esac

fi
export BUILD_PLATFORM_ARCH
echo "dbg: BUILD_PLATFORM_ARCH=$BUILD_PLATFORM_ARCH"


if [ -z "$BUILD_PLATFORM" ]; then
    BUILD_PLATFORM=`uname`
    case "$BUILD_PLATFORM" in
        linux|Linux|GNU/Linux|LINUX)
            BUILD_PLATFORM=linux
            ;;

        os2|OS/2|OS2)
            BUILD_PLATFORM=os2
            ;;

        freebsd|FreeBSD|FREEBSD)
            BUILD_PLATFORM=freebsd
            ;;

        openbsd|OpenBSD|OPENBSD)
            BUILD_PLATFORM=openbsd
            ;;

        netbsd|NetBSD|NETBSD)
            BUILD_PLATFORM=netbsd
            ;;

        Darwin|darwin)
            BUILD_PLATFORM=darwin
            ;;

        *)
            echo "$0: unknown os $BUILD_PLATFORM"
            sleep 1
            exit 1
            ;;
    esac
fi
export BUILD_PLATFORM
echo "dbg: BUILD_PLATFORM=$BUILD_PLATFORM"


# Target platform.
if [ -z "$BUILD_TARGET_CPU" ]; then
    BUILD_TARGET_CPU=$BUILD_PLATFORM_CPU
fi
export BUILD_TARGET_CPU
echo "dbg: BUILD_TARGET_CPU=$BUILD_TARGET_CPU"

if [ -z "$BUILD_TARGET_ARCH" ]; then
    case "$BUILD_TARGET_CPU" in
        i[3456789]86)
            BUILD_TARGET_ARCH='x86'
            ;;
        k8|k8l|k9|k10)
            BUILD_TARGET_ARCH='amd64'
            ;;
        *)  echo "$0: unknown cpu/arch - $BUILD_TARGET_CPU"
            sleep 1
            exit 1
            ;;
    esac

fi
export BUILD_TARGET_ARCH
echo "dbg: BUILD_TARGET_ARCH=$BUILD_TARGET_ARCH"

if [ -z "$BUILD_TARGET" ]; then
    BUILD_TARGET=$BUILD_PLATFORM
fi
export BUILD_TARGET
echo "dbg: BUILD_TARGET=$BUILD_TARGET"


# Determin executable extension and path separator.
_SUFF_EXE=
_PATH_SEP=":"
case "$BUILD_PLATFORM" in
    os2|win32|winnt|win2k|winxp)
        _SUFF_EXE=".exe"
        _PATH_SEP=";"
        ;;
esac

# Make shell
export MAKESHELL="$PATH_KBUILD/bin/$BUILD_PLATFORM_ARCH.$BUILD_PLATFORM/kmk_ash${_SUFF_EXE}";

# The PATH.
PATH="$PATH_KBUILD/bin/$BUILD_PLATFORM_ARCH.$BUILD_PLATFORM/${_PATH_SEP}$PATH"
export PATH
echo "dbg: PATH=$PATH"

# Sanity and x bits.
if [ ! -d "$PATH_KBUILD/bin/$BUILD_PLATFORM_ARCH.$BUILD_PLATFORM/" ]; then
    echo "$0: warning: The bin directory for this platform doesn't exists. ($PATH_KBUILD/bin/$BUILD_PLATFORM_ARCH.$BUILD_PLATFORM/)"
else
    for prog in kmk kDepPre kDepIDB kmk_append kmk_ash kmk_cat kmk_cp kmk_echo kmk_install kmk_ln kmk_mkdir kmk_mv kmk_rm kmk_sed;
    do
        chmod a+x $PATH_KBUILD/bin/$BUILD_PLATFORM_ARCH.$BUILD_PLATFORM/${prog} > /dev/null 2>&1
        if [ ! -f "$PATH_KBUILD/bin/$BUILD_PLATFORM_ARCH.$BUILD_PLATFORM/${prog}${_SUFF_EXE}" ]; then
            echo "$0: warning: The ${prog} program doesn't exist for this platform. ($PATH_KBUILD/bin/$BUILD_PLATFORM_ARCH.$BUILD_PLATFORM/${prog}${_SUFF_EXE})"
        fi
    done
fi

unset _SUFF_EXE
unset _PATH_SEP

# Execute command or spawn shell.
if [ $# -eq 0 ]; then
    echo "$0: info: Spawning work shell..."
    if [ "$TERM" != 'dumb'  ] && [ -n "$BASH" ]; then
        export PS1='\[\033[01;32m\]\u@\h \[\033[01;34m\]\W \$ \[\033[00m\]'
    fi
    $SHELL -i
else
    echo "$0: info: Executing command: $*"
    $*
fi
