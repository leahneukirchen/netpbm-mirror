ifeq ($(SRCDIR)x,x)
  SRCDIR = $(CURDIR)/..
  BUILDDIR = $(SRCDIR)
endif
SUBDIR = test
VPATH = .:$(SRCDIR)/$(SUBDIR)
include $(BUILDDIR)/config.mk

MERGE_OBJECTS =

PROGS = testrandom

all: $(PROGS)

PORTBINARIES = testrandom
OBJECTS = $(PORTBINARIES:%=%.o)

OMIT_TEST_RULE = 1
include $(SRCDIR)/common.mk

distclean clean: cleanlocal
.PHONY: cleanlocal
cleanlocal:
	rm -f $(PROGS)
