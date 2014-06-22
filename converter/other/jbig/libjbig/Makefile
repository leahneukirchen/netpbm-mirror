ifeq ($(SRCDIR)x,x)
  SRCDIR = $(CURDIR)/../../../..
  BUILDDIR = $(SRCDIR)
endif
SUBDIR = converter/other/jbig/libjbig
VPATH=.:$(SRCDIR)/$(SUBDIR)

include $(BUILDDIR)/config.mk

LIBJBIG_OBJECTS = jbig.o jbig_ar.o

OBJECTS = $(LIBJBIG_OBJECTS)
MERGE_OBJECTS = $(LIBJBIG_OBJECTS)

COMP_INCLUDES = -I$(SRCDIR)/$(SUBDIR)/include

all: libjbig.a

include $(SRCDIR)/common.mk

libjbig.a: $(LIBJBIG_OBJECTS)
	$(AR) -rc $@ $^
	$(RANLIB) $@

