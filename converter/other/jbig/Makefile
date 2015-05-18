ifeq ($(SRCDIR)x,x)
  SRCDIR = $(CURDIR)/../../..
  BUILDDIR = $(SRCDIR)
endif
SUBDIR = converter/other/jbig
VPATH=.:$(SRCDIR)/$(SUBDIR)

SUBDIRS =

include $(BUILDDIR)/config.mk

# INTERNAL_JBIGLIB must be relative to the current directory, because it
# may end up in MERGE_OBJECTS, which must be relative.
INTERNAL_JBIGLIB = libjbig/libjbig.a
INTERNAL_JBIGHDR_DIR = $(SRCDIR)/$(SUBDIR)/libjbig/include

EXTERN_INCLUDES =
ifneq ($(JBIGHDR_DIR),NONE)
  ifneq ($(JBIGHDR_DIR)x,x)
    EXTERN_INCLUDES += -I$(JBIGHDR_DIR)
  endif
endif

ifneq ($(JBIGHDR_DIR),NONE)
  ifneq ($(JBIGLIB),NONE)
    PORTBINARIES = jbigtopnm pnmtojbig
  endif
endif

BINARIES = $(PORTBINARIES)

MERGEBINARIES = $(BINARIES)

SCRIPTS =

ifeq ($(JBIGLIB),$(INTERNAL_JBIGLIB))
  JBIGLIB_DEP = $(JBIGLIB)
  SUBDIRS += libjbig
else
  # It's not our internal version; user's on his own to make sure it's built
endif

OBJECTS = $(BINARIES:%=%.o)
MERGE_OBJECTS = $(BINARIES:%=%.o2)

all: $(BINARIES)

include $(SRCDIR)/common.mk

$(BINARIES): %: %.o $(JBIGLIB_DEP) $(LIBOPT)
$(BINARIES): LDFLAGS_TARGET = $(shell $(LIBOPT) $(JBIGLIB))

$(INTERNAL_JBIGLIB): $(BUILDDIR)/$(SUBDIR)/libjbig FORCE
	$(MAKE) -f $(SRCDIR)/$(SUBDIR)/libjbig/Makefile \
	  -C $(dir $@) $(notdir $@)

.PHONY: FORCE
FORCE:
