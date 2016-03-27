ifeq ($(SRCDIR)x,x)
  SRCDIR = $(CURDIR)/../../..
  BUILDDIR = $(SRCDIR)
endif
SUBDIR = converter/other/pamtosvg
VPATH=.:$(SRCDIR)/$(SUBDIR)

include $(BUILDDIR)/config.mk

PORTBINARIES = pamtosvg

BINARIES = $(PORTBINARIES)

MERGEBINARIES = $(BINARIES)

ADDL_OBJECTS = \
	output-svg.o \
	fit.o \
	spline.o \
	curve.o \
	vector.o \
	epsilon-equal.o \
	autotrace.o \
	pxl-outline.o \
	bitmap.o \
	thin-image.o \
	logreport.o \
	exception.o \
	image-proc.o \

OBJECTS = pamtosvg.o $(ADDL_OBJECTS)

MERGE_OBJECTS = pamtosvg.o2 $(ADDL_OBJECTS)

all: $(BINARIES)

include $(SRCDIR)/common.mk

pamtosvg: $(ADDL_OBJECTS)
