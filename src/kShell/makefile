# $Id: makefile 44 2003-03-28 11:22:18Z knut.osmundsen@oracle.com $

#CC = icc /Q /Ti+ /I../kLib/Generic/include
CC = icc /Q /Ti+ -DOS2 -D__i386__ -DDEBUG /I/ktaskmgr/tree/Generic/include
CC = icc /Q /Ti+ /O /Oi+ -DOS2 -D__i386__ -DDEBUG /I/ktaskmgr/tree/Generic/include


all: kShellMain.exe \
     kShell.lib 	
			
kShellMain.exe: kShellMain.c kShell.c kShell.h
	$(CC) kShellmain.c kShell.c /ktaskmgr/tree/lib/debug/klib.lib
	
kShellMain.obj: kShellMain.c kShell.h
	$(CC) /c kShellMain.c

kShell.obj: kShell.c kShell.h
	$(CC) /c kShell.c
	
kShell.lib:
	-del kShell.lib
	ilib kShell.lib +kShell.obj;
