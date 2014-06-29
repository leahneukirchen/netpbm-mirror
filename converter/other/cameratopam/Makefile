ifeq ($(SRCDIR)x,x)
  SRCDIR = $(CURDIR)/../../..
  BUILDDIR = $(SRCDIR)
endif
SUBDIR = converter/other/cameratopam
VPATH=.:$(SRCDIR)/$(SUBDIR)

EXTERN_INCLUDES =
ifneq ($(JPEGLIB),NONE)
  ifneq ($(JPEGHDR_DIR)x,x)
    EXTERN_INCLUDES += -I$(JPEGHDR_DIR)
    HAVE_JPEG_DEFINE = -DHAVE_JPEG
  endif
endif

include $(BUILDDIR)/config.mk


.PHONY: all
all: cameratopam

OBJECTS = util.o identify.o cameratopam.o camera.o foveon.o decode.o \
	canon.o ljpeg.o dng.o

camera.o camera.o2: CFLAGS_TARGET = $(HAVE_JPEG_DEFINE)

MERGE_OBJECTS =

BINARIES = cameratopam
MERGEBINARIES = 
SCRIPTS = 

include $(SRCDIR)/common.mk

cameratopam: $(OBJECTS) $(NETPBMLIB) $(LIBOPT)
	$(LD) -o $@ \
          $(OBJECTS) $(shell $(LIBOPT) $(NETPBMLIB) $(LIBOPTR)) \
	  $(MATHLIB) $(LDFLAGS) $(LDLIBS) \
	  $(RPATH) $(LADD)

