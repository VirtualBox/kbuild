VAR1=olemann

VAR2=$(VAR1:ole=^
nextline)

INCLUDES=-I. -I.. -Ig:\kTaskMgr\tree\Generic\include
VAR3=$(INCLUDES:-I=-i )

all:
	echo VAR3: $(VAR3)
	echo VAR1: $(VAR1)
	echo VAR2: $(VAR2)
	
