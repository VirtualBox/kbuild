all: inlinefile.keep inlinefile.nokeep

				
inlinefile.keep:
	echo Creating <<$@
line 1
line 2
line 3
# comment like line, but it will be included.	
<<KEEP
	echo done


inlinefile.nokeep:
	echo Creating <<$@
# comment like line, but it will be included.	
line 1
line 2
line 3
<<NOKEEP	
	echo done
	
	
	

