#	@(#)Makefile	5.2 (Berkeley) 12/28/90
# $FreeBSD: src/usr.bin/make/Makefile,v 1.13.2.1 2001/05/25 08:33:40 sobomax Exp $

CC = icc
CFLAGS = /Q /Ti+ /Ss+ /Ge+ /I. /I./include /I../kLib/Generic/include \
         -DUSE_KLIB -DOS2 -D__i386__ -D__32BIT__ -DNMAKE=1 -DMACHINE=\"ibmos2\" -DMACHINE_ARCH=\"x86\" -DMACHINE_CPU=\"386\" \

OBJDIR=obj.icc

LSTOBJS=\
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

BASEOBJS=\
$(OBJDIR)\arch.obj\
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

INCOBJS=\
$(OBJDIR)\dirent.obj\


all: kmk.exe

kmk.exe: $(BASEOBJS) $(LSTOBJS) $(INCOBJS)
    $(CC) @<<
$(CFLAGS) $** /Fm$(@F).map /Fe$@ ..\klib\lib\debug\klib.lib
<<

$(LSTOBJS): lst.lib\$(@B).c list.h lst.h
    @if not exist $(OBJDIR) mkdir $(OBJDIR)
    $(CC) -c $(CFLAGS) /Fo$(OBJDIR)/$(@B) lst.lib/$(@B).c

$(BASEOBJS): $(@B).c list.h lst.h make.h
    @if not exist $(OBJDIR) mkdir $(OBJDIR)
    $(CC) -c $(CFLAGS) /Fo$(OBJDIR)/$(@B) $(@B).c

$(INCOBJS): include\$(@B).c list.h lst.h make.h
    @if not exist $(OBJDIR) mkdir $(OBJDIR)
    $(CC) -c $(CFLAGS) /Fo$(OBJDIR)/$(@B) include/$(@B).c


#
#PROG=	make
#CFLAGS+= -I${.CURDIR}
#SRCS=	arch.c buf.c compat.c cond.c dir.c for.c hash.c job.c main.c \
#	make.c parse.c str.c suff.c targ.c var.c util.c
#SRCS+=	lstAppend.c lstAtEnd.c lstAtFront.c lstClose.c lstConcat.c \
#	lstDatum.c lstDeQueue.c lstDestroy.c lstDupl.c lstEnQueue.c \
#	lstFind.c lstFindFrom.c lstFirst.c lstForEach.c lstForEachFrom.c \
#	lstInit.c lstInsert.c lstIsAtEnd.c lstIsEmpty.c lstLast.c \
#	lstMember.c lstNext.c lstOpen.c lstRemove.c lstReplace.c lstSucc.c
#.PATH:	${.CURDIR}/lst.lib

