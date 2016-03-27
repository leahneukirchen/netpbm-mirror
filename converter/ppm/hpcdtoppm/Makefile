ifeq ($(SRCDIR)x,x)
  SRCDIR = $(CURDIR)/../../..
  BUILDDIR = $(SRCDIR)
endif
SUBDIR = converter/ppm/hpcdtoppm
VPATH=.:$(SRCDIR)/$(SUBDIR)

include $(BUILDDIR)/config.mk

all: hpcdtoppm

SCRIPTS = hpcdtoppm pcdovtoppm
MERGE_OBJECTS =

include $(SRCDIR)/common.mk

install: install.bin.local
.PHONY: install.bin.local
install.bin.local: $(PKGDIR)/bin
# In June 2002, pcdovtoppm replaced pcdindex
	cd $(PKGDIR)/bin ; \
	$(SYMLINK) pcdindex$(EXE) pcdovtoppm$(EXE)


FORCE:
