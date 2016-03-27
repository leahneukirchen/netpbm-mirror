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

PORTBINARIES = pamflip

BINARIES = $(PORTBINARIES)

SCRIPTS =

ADDL_OBJECTS = pamflip_sse.o

OBJECTS = pamflip.o $(ADDL_OBJECTS)

MERGE_OBJECTS = $(OBJECTS:%.o=%.o2)

include $(SRCDIR)/common.mk

.PHONY: all
all: $(BINARIES) $(SUBDIRS:%=%/all)

pamflip_sse.o pamflip_sse.o2: CFLAGS_TARGET = $(CFLAGS_SSE)
pamflip.o pamflip.o2: CFLAGS_TARGET = $(CFLAGS_SSE)

pamflip:%:%.o $(ADDL_OBJECTS)
