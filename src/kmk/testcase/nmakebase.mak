all: /usr/tmp/mytarget.exe

/usr/tmp/mytarget.exe: /root/tst.mak /root/lst
	@echo '$$< = '$<
	@echo '$$@ = '$@
	@echo '$$! = '$!
	@echo '$$? = '$?
	@echo '$$* = '$?
	@echo '$$** = '$?
	
	@echo '$$(@F) = '$(@F)
	@echo '$$(@D) = '$(@D)
	@echo '$$(@R) = '$(@R)
	@echo '$$(@B) = '$(@B)
	
	


