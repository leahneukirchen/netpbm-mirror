ifeq ($(SRCDIR)x,x)
  SRCDIR = $(CURDIR)/../..
  BUILDDIR = $(SRCDIR)
endif
SUBDIR = editor/pamflip
VPATH=.:$(SRCDIR)/$(SUBDIR)

default: all

include $(BUILDDIR)/config.mk

SUBDIRS =

MERGEBINARIES = pamflip

BINARIES = pamflip

SCRIPTS =

PAMFLIP_OBJECTS = pamflip.o pamflip_sse.o

OBJECTS = $(PAMFLIP_OBJECTS)

MERGE_OBJECTS = $(OBJECTS:%.o=%.o2)

include $(SRCDIR)/common.mk

.PHONY: all
all: $(BINARIES) $(SUBDIRS:%=%/all)

pamflip_sse.o pamflip_sse.o2: CFLAGS_TARGET = $(CFLAGS_SSE)

pamflip: $(PAMFLIP_OBJECTS) $(NETPBMLIB) $(LIBOPT)
	$(LD) -o $@ $(PAMFLIP_OBJECTS) \
	  $(shell $(LIBOPT) $(NETPBMLIB)) \
	  $(MATHLIB) $(LDFLAGS) $(LDLIBS) $(RPATH) $(LADD)
