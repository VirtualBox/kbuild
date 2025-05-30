# $Id: Makefile 2413 2010-09-11 17:43:04Z knut.osmundsen@oracle.com $

#
# Odin32 API
#
#   Makefile for the Quick-and-Dirty dependency utility. (FastDep)
#
#   Copyright (c) 1999-2010 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
#
#   GPL
#


!ifdef BUILD_SETUP_MAK


#
# Setup config
#
ALL_NO_DBGMEM = 1
PATH_ROOT = ..\..
!include $(PATH_ROOT)\$(BUILD_SETUP_MAK)

#
# Target config
#
TARGET_NAME = fastdep
TARGET_MODE = EXE
TARGET_NEEDED = 1
TARGET_PUB_BASE = $(PATH_TOOLS)

TARGET_OBJS =\
$(PATH_TARGET)\fastdep.$(EXT_OBJ)\
$(PATH_TARGET)\avl.$(EXT_OBJ)\

TARGET_LIBS =\
$(LIB_OS)\
$(LIB_C_OBJ)

#
# Rules config
#
RULES_FORWARD =
!include $(MAKE_INCLUDE_PROCESS)


!else
#
# Directory macro.
#
ODIN32_BIN     = $(ODIN32_TOOLS)


#
# Tell buildenvironment that we're making an VIO .exe.
# Tell buildenvironment that we like to use static linked CRT.
# Tell buildenvironment that we should not copy this into /bin.
#
EXETARGET = 1
VIO = 1
STATIC_CRT = 1
NO_MAIN_BIN_COPY = 1


#
# include common definitions
#
!include ../../makefile.inc


#
# Addjust common definitions
#
!if "$(VAC3)" == "1" || "$(VAC36)" == "1"
CFLAGS   = $(CFLAGS) -W3 -Wall+ppt-ppc-inl-cnv-gnr-vft-gen-uni-ext- \
!ifdef DEBUG
        -O+ -Tm-
!endif
!endif


#
# Object files. Prefix with OBJDIR and one space before the '\'.
#
OBJS = \
$(OBJDIR)\fastdep.obj \
$(OBJDIR)\avl.obj


#
# Libraries. One space before the '\'.
#
LIBS = \
$(RTLLIB) \
os2386.lib


#
# Target name - name of the exe without extention and path.
#
TARGET = FastDep


#
# Includes the common rules.
#
!include $(ODIN32_POST_INC)


#
# We need all.
#
needed: all
!endif


#
# NT version using Watcom C/C++.
#
fastdepnt.exe:
    wcl386 -bt=nt -l=nt -d2 /hc /"option map" -DOS2FAKE=1 -I$(WATCOM)\h\nt fastdep.c avl.c os2fake-win.c /Fe=$@ kernel32.lib
