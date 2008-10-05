ifeq ($(SRCDIR)x,x)
  SRCDIR = $(CURDIR)/../../../..
  BUILDDIR = $(SRCDIR)
endif
FIASCOSUBDIR = converter/other/fiasco
SUBDIR = $(FIASCOSUBDIR)/codec
VPATH=.:$(SRCDIR)/$(SUBDIR)

include $(BUILDDIR)/config.mk

COMP_INCLUDES = -I$(SRCDIR)/$(FIASCOSUBDIR) -I$(SRCDIR)/$(FIASCOSUBDIR)/lib \
	 -I$(SRCDIR)/$(FIASCOSUBDIR)/input -I$(SRCDIR)/$(FIASCOSUBDIR)/output 

OBJECTS =  approx.o bintree.o coder.o coeff.o \
           control.o decoder.o dfiasco.o domain-pool.o ip.o motion.o mwfa.o \
           options.o prediction.o subdivide.o tiling.o wfalib.o

MERGE_OBJECTS = $(OBJECTS)

all: libfiasco_codec.a

include $(SRCDIR)/common.mk

libfiasco_codec.a: $(OBJECTS)
	$(AR) -rc $@ $(OBJECTS)
	$(RANLIB) $@

