ifeq ($(SRCDIR)x,x)
  SRCDIR = $(CURDIR)/../..
  BUILDDIR = $(SRCDIR)
endif
SUBDIR = generator/pamtris
VPATH=.:$(SRCDIR)/$(SUBDIR)

include $(BUILDDIR)/config.mk

PORTBINARIES = pamtris

MERGEBINARIES = $(PORTBINARIES)

BINARIES = $(MERGEBINARIES) $(NOMERGEBINARIES)

ADDL_OBJECTS = boundaries.o framebuffer.o input.o triangle.o utils.o

OBJECTS = pamtris.o $(ADDL_OBJECTS)

MERGE_OBJECTS = pamtris.o2 $(ADDL_OBJECTS)

.PHONY: all
all: $(BINARIES)

pamtris:%:%.o $(ADDL_OBJECTS)

include $(SRCDIR)/common.mk
