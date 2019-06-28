ifeq ($(SRCDIR)x,x)
  SRCDIR = $(CURDIR)/../../..
  BUILDDIR = $(SRCDIR)
endif
SUBDIR = converter/pbm/pbmtoppa
VPATH=.:$(SRCDIR)/$(SUBDIR)

include $(BUILDDIR)/config.mk

all: pbmtoppa

PORTBINARIES = pbmtoppa

BINARIES = $(PORTBINARIES)

MERGEBINARIES = $(BINARIES)

ADDL_OBJECTS = ppa.o pbm.o cutswath.o

OBJECTS = pbmtoppa.o $(ADDL_OBJECTS)

MERGE_OBJECTS = pbmtoppa.o2 $(ADDL_OBJECTS)

include $(SRCDIR)/common.mk

pbmtoppa: $(OBJECTS)
