ifeq ($(SRCDIR)x,x)
  SRCDIR = $(CURDIR)/../..
  BUILDDIR = $(SRCDIR)
endif
SUBDIR = other/pamx
VPATH=.:$(SRCDIR)/$(SUBDIR)

include $(BUILDDIR)/Makefile.config

ifneq ($(X11LIB),NONE)
  ifneq ($(X11HDR_DIR),)
    INCLUDES += -I$(X11HDR_DIR)
  endif
endif

ifneq ($(X11LIB),NONE)
  BINARIES += pamx
endif

PAMX_OBJECTS = \
	pamx.o \
	image.o \
	send.o \
	window.o \

MERGE_OBJECTS = \
	pamx.o2 \
	image.o \
	send.o \
	window.o \

OBJECTS = $(PAMX_OBJECTS)

MERGEBINARIES = $(BINARIES)

all: $(BINARIES)

include $(SRCDIR)/Makefile.common

pamx: $(PAMX_OBJECTS) $(NETPBMLIB) $(LIBOPT)
	$(LD) $(LDFLAGS) -o $@ $(PAMX_OBJECTS) \
	  $(shell $(LIBOPT) $(NETPBMLIB) $(X11LIB)) \
	  $(LDLIBS) $(MATHLIB) $(RPATH) $(LADD)
