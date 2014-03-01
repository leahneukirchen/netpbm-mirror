ifeq ($(SRCDIR)x,x)
  SRCDIR = $(CURDIR)/../..
  BUILDDIR = $(SRCDIR)
endif
SUBDIR = editor/specialty
VPATH=.:$(SRCDIR)/$(SUBDIR)

include $(BUILDDIR)/config.mk

PORTBINARIES = pamdeinterlace \
	       pammixinterlace \
	       pamoil \
	       pampop9 \
	       pampaintspill \
	       pbmlife \
	       pgmabel \
	       pgmbentley \
	       pgmmorphconv \
	       pnmindex \
	       pnmmercator \
	       ppm3d \
	       ppmglobe \
	       ppmntsc \
	       ppmrelief \
	       ppmshift \
	       ppmspread \
	       ppmtv \

# We don't include programs that have special library dependencies in the
# merge scheme, because we don't want those dependencies to prevent us
# from building all the other programs.

NOMERGEBINARIES = 
MERGEBINARIES = $(PORTBINARIES)


BINARIES = $(MERGEBINARIES) $(NOMERGEBINARIES)
SCRIPTS =

OBJECTS = $(BINARIES:%=%.o)

MERGE_OBJECTS = $(MERGEBINARIES:%=%.o2)

.PHONY: all
all: $(BINARIES)

include $(SRCDIR)/common.mk

install.bin: install.bin.local

.PHONY: install.bin.local
install.bin.local: $(PKGDIR)/bin
# Remember that $(SYMLINK) might just be a copy command.
# pamoil replaced pgmoil in June 2001.
	cd $(PKGDIR)/bin ; \
	rm -f pgmoil$(EXE) ; \
	$(SYMLINK) pamoil$(EXE) pgmoil$(EXE)
