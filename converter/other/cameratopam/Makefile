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

ADDL_OBJECTS = util.o identify.o camera.o foveon.o decode.o \
	canon.o ljpeg.o dng.o

OBJECTS = cameratopam.o $(ADDL_OBJECTS)

camera.o camera.o2: CFLAGS_TARGET = $(HAVE_JPEG_DEFINE)

MERGE_OBJECTS = cameratopam.o2 $(ADDL_OBJECTS)

PORTBINARIES = cameratopam
BINARIES = $(PORTBINARIES)
MERGEBINARIES = cameratopam
SCRIPTS = 

include $(SRCDIR)/common.mk

cameratopam: $(ADDL_OBJECTS)
