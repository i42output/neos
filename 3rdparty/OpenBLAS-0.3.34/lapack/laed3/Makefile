TOPDIR	= ../..
include ../../Makefile.system

SBLASOBJS = slaed3_single.$(SUFFIX)
DBLASOBJS = dlaed3_single.$(SUFFIX)

ifdef SMP
SBLASOBJS += slaed3_parallel.$(SUFFIX)
DBLASOBJS += dlaed3_parallel.$(SUFFIX)
endif

ifeq "$(or $(BUILD_SINGLE),$(BUILD_COMPLEX))" ""
SBLASOBJS=
endif
ifeq "$(or $(BUILD_DOUBLE),$(BUILD_COMPLEX16))" ""
DBLASOBJS=
endif

slaed3_single.$(SUFFIX) : laed3_single.c
	$(CC) -c $(CFLAGS) -UCOMPLEX -UDOUBLE $< -o $(@F)
dlaed3_single.$(SUFFIX) : laed3_single.c
	$(CC) -c $(CFLAGS) -UCOMPLEX -DDOUBLE $< -o $(@F)
slaed3_parallel.$(SUFFIX) : laed3_parallel.c
	$(CC) -c $(CFLAGS) -UCOMPLEX -UDOUBLE $< -o $(@F)
dlaed3_parallel.$(SUFFIX) : laed3_parallel.c
	$(CC) -c $(CFLAGS) -UCOMPLEX -DDOUBLE $< -o $(@F)
slaed3_single.$(PSUFFIX) : laed3_single.c
	$(CC) -c $(PFLAGS) -UCOMPLEX -UDOUBLE $< -o $(@F)
dlaed3_single.$(PSUFFIX) : laed3_single.c
	$(CC) -c $(PFLAGS) -UCOMPLEX -DDOUBLE $< -o $(@F)
slaed3_parallel.$(PSUFFIX) : laed3_parallel.c
	$(CC) -c $(PFLAGS) -UCOMPLEX -UDOUBLE $< -o $(@F)
dlaed3_parallel.$(PSUFFIX) : laed3_parallel.c
	$(CC) -c $(PFLAGS) -UCOMPLEX -DDOUBLE $< -o $(@F)

include ../../Makefile.tail
