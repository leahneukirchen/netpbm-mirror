ifeq ($(SRCDIR)x,x)
  SRCDIR = $(CURDIR)/..
  BUILDDIR = $(SRCDIR)
endif
SUBDIR = urt
VPATH=.:$(SRCDIR)/$(SUBDIR)

default: all

include $(BUILDDIR)/config.mk

LIBOBJECTS = Runput.o cmd_name.o \
	rle_addhist.o rle_error.o rle_getcom.o rle_getrow.o rle_getskip.o \
	rle_global.o rle_hdr.o rle_open_f.o rle_putcom.o rle_putrow.o \
        rle_row_alc.o \
        scanargs.o vaxshort.o     

MERGE_OBJECTS =

OMIT_URT_RULE = 1
include $(SRCDIR)/common.mk

all: librle.a

librle.a: $(LIBOBJECTS)
	rm -f $@
	$(AR) rc $@ $^
	$(RANLIB) $@

# Rule for objects.
$(LIBOBJECTS): %.o: %.c importinc
	$(CC) -c $(INCLUDES) -o $@ $< $(CFLAGS_ALL)

BINARIES =
SCRIPTS =

