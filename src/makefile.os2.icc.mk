# $Id: makefile.os2.icc.mk 48 2003-04-05 03:22:01Z knut.osmundsen@oracle.com $
#
# OS/2 VAC++ v3.08 bootstrap makefile.
#
# Copyright (c) 2003 knut st. osmundsen <bird@anduin.net>
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


# misc variables
MAKEFILE    = makefile.os2.icc.mk
POSTFIX     =
!ifdef DEBUG
POSTFIX     = .dbg
!endif
!ifdef PROFILE
POSTFIX     = .prf
!endif
OBJDIR      = ..\obj\os2-icc-kmk.$(POSTFIX)

# paths
PATH_KLIB   = g:\ktaskmgr\tree
# PATH_TOOLKIT, PATH_VAC308,.. is defined in the environment.

# compiler setup
!ifdef DEBUG
CFLAGS_1    = /O- -DDEBUG
!endif
!ifdef PROFILE
CFLAGS_1    = /O+ /Gh
!endif
CFLAGS      = /Q /Ti+ /Gm /Ge /Gl -DOS2 -D__i386__ -DKMK -I$(PATH_KLIB)\Generic\include -I$(PATH_TOOLKIT)\h -I$(PATH_VAC308)\include $(CFLAGS_1)
CFLAGS_KMK  = /IkMk\include $(CFLAGS)

# linker setup
!ifdef DEBUG
LDFLAGS_1   = /NOEXEPACK
!endif
!ifdef PROFILE
LDFLAGS_1   =
!endif
!ifndef LDFLAGS_1 #releas
LDFLAGS_1   = /EXEPACK:2 /Packcode /Packdata
!endif
LDFLAGS     = /NoLogo /NoExtDictionary /Optfunc /Base:0x10000 /Map /Linenumbers /Debug /PmType:vio $(LDFLAGS_1)


# inference rules.
{.}.c{$(OBJDIR)}.obj:
    $(CC) -c $(CFLAGS) -Fo$(OBJDIR)\$(@F) $(MAKEDIR)\$(<F)

{.\kShell}.c{$(OBJDIR)}.obj:
    $(CC) -c $(CFLAGS) -Fo$(OBJDIR)\$(@F) $(MAKEDIR)\kShell\$(<F)

{.\kMk}.c{$(OBJDIR)}.obj:
    $(CC) -c $(CFLAGS_KMK) -Fo$(OBJDIR)\$(@F) $(MAKEDIR)\kMk\$(<F)

{.\kMk\lst.lib}.c{$(OBJDIR)}.obj:
    $(CC) -c $(CFLAGS_KMK) -Fo$(OBJDIR)\$(@F) $(MAKEDIR)\kMk\lst.lib$(<F)


# object files
OBJS_KSHELL = \
$(OBJDIR)\kShell.obj

OBJS_KMK = \
#$(OBJDIR)\arch.obj\
$(OBJDIR)\buf.obj\
$(OBJDIR)\compat.obj\
$(OBJDIR)\cond.obj\
$(OBJDIR)\dir.obj\
$(OBJDIR)\for.obj\
$(OBJDIR)\hash.obj\
$(OBJDIR)\job.obj\
$(OBJDIR)\main.obj\
$(OBJDIR)\make.obj\
$(OBJDIR)\parse.obj\
$(OBJDIR)\str.obj\
$(OBJDIR)\suff.obj\
$(OBJDIR)\targ.obj\
$(OBJDIR)\var.obj\
$(OBJDIR)\util.obj\
$(OBJDIR)\helpers.obj\
\
$(OBJDIR)\lstAppend.obj       \
$(OBJDIR)\lstAtEnd.obj        \
$(OBJDIR)\lstAtFront.obj      \
$(OBJDIR)\lstClose.obj        \
$(OBJDIR)\lstConcat.obj       \
$(OBJDIR)\lstDatum.obj        \
$(OBJDIR)\lstDeQueue.obj      \
$(OBJDIR)\lstDestroy.obj      \
$(OBJDIR)\lstDupl.obj         \
$(OBJDIR)\lstEnQueue.obj      \
$(OBJDIR)\lstFind.obj         \
$(OBJDIR)\lstFindFrom.obj     \
$(OBJDIR)\lstFirst.obj        \
$(OBJDIR)\lstForEach.obj      \
$(OBJDIR)\lstForEachFrom.obj  \
$(OBJDIR)\lstInit.obj         \
$(OBJDIR)\lstInsert.obj       \
$(OBJDIR)\lstIsAtEnd.obj      \
$(OBJDIR)\lstIsEmpty.obj      \
$(OBJDIR)\lstLast.obj         \
$(OBJDIR)\lstMember.obj       \
$(OBJDIR)\lstNext.obj         \
$(OBJDIR)\lstOpen.obj         \
$(OBJDIR)\lstRemove.obj       \
$(OBJDIR)\lstReplace.obj      \
$(OBJDIR)\lstSucc.obj

OBJS_KDEPEND = \
$(OBJDIR)\fastdep.obj

# sum objs.
OBJS = $(OBJDIR)\mainmain.obj $(OBJS_KSHELL) $(OBJS_KMK) #$(OBJS_KDEPEND)


# libs
LIBS = \
!ifdef DEBUG
$(PATH_KLIB)\lib\debug\kLib.lib \
!else
$(PATH_KLIB)\lib\debug\kLib.lib \
!ifdef PROFILE
$(PATH_KLIB)\lib\debug\kProfile.lib \
!endif
!endif
$(PATH_TOOLKIT)\os2386.lib \
$(VAC308_TOOLKIT)\cppom30.lib \
                    
                
# the rules             
all: $(OBJDIR) $(OBJDIR)\kMk.exe


$(OBJDIR):
    -mkdir ..\obj
    -mkdir $(OBJDIR)

$(OBJDIR)\kMk.exe: $(OBJS)
    $(LD) $(LDFLAGS) @<<$(OBJDIR)\$(@F).lnk
/OUT:$(OBJDIR)\$(@F).exe
/MAP:$(OBJDIR)\$(@F).map
$(OBJS)
$(LIBS)
<<KEEP

