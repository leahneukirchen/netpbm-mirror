ifeq ($(SRCDIR)x,x)
  SRCDIR = $(CURDIR)/..
  BUILDDIR = $(SRCDIR)
endif
SUBDIR = other
VPATH=.:$(SRCDIR)/$(SUBDIR)

include $(BUILDDIR)/config.mk

SUBDIRS = pamx

EXTERN_INCLUDES =
ifneq ($(LINUXSVGALIB),NONE)
  ifneq ($(LINUXSVGAHDR_DIR),)
    EXTERN_INCLUDES += -I$(LINUXSVGAHDR_DIR)
  endif
endif

# We tend to separate out the build targets so that we don't have
# any more dependencies for a given target than it really needs.
# That way, if there is a problem with a dependency, we can still
# successfully build all the stuff that doesn't depend upon it.
# This package is so big, it's useful even when some parts won't 
# build.

PORTBINARIES = pamarith pambayer pamchannel pamdepth \
	pamendian pamexec pamfix pamlookup pampick pamsplit \
	pamstack pamsummcol pamunlookup pamvalidate pnmcolormap \
	ppmdcfont ppmddumpfont ppmdmkfont 

ifneq ($(LINUXSVGALIB),NONE)
  PORTBINARIES += ppmsvgalib
endif

BINARIES = $(PORTBINARIES)

SCRIPTS = ppmtomap pamfixtrunc

OBJECTS = $(BINARIES:%=%.o)

# We don't include programs that have special library dependencies in the
# merge scheme, because we don't want those dependencies to prevent us
# from building all the other programs.

MERGEBINARIES = $(BINARIES)
MERGE_OBJECTS = $(MERGEBINARIES:%=%.o2)

.PHONY: all
all: $(BINARIES) $(SUBDIRS:%=%/all)

include $(SRCDIR)/common.mk

ppmsvgalib: LDFLAGS_TARGET = $(shell $(LIBOPT) $(LINUXSVGALIB))

install.bin: install.bin.local
.PHONY: install.bin.local
install.bin.local: $(PKGDIR)/bin
# Remember that $(SYMLINK) might just be a copy command.
# In July 2002, pamarith replaced pnmarith
	cd $(PKGDIR)/bin ; \
	  rm -f pnmarith$(EXE) ; \
	  $(SYMLINK) pamarith$(EXE) pnmarith$(EXE)
# In December 2005, pamsplit replaced pnmsplit
	cd $(PKGDIR)/bin ; \
	  rm -f pnmsplit$(EXE) ; \
	  $(SYMLINK) pamsplit$(EXE) pnmsplit$(EXE)
# In February 2006, pamdepth replaced pnmdepth
	cd $(PKGDIR)/bin ; \
	  rm -f pnmdepth$(EXE) ; \
	  $(SYMLINK) pamdepth$(EXE) pnmdepth$(EXE)

FORCE:
