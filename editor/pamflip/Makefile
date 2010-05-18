ifeq ($(SRCDIR)x,x)
  SRCDIR = $(CURDIR)/../..
  BUILDDIR = $(SRCDIR)
endif
SUBDIR = editor/pamflip
VPATH=.:$(SRCDIR)/$(SUBDIR)

include $(BUILDDIR)/config.mk

SUBDIRS =

MERGEBINARIES = pamflip

BINARIES = pamflip

SCRIPTS =

PAMFLIP_OBJECTS = pamflip.o pamflip_sse.o

OBJECTS = $(PAMFLIP_OBJECTS)

MERGE_OBJECTS = $(OBJECTS:%.o=%.o2)

.PHONY: all
all: $(BINARIES) $(SUBDIRS:%=%/all)

include $(SRCDIR)/common.mk

pamflip: $(PAMFLIP_OBJECTS) $(NETPBMLIB) $(LIBOPT)
	$(LD) -o $@ $(PAMFLIP_OBJECTS) \
	  $(shell $(LIBOPT) $(NETPBMLIB)) \
	  $(MATHLIB) $(LDFLAGS) $(LDLIBS) $(RPATH) $(LADD)
