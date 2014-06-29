ifeq ($(SRCDIR)x,x)
  SRCDIR = $(CURDIR)/../../..
  BUILDDIR = $(SRCDIR)
endif
SUBDIR = converter/other/pnmtopalm
VPATH=.:$(SRCDIR)/$(SUBDIR)

include $(BUILDDIR)/config.mk

BINARIES = palmtopnm pnmtopalm
SCRIPTS =
OBJECTS = $(BINARIES:%=%.o) palmcolormap.o
MERGE_OBJECTS = $(BINARIES:%=%.o2) palmcolormap.o
MERGEBINARIES = $(BINARIES)
DATAFILES = palmcolor8.map palmgray1.map palmgray2.map palmgray4.map

all: $(BINARIES)

include $(SRCDIR)/common.mk

LIBOPTS = $(shell $(LIBOPT) $(NETPBMLIB))

$(BINARIES): %: %.o palmcolormap.o $(NETPBMLIB) $(LIBOPT)
	$(LD) -o $@ $< palmcolormap.o $(LIBOPTS) \
	  $(MATHLIB) $(LDFLAGS) $(LDLIBS) $(RPATH) $(LADD)

gen_palm_colormap : % : %.c palmcolormap.o
	$(CC) -I importinc $(CFLAGS_ALL) -o $@ \
	  $< palmcolormap.o \
	  $(LIBOPTS) $(MATHLIB) $(LDFLAGS) $(LDLIBS) $(LADD)


clean: cleanspecial
.PHONY: cleanspecial
cleanspecial:
	rm -f gen_palm_colormap
