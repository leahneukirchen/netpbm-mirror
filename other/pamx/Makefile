ifeq ($(SRCDIR)x,x)
  SRCDIR = $(CURDIR)/../..
  BUILDDIR = $(SRCDIR)
endif
SUBDIR = other/pamx
VPATH=.:$(SRCDIR)/$(SUBDIR)

include $(BUILDDIR)/config.mk

EXTERN_INCLUDE =

ifeq ($(shell pkg-config x11 --modversion --silence-errors),)
  # Pkg-config has never heard of X11, or doesn't even exist

  ifneq ($(X11LIB),NONE)
    HAVE_X11LIB = Y
    ifneq ($(X11HDR_DIR)x,x)
      EXTERN_INCLUDES += -I$(X11HDR_DIR)
    endif
  endif
else
  HAVE_X11LIB = Y
  X11LIB = $(shell pkg-config x11 --libs)
  EXTERN_INCLUDES += $(shell pkg-config x11 --cflags)
endif

ifeq ($(HAVE_X11LIB),Y)
  BINARIES += pamx

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

endif

OBJECTS = $(PAMX_OBJECTS)

MERGEBINARIES = $(BINARIES)

all: $(BINARIES)

include $(SRCDIR)/common.mk

ifeq ($(shell pkg-config x11 --libs),)
  X11_LIBOPTS = $(shell $(LIBOPT) $(LIBOPTR) $(X11LIB))
else
  X11_LIBOPTS = $(shell pkg-config x11 --libs)
endif

pamx: $(PAMX_OBJECTS) $(NETPBMLIB) $(LIBOPT)
	$(LD) -o $@ $(PAMX_OBJECTS) \
	  $(shell $(LIBOPT) $(NETPBMLIB) $(X11_LIBOPTS)) \
	  $(LDFLAGS) $(LDLIBS) $(MATHLIB) $(RPATH) $(LADD)
