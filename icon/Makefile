ifeq ($(SRCDIR)x,x)
  SRCDIR = $(CURDIR)/..
  BUILDDIR = $(SRCDIR)
endif
SUBDIR = buildtools
VPATH = .:$(SRCDIR)/$(SUBDIR)
include $(BUILDDIR)/config.mk

.PHONY: default
default: netpbm.o

# We must not attempt to build netpbm.o if the build system does not
# have 'windres', so our all list is empty.
.PHONY: all
all:

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
