ifeq ($(SRCDIR)x,x)
  SRCDIR = $(CURDIR)/..
  BUILDDIR = $(SRCDIR)
endif
SUBDIR = buildtools
VPATH = .:$(SRCDIR)/$(SUBDIR)
include $(BUILDDIR)/config.mk

MERGE_OBJECTS =

# These are programs that are used by the make files:
BUILDPROGS = libopt typegen endiangen

# Ideally, this directory would not contain anything but build tools, which
# means they would all be built to run in the build environment, not the
# target environment.  But we have no other convenient place for test tools,
# so we put them here and build them for the target environment.
TESTPROGS = testrandom

PROGS = $(BUILDPROGS) $(TESTPROGS)

all: $(PROGS)

PORTBINARIES = $(TESTPROGS)
BINARIES = $(PORTBINARIES)
OBJECTS = $(BINARIES:%=%.o)
SCRIPTS =

OMIT_BUILDTOOL_RULE = 1
include $(SRCDIR)/common.mk

ifdef DLLVER
STRIP_DLL_VERSION=-DDLLVERSTR="\"$(DLLVER)\""
endif

ifeq ($(LINKER_CAN_DO_EXPLICIT_LIBRARY),Y)
EXPLICIT=-DEXPLICIT
endif

libopt.o: libopt.c
	$(CC_FOR_BUILD) -c -o $@ $(CFLAGS_FOR_BUILD) \
	  -DSHLIBPREFIXLIST="\"$(SHLIBPREFIXLIST)\"" \
	  $(STRIP_DLL_VERSION) $(EXPLICIT) $(CFLAGS_PERSONAL) $(CADD) \
	  $<

typegen.o endiangen.o:%.o:%.c
	$(CC_FOR_BUILD) -c -o $@ $(CFLAGS_FOR_BUILD) $<

$(BUILDPROGS):%:%.o
	$(LD_FOR_BUILD) -o $@ $(LDFLAGS_FOR_BUILD) $<

distclean clean: cleanlocal
.PHONY: cleanlocal
cleanlocal:
	rm -f $(PROGS)
