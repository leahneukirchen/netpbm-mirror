###############################################################################
#
#  This directory is not part of a normal build.  The file netpbm.ico is
#  not platform-specific, so we distribute it in this directory already
#  built.  This make file is only for use by developers in generating a
#  different icon.
#
#  If you want to change netpbm.ico, run the make in this directory.  And run
#  'make clean' in this directory too.
#
###############################################################################

ifeq ($(SRCDIR)x,x)
  SRCDIR = $(CURDIR)/..
  BUILDDIR = $(SRCDIR)
endif
SUBDIR = buildtools
VPATH = .:$(SRCDIR)/$(SUBDIR)
include $(BUILDDIR)/config.mk

all: netpbm.ico

include $(SRCDIR)/common.mk

%.ico:%.ppm
	ppmtowinicon $< >$@

%.o:%.ico
	echo "id ICON \"$<\"" >rc
	$(WINDRES) --input-format rc --input rc --output-format coff \
	   --output $@
	rm rc

distclean clean: cleanlocal
.PHONY: cleanlocal
cleanlocal:
	rm -f rc
