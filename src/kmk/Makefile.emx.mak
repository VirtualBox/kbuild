#	@(#)Makefile	5.2 (Berkeley) 12/28/90
# $FreeBSD: src/usr.bin/make/Makefile,v 1.13.2.1 2001/05/25 08:33:40 sobomax Exp $

CC = gcc
CFLAGS = -g -I. -I./include -I../kLib/Generic/include \
#         -DUSE_KLIB \
         -DOS2 -D__i386__ -D__32BIT__ -DNMAKE -DMACHINE=\"ibmos2\" -DMACHINE_ARCH=\"x86\" -DMACHINE_CPU=\"386\" \

OBJDIR=obj.emx

LSTOBJS=\
$(OBJDIR)\lstAppend.o       \
$(OBJDIR)\lstAtEnd.o        \
$(OBJDIR)\lstAtFront.o      \
$(OBJDIR)\lstClose.o        \
$(OBJDIR)\lstConcat.o       \
$(OBJDIR)\lstDatum.o        \
$(OBJDIR)\lstDeQueue.o      \
$(OBJDIR)\lstDestroy.o      \
$(OBJDIR)\lstDupl.o         \
$(OBJDIR)\lstEnQueue.o      \
$(OBJDIR)\lstFind.o         \
$(OBJDIR)\lstFindFrom.o     \
$(OBJDIR)\lstFirst.o        \
$(OBJDIR)\lstForEach.o      \
$(OBJDIR)\lstForEachFrom.o  \
$(OBJDIR)\lstInit.o         \
$(OBJDIR)\lstInsert.o       \
$(OBJDIR)\lstIsAtEnd.o      \
$(OBJDIR)\lstIsEmpty.o      \
$(OBJDIR)\lstLast.o         \
$(OBJDIR)\lstMember.o       \
$(OBJDIR)\lstNext.o         \
$(OBJDIR)\lstOpen.o         \
$(OBJDIR)\lstRemove.o       \
$(OBJDIR)\lstReplace.o      \
$(OBJDIR)\lstSucc.o

BASEOBJS=\
$(OBJDIR)\arch.o\
$(OBJDIR)\buf.o\
$(OBJDIR)\compat.o\
$(OBJDIR)\cond.o\
$(OBJDIR)\dir.o\
$(OBJDIR)\for.o\
$(OBJDIR)\hash.o\
$(OBJDIR)\job.o\
$(OBJDIR)\main.o\
$(OBJDIR)\make.o\
$(OBJDIR)\parse.o\
$(OBJDIR)\str.o\
$(OBJDIR)\suff.o\
$(OBJDIR)\targ.o\
$(OBJDIR)\var.o\
$(OBJDIR)\util.o\
$(OBJDIR)\helpers.o\

INCOBJS=\
$(OBJDIR)\dirent.o\


all: kmk.exe

kmk.exe: $(BASEOBJS) $(LSTOBJS) $(INCOBJS)
    $(CC) $(CFLAGS) -Zmap $** -o $@ -lregex

$(LSTOBJS): lst.lib\$(@B).c list.h lst.h
    @if not exist $(OBJDIR) mkdir $(OBJDIR)
    $(CC) -c $(CFLAGS) -o $(OBJDIR)/$(@F) lst.lib/$(@B).c

$(BASEOBJS): $(@B).c list.h lst.h make.h
    @if not exist $(OBJDIR) mkdir $(OBJDIR)
    $(CC) -c $(CFLAGS) -o $(OBJDIR)/$(@F) $(@B).c

$(INCOBJS): include\$(@B).c list.h lst.h make.h
    @if not exist $(OBJDIR) mkdir $(OBJDIR)
    $(CC) -c $(CFLAGS) -o $(OBJDIR)/$(@F) include/$(@B).c


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

