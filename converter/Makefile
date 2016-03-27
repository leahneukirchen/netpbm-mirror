ifeq ($(SRCDIR)x,x)
  SRCDIR = $(CURDIR)/..
  BUILDDIR = $(SRCDIR)
endif
SUBDIR = converter
VPATH=.:$(SRCDIR)/$(SUBDIR)

include $(BUILDDIR)/config.mk

SUBDIRS = pbm pgm ppm other

all: $(SUBDIRS:%=%/all)

SCRIPTS = 
BINARIES =

include $(SRCDIR)/common.mk
