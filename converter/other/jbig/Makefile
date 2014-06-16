ifeq ($(SRCDIR)x,x)
  SRCDIR = $(CURDIR)/../../..
  BUILDDIR = $(SRCDIR)
endif
SUBDIR = converter/other/jbig
VPATH=.:$(SRCDIR)/$(SUBDIR)

include $(BUILDDIR)/config.mk

LIBJBIG_OBJECTS = jbig.o jbig_tab.o

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

SCRIPTS =

ifeq ($(JBIGLIB),$(BUILDDIR)/$(SUBDIR)/libjbig.a)
  JBIGLIB_DEP = $(JBIGLIB)
else
  # It's not our internal version; user's on his own to make sure it's built
endif

OBJECTS = $(BINARIES:%=%.o) $(LIBJBIG_OBJECTS)
MERGE_OBJECTS = $(BINARIES:%=%.o2) $(LIBJBIG_OBJECTS)

all: $(BINARIES)

include $(SRCDIR)/common.mk

$(BINARIES): %: %.o $(JBIGLIB_DEP) $(LIBOPT)
$(BINARIES): LDFLAGS_TARGET = $(shell $(LIBOPT) $(JBIGLIB))

$(BUILDDIR)/$(SUBDIR)/libjbig.a: $(LIBJBIG_OBJECTS)
	$(AR) -rc $@ $^
	$(RANLIB) $@

