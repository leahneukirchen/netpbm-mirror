ifeq ($(SRCDIR)x,x)
  SRCDIR = $(CURDIR)/../..
  BUILDDIR = $(SRCDIR)
endif
SUBDIR = lib/util
VPATH=.:$(SRCDIR)/$(SUBDIR)

include $(BUILDDIR)/config.mk

# nstring is required for asprintf(), etc.  Also some systems don't have
# snprintf(), e.g. Solaris 2.5.1.  2002.03.29.
UTILOBJECTS = \
  filename.o \
  io.o \
  mallocvar.o \
  matrix.o \
  nsleep.o \
  nstring.o \
  shhopt.o \
  token.o \
  vasprintf.o \

MERGE_OBJECTS =

all: $(UTILOBJECTS)

include $(SRCDIR)/common.mk

$(UTILOBJECTS):%.o:%.c importinc
	$(CC) -c $(INCLUDES) -DNDEBUG $(CPPFLAGS) $(CFLAGS) $(CFLAGS_SHLIB) \
	  $(CFLAGS_PERSONAL) $(CADD) -o $@ $<

testnstring: test.c nstring.h nstring.o
	$(CC) $(CFLAGS) $(CADD) -o $@ nstring.o $<
