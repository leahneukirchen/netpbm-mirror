ifeq ($(SRCDIR)x,x)
  SRCDIR = $(CURDIR)/..
  BUILDDIR = $(SRCDIR)
endif
SUBDIR = generator
VPATH=.:$(SRCDIR)/$(SUBDIR)

include $(BUILDDIR)/config.mk

# We tend to separate out the build targets so that we don't have
# any more dependencies for a given target than it really needs.
# That way, if there is a problem with a dependency, we can still
# successfully build all the stuff that doesn't depend upon it.
# This package is so big, it's useful even when some parts won't 
# build.

PORTBINARIES = pamcrater pamgauss pamgradient \
	       pamseq pamshadedrelief pamstereogram \
	       pbmpage pbmmake pbmtext pbmupc \
	       pgmkernel pgmmake pgmnoise pgmramp \
	       ppmcie ppmcolors ppmforge ppmmake ppmpat ppmrough ppmwheel \

ifneq ($(DONT_HAVE_PROCESS_MGMT),Y)
PORTBINARIES += pbmtextps 
endif
# We don't include programs that have special library dependencies in the
# merge scheme, because we don't want those dependencies to prevent us
# from building all the other programs.

NOMERGEBINARIES = 
MERGEBINARIES = $(PORTBINARIES)


BINARIES = $(MERGEBINARIES) $(NOMERGEBINARIES)
SCRIPTS = pgmcrater ppmrainbow

OBJECTS = $(BINARIES:%=%.o)

MERGE_OBJECTS = $(MERGEBINARIES:%=%.o2)

.PHONY: all
all: $(BINARIES)

include $(SRCDIR)/common.mk
