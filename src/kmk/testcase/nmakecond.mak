!ifndef MAKE
!error "MAKE not defined!"
!endif

# nmake syntax error:
#!if $(MAKE) == ""
#!error "MAKE not defined!"
#!endif

!if "$(MAKE)" == ""
!error "MAKE not defined!"
!endif


!if "$(__NOT_DEFINED_VARIABLE__)" != ""
!error "__NOT_DEFINED_VARIABLE__ is defined!"
!endif

!if "$(__NOT_DEFINED_VARIABLE__)x" != "x"
!error "__NOT_DEFINED_VARIABLE__+x is wrong!"
!endif

!if [echo ok]
!endif

			
!ifdef MAKE
!   include nmakeinclude.mak
!else
!   include nonexistinginclude.mak
!endif			





