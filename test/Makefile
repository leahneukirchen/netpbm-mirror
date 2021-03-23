ifeq ($(SRCDIR)x,x)
  SRCDIR = $(CURDIR)/..
  BUILDDIR = $(SRCDIR)
endif
SUBDIR = test
VPATH = .:$(SRCDIR)/$(SUBDIR)
include $(BUILDDIR)/config.mk

MERGE_OBJECTS =

PROGS =

OMIT_TEST_RULE = 1
include $(SRCDIR)/common.mk

all:

distclean clean: cleanlocal
.PHONY: cleanlocal
cleanlocal:

