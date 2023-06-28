ifeq ($(SRCDIR)x,x)
  SRCDIR = $(CURDIR)/../../..
  BUILDDIR = $(SRCDIR)
endif
SUBDIR = converter/ppm/hpcdtoppm
VPATH=.:$(SRCDIR)/$(SUBDIR)

include $(BUILDDIR)/config.mk

SCRIPTS =  pcdovtoppm

ifeq ($(file <hpcdtoppm-import/Makefile)x,x)
  # The file does not exist, which means user did not augment the
  # Netpbm source tree by adding hpcdtoppm source code.
  #
  # Therefore, we package the dummy 'hpcdtoppm' program that just tells the
  # user how to get the real one.
  #
  # See README in this directory.
  #
  # (Note that empty file and nonexistent file look the same with
  # $(file)).
  SCRIPTS += hpcdtoppm
else
  SUBDIRS += hpcdtoppm-import
endif

MERGE_OBJECTS =

.PHONY: all
all: $(BINARIES) $(SUBDIRS:%=%/all)

include $(SRCDIR)/common.mk

install.bin install.merge: install.bin.local
.PHONY: install.bin.local
install.bin.local: $(PKGDIR)/bin
# In June 2002, pcdovtoppm replaced pcdindex
	cd $(PKGDIR)/bin ; \
	$(SYMLINK) pcdovtoppm pcdindex


FORCE:
