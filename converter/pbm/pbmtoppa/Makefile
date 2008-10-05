ifeq ($(SRCDIR)x,x)
  SRCDIR = $(CURDIR)/../../..
  BUILDDIR = $(SRCDIR)
endif
SUBDIR = converter/pbm/pbmtoppa
VPATH=.:$(SRCDIR)/$(SUBDIR)

include $(BUILDDIR)/config.mk

all: pbmtoppa

BINARIES = pbmtoppa

MERGEBINARIES = $(BINARIES)

OBJECTS = pbmtoppa.o ppa.o pbm.o cutswath.o
MERGE_OBJECTS = pbmtoppa.o2 ppa.o pbm.o cutswath.o

include $(SRCDIR)/common.mk

pbmtoppa: $(OBJECTS) $(NETPBMLIB) $(LIBOPT)
	$(LD) $(LDFLAGS) -o pbmtoppa $(OBJECTS) \
	  -lm $(shell $(LIBOPT) $(NETPBMLIB)) $(LDLIBS) \
	  $(RPATH) $(LADD)

