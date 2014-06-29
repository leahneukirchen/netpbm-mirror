ifeq ($(SRCDIR)x,x)
  SRCDIR = $(CURDIR)/../../..
  BUILDDIR = $(SRCDIR)
endif
SUBDIR = converter/other/pnmtopalm
VPATH=.:$(SRCDIR)/$(SUBDIR)

include $(BUILDDIR)/config.mk

BINARIES = palmtopnm pnmtopalm
PORTBINARIES = $(BINARIES) gen_palm_colormap
SCRIPTS =
ADDL_OBJECTS = palmcolormap.o
OBJECTS = $(BINARIES:%=%.o) $(ADDL_OBJECTS) gen_palm_colormap.o
MERGE_OBJECTS = $(BINARIES:%=%.o2) palmcolormap.o
MERGEBINARIES = $(BINARIES)
DATAFILES = palmcolor8.map palmgray1.map palmgray2.map palmgray4.map

all: $(BINARIES)

include $(SRCDIR)/common.mk

$(BINARIES): $(ADDL_OBJECTS)

clean: cleanspecial
.PHONY: cleanspecial
cleanspecial:
	rm -f gen_palm_colormap
