ifeq ($(SRCDIR)x,x)
  SRCDIR = $(CURDIR)/../../..
  BUILDDIR = $(SRCDIR)
endif
SUBDIR = converter/other/jpeg2000
VPATH=.:$(SRCDIR)/$(SUBDIR)

SUBDIRS =

include $(BUILDDIR)/config.mk

EXTERN_INCLUDES =

# INTERNAL_JASPERLIB must be relative to the current directory, because it
# may end up in MERGE_OBJECTS, which must be relative.
INTERNAL_JASPERLIB = libjasper/libjasper.a
INTERNAL_JASPERHDR_DIR = $(SRCDIR)/$(SUBDIR)/libjasper/include

ifneq ($(JASPERHDR_DIR),NONE)
  ifneq ($(JASPERHDR_DIR)x,x)
    EXTERN_INCLUDES += -I$(JASPERHDR_DIR)
  endif
endif

ifeq ($(JASPERLIB),$(INTERNAL_JASPERLIB))
  ifeq ($(HAVE_INT64),Y)
    JASPERLIB_DEP = $(JASPERLIB)
    JASPERLIB_USE = $(JASPERLIB)
  else
    # He wants the internal library, but doesn't have a 64 bit compiler,
    # so we have no way to build it.  Ergo, he doesn't have a Jasper
    # library.
    JASPERLIB_USE = NONE
  endif
else
  # It's not our internal version; user's on his own to make sure it's built
  JASPERLIB_USE = $(JASPERLIB)
endif


ifneq ($(JASPERHDR_DIR),NONE)
  ifneq ($(JASPERLIB_USE),NONE)
    PORTBINARIES = pamtojpeg2k jpeg2ktopam
  endif
endif

BINARIES = $(PORTBINARIES)

OBJECTS = $(BINARIES:%=%.o)
MERGE_OBJECTS = $(BINARIES:%=%.o2) 
ifeq ($(JASPERLIB),$(INTERNAL_JASPERLIB))
  # MERGE_OBJECTS contains relative paths, so $(INTERNAL_JASPERLIB) had better
  # be relative to the current directory.
  MERGE_OBJECTS += $(JASPERLIB)
  SUBDIRS += libjasper
endif
MERGEBINARIES = $(BINARIES)

.PHONY: all
all: $(BINARIES)

include $(SRCDIR)/common.mk

LIBOPTS = $(shell $(LIBOPT) $(JASPERLIB_USE))

$(BINARIES): %: %.o $(JASPERLIB_DEP) $(LIBOPT)
$(BINARIES): LDFLAGS_TARGET = $(LIBOPTS) $(JASPERDEPLIBS)

$(INTERNAL_JASPERLIB): $(BUILDDIR)/$(SUBDIR)/libjasper FORCE
	$(MAKE) -f $(SRCDIR)/$(SUBDIR)/libjasper/Makefile \
	   -C $(dir $@) $(notdir $@)

.PHONY: FORCE
FORCE:
