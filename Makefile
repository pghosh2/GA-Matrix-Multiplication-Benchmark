# As of ga-5-1, the variables needed to build GA are determined from the
# ga-config script which is installed with GA.

ifndef GA_CONFIG
error:
	echo "you must set GA_CONFIG e.g. GA_CONFIG=/path/to/ga-config"
	exit 1
endif

CC       = $(shell $(GA_CONFIG) --cc)
F77      = $(shell $(GA_CONFIG) --f77)
CFLAGS   = $(shell $(GA_CONFIG) --cflags)
FFLAGS   = $(shell $(GA_CONFIG) --fflags)
CPPFLAGS = $(shell $(GA_CONFIG) --cppflags)
LDFLAGS  = $(shell $(GA_CONFIG) --ldflags)
LIBS     = $(shell $(GA_CONFIG) --libs)
FLIBS    = $(shell $(GA_CONFIG) --flibs)

# =========================================================================== 

FFLAGS += -O -g
CFLAGS += -O2 -g -std=gnu99 -DDEBUG=0
CPPFLAGS += -DUSE_MPI -DMPI

LINK = $(F77) -lm
LOADER_OPTS = -g
LIBS += -lgsl -lgslcblas

PROGRAMS =
PROGRAMS += spiral-mm.c
PROGRAMS += spiral-mm.x
PROGRAMS += test-mm.c
PROGRAMS += test-mm.x
PROGRAMS += spiral-mm-trans.c
PROGRAMS += spiral-mm-trans.x
PROGRAMS += test-mm-trans.c
PROGRAMS += test-mm-trans.x

.PHONY: all
all: $(PROGRAMS)

.SUFFIXES: .c .o .h .x

.c.o:
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

.F.o:
	$(F77) $(FFLAGS) $(CPPFLAGS) -c -o $@ $<

.o.x:
	$(LINK) $(LOADER_OPTS) $(LDFLAGS) -o $@ $< $(LIBS)

clean:
	$(RM) *.o *.x
