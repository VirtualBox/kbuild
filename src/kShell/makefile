# $Id: makefile 21 2002-11-22 01:36:57Z knut.osmundsen@oracle.com $

all: kShellMain.exe \
     kShell.lib 	
			
kShellMain.exe: kShellMain.c kShell.c kShell.h
	icc /Q /Ti+ kShellmain.c kShell.c
	
kShellMain.obj: kShellMain.c kShell.h
	icc /Q /c /Ti+ kShellMain.c

kShell.obj: kShell.c kShell.h
	icc /Q /c /Ti+ kShell.c
	
kShell.lib:
	-del kShell.lib
	ilib kShell.lib +kShell.obj;
