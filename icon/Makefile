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
