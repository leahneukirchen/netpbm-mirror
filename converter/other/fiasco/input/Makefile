ifeq ($(SRCDIR)x,x)
  SRCDIR = $(CURDIR)/../../../..
  BUILDDIR = $(SRCDIR)
endif
FIASCOSUBDIR = converter/other/fiasco
SUBDIR = $(FIASCOSUBDIR)/input
BUILDDIR = ../../../..
VPATH=.:$(SRCDIR)/$(SUBDIR)

include $(BUILDDIR)/config.mk

OBJECTS =  basis.o matrices.o mc.o nd.o read.o tree.o weights.o

MERGE_OBJECTS = $(OBJECTS)

COMP_INCLUDES = -I$(SRCDIR)/$(FIASCOSUBDIR) -I$(SRCDIR)/$(FIASCOSUBDIR)/lib \
	        -I$(SRCDIR)/$(FIASCOSUBDIR)/codec

all: libfiasco_input.a

include $(SRCDIR)/common.mk

libfiasco_input.a: $(OBJECTS)
	$(AR) -rc $@ $(OBJECTS)
	$(RANLIB) $@

