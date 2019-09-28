ifeq ($(SRCDIR)x,x)
  SRCDIR = $(CURDIR)/../..
  BUILDDIR = $(SRCDIR)
endif
SUBDIR = other/pamx
VPATH=.:$(SRCDIR)/$(SUBDIR)

include $(BUILDDIR)/config.mk

EXTERN_INCLUDE =

TEST_PKGCONFIG_X11 := \
  if $(PKG_CONFIG) x11 --exists; then echo exists; fi

ifeq ($(shell $(TEST_PKGCONFIG_X11)),)
  # Pkg-config has never heard of X11, or doesn't even exist

  ifneq ($(X11LIB),NONE)
    HAVE_X11LIB = Y
    ifneq ($(X11HDR_DIR)x,x)
      EXTERN_INCLUDES += -I$(X11HDR_DIR)
    endif
  endif
else
  HAVE_X11LIB = Y
  X11LIB = $(shell $(PKG_CONFIG) x11 --libs)
  EXTERN_INCLUDES += $(shell $(PKG_CONFIG) x11 --cflags)
endif

ifeq ($(HAVE_X11LIB),Y)
  PORTBINARIES += pamx

  EXTRA_OBJECTS = \
	  image.o \
	  send.o \
	  window.o \

endif

BINARIES = $(PORTBINARIES)

OBJECTS = $(BINARIES:%=%.o) $(EXTRA_OBJECTS)

MERGE_OBJECTS = $(BINARIES:%=%.o2) $(EXTRA_OBJECTS)

MERGEBINARIES = $(BINARIES)

all: $(BINARIES)

include $(SRCDIR)/common.mk

ifeq ($(shell $(TEST_PKGCONFIG_X11)),)
  # Pkg-config has never heard of X11, or doesn't even exist
  X11_LIBOPTS = $(shell $(LIBOPT) $(LIBOPTR) $(X11LIB))
else
  X11_LIBOPTS = $(shell $(PKG_CONFIG) x11 --libs)
endif

pamx: image.o send.o window.o
pamx: ADDL_OBJECTS = image.o send.o window.o
pamx: LDFLAGS_TARGET = $(X11_LIBOPTS)
