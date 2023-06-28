# Makefile for Netpbm

# Configuration should normally be done in the included file config.mk.

# Targets in this file:
#
#   nonmerge:     Build everything, in the source directory.
#   merge:        Build everything as merged executables, in the source dir.
#   package:      Make a package of Netpbm files ready to install.
#
#   deb:          Make a .deb file in the current dir.
#
#   check-tree:     Conduct tests on Netpbm files in the source dir. 
#                   with "target=pamtotga" tests only pamtotga.
#   check-package:  Conduct tests on packaged Netpbm files.
#   check-install:  Conduct tests on installed Netpbm files.
#   check:          Default check.  Synonym for check-package.
#
#   clean:        Delete target executables and intermediate objects.
#   distclean:    Delete configuration files in addition to the above.
#
#   tags:         Generate/update an Emacs tags file, named TAGS.
#   
#   The default target is either "merge" or "nonmerge", as determined by
#   the DEFAULT_TARGET variable set by config.mk.

# About the "merge" target: Normally the Makefiles build separate
# executables for each program.  However, on some systems (especially
# those without shared libraries) this can mean a lot of space.  In
# this case you might try building a "merge" instead.  The idea here
# is to link all the programs together into one huge executable, along
# with a tiny dispatch program that runs one of the programs based on
# the command name with which it was invoked.  You install the merged
# executable with a file system link for the name of each program it
# includes.  This is much more important when you're statically
# linking than when you're using shared libraries.  On a Sun3 under
# SunOS 3.5, where shared libraries are not available, the space for
# executables went from 2970K to 370K in an older Netpbm.  On a
# GNU/Linux IA32 system with shared libraries in 2002, it went from
# 2949K to 1663K.

# To build a "merge" system, just set DEFAULT_TARGET to "merge" instead
# of "nomerge" in config.mk.  In that case, you should probably also
# set NETPBMLIBTYPE to "unixstatic", since a shared library doesn't do you 
# much good.

# The CURDIR variable presents a problem because it was introduced in
# GNU Make 3.77.  We need the CURDIR variable in order for our 'make
# -C xxx -f xxx' commands to work.  If we used the obvious alternative
# ".", that wouldn't work because it would refer to the directory
# named in -C, not the directory the make file you are reading is
# running in.  The -f option is necessary in order to have separate
# source and object directories.

ifeq ($(CURDIR)x,x)
all package install:
	@echo "YOU NEED AT LEAST VERSION 3.77 OF GNU MAKE TO BUILD NETPBM."
	@echo "Netpbm's makefiles need the CURDIR variable that was "
	@echo "introduced in 3.77.  Your version does not have CURDIR."
	@echo
	@echo "You can get a current GNU Make via http://www.gnu.org/software"
	@echo 
	@echo "If upgrading is impossible, try modifying GNUMakefile and "
	@echo "common.mk to replace \$(CURDIR) with \$(shell /bin/pwd) "
else


# srcdir.mk defines SRCDIR .
include srcdir.mk
BUILDDIR = $(CURDIR)
SUBDIR = 
VPATH=.:$(SRCDIR)

include $(BUILDDIR)/config.mk

PROG_SUBDIRS = converter analyzer editor generator other
PRODUCT_SUBDIRS = lib $(PROG_SUBDIRS)
SUPPORT_SUBDIRS = urt icon buildtools test

SUBDIRS = $(PRODUCT_SUBDIRS) $(SUPPORT_SUBDIRS)

SCRIPTS = manweb
MANUALS1 = netpbm
NOMERGEBINARIES = netpbm

OBJECTS = netpbm.o

default: $(DEFAULT_TARGET)
	echo "EXISTENCE OF THIS FILE MEANS NETPBM HAS BEEN BUILT." \
	  >build_complete
	@echo ""
	@echo "Netpbm is built.  The next step is normally to package it "
	@echo "for installation by running "
	@echo ""
	@echo "    make package pkgdir=DIR"
	@echo ""
	@echo "to copy all the Netpbm files you need to install into the "
	@echo "directory DIR.  Then you can proceed to install."

all: nonmerge

.PHONY: nonmerge
nonmerge: $(PRODUCT_SUBDIRS:%=%/all)

# Completely parallel make (make --jobs) does not work because there are
# multiple targets somewhere in the Netpbm build that depend upon pm_config.h
# and similar targets, and the threads building those multiple targets might
# simultaneously find that pm_config.h needs to be built and proceed to build
# it.  After one thread has built pm_config.h, it will proceed to use
# pm_config.h, but the other thread is still building pm_config.h, which is
# not valid while it is in the middle of being used.
#
# But many submakes don't have any such shared dependencies, so build their
# targets in parallel just fine.  So we declare this make file ineligible for
# parallel make and have special dependencies to get pm_config.h and like
# targets built before any submakes begin.  The submakes will thus never find
# that pm_config.h needs to be built, so we leave them eligible for parallel
# make.

.NOTPARALLEL:

$(SUBDIRS:%=%/all) lib/util/all: pm_config.h inttypes_netpbm.h version.h
$(PROG_SUBDIRS:%=%/all): lib/all $(SUPPORT_SUBDIRS:%=%/all)
lib/all: lib/util/all
netpbm: lib/all

.PHONY: lib/util/all
lib/util/all:
	mkdir -p lib/util
	$(MAKE) -C lib/util -f $(SRCDIR)/lib/util/Makefile \
	    SRCDIR=$(SRCDIR) BUILDDIR=$(BUILDDIR) all

OMIT_CONFIG_RULE = 1
OMIT_VERSION_H_RULE = 1
OMIT_INTTYPES_RULE = 1
include $(SRCDIR)/common.mk

$(BUILDDIR)/config.mk: $(SRCDIR)/config.mk.in
	$(SRCDIR)/configure $(SRCDIR)/config.mk.in


# typegen is a utility program used by the make file below.
TYPEGEN = $(BUILDDIR)/buildtools/typegen

# endiangen is a utility program used by the make file below.
ENDIANGEN = $(BUILDDIR)/buildtools/endiangen

$(TYPEGEN) $(ENDIANGEN): $(BUILDDIR)/buildtools
	$(MAKE) -C $(dir $@) -f $(SRCDIR)/buildtools/Makefile \
	    SRCDIR=$(SRCDIR) BUILDDIR=$(BUILDDIR) $(notdir $@) 

inttypes_netpbm.h: $(TYPEGEN)
	$(TYPEGEN) >$@


# testrandom is a utility program used by the make file below.
TESTRANDOM = $(BUILDDIR)/test/testrandom

$(TESTRANDOM): $(BUILDDIR)/test
	$(MAKE) -C $(dir $@) -f $(SRCDIR)/test/Makefile \
	    SRCDIR=$(SRCDIR) BUILDDIR=$(BUILDDIR) $(notdir $@) 

# We run a couple of programs on the build machine in computing the
# contents of pm_config.h.  We need to give the user a way not to do
# that or to override the results, because it doesn't work if he's
# cross compiling.

pm_config.h: \
  $(SRCDIR)/pm_config.in.h config.mk inttypes_netpbm.h \
  $(ENDIANGEN)
# Note that this rule depends on the effect of the .DELETE_ON_ERROR
# target we get from common.mk
	echo '/* pm_config.h GENERATED BY A MAKE RULE */' >$@
	echo '#ifndef PM_CONFIG_H' >>$@
	echo '#define PM_CONFIG_H' >>$@
ifeq ($(INTTYPES_H)x,x)
	echo '/* Dont need to #include any inttypes.h-type thing */' >>$@
else
  ifeq ($(INTTYPES_H),"inttypes_netpbm.h")
	cat inttypes_netpbm.h >>$@
  else
	echo '#include $(INTTYPES_H)' >>$@
  endif
endif
ifeq ($(HAVE_INT64),Y)
	echo "#define HAVE_INT64 1" >>$@
else
	echo "#define HAVE_INT64 0" >>$@
endif	
ifeq ($(WANT_SSE),Y)
	echo "#define WANT_SSE 1" >>$@
else
	echo "#define WANT_SSE 0" >>$@
endif	
ifeq ($(DONT_HAVE_PROCESS_MGMT),Y)
	echo "#define HAVE_FORK 0" >>$@
else
	echo "#define HAVE_FORK 1" >>$@
endif
	echo '#define RGB_DB_PATH "$(RGB_DB_PATH)"' >>$@
	echo '/* pm_config.h.in FOLLOWS ... */' >>$@
	cat $(SRCDIR)/pm_config.in.h >>$@
	$(ENDIANGEN) >>$@
	echo '#endif' >>$@


MAJOR := $(NETPBM_MAJOR_RELEASE)
MINOR := $(NETPBM_MINOR_RELEASE)
POINT := $(NETPBM_POINT_RELEASE)
version.h: $(SRCDIR)/version.mk
	@rm -f $@
	@echo "/* Generated by make file rule */" >>$@
	@echo "#define NETPBM_VERSION" \
	  \"Netpbm $(MAJOR).$(MINOR).$(POINT)"\"" >>$@


.PHONY: install
install:
	@echo "After doing a 'make', do "
	@echo ""
	@echo "  make package pkgdir=DIR"
	@echo ""
	@echo "to copy all the Netpbm files you need to install into the "
	@echo "directory DIR."
	@echo ""
	@echo "Then, do "
	@echo ""
	@echo "  ./installnetpbm"
	@echo
	@echo "to install from there to your system via an interactive.  "
	@echo "dialog.  Or do it manually using simple copy commands and "
	@echo "following instructions in the file DIR/README"

.PHONY: package package_build init_package advise_installnetpbm
package: build_complete package_build advise_installnetpbm

build_complete:
# The regular build creates this file as its last act, so if it doesn't exist,
# that means either the user skipped the build step, or the build failed.
	@echo "You must build Netpbm before you can package Netpbm. "
	@echo "The usual way to do this is to type 'make' with no arguments."
	@echo "If you did that, then the build apparently failed.  There "
	@echo "should have been error messages indicating why.  If you "
	@echo "can't fix the build problem, you can do 'make --keep-going' "
	@echo "to force the build to continue with other parts that "
	@echo "it may be able to build successfully, then do "
	@echo "'make package --keep-going' to package whatever was "
	@echo "successfully built."
	@echo
	@false;

package_build: init_package install-run install-dev 

MAJOR=$(NETPBM_MAJOR_RELEASE)
MINOR=$(NETPBM_MINOR_RELEASE)
POINT=$(NETPBM_POINT_RELEASE)

init_package:
	@if [ -d $(PKGDIR) ]; then \
	  echo "Directory $(PKGDIR) already exists.  Please specify a "; \
	  echo "directory that can be created fresh, like this: "; \
	  echo "  make package pkgdir=/tmp/newnetpbm "; \
	  false; \
	  fi
	mkdir $(PKGDIR)
	echo "Netpbm install package made by 'make package'" \
	    >$(PKGDIR)/pkginfo
	date >>$(PKGDIR)/pkginfo
	echo Netpbm $(MAJOR).$(MINOR).$(POINT) >$(PKGDIR)/VERSION
	$(INSTALL) -c -m 664 $(SRCDIR)/buildtools/README.pkg $(PKGDIR)/README
	$(INSTALL) -c -m 664 $(SRCDIR)/buildtools/config_template \
	  $(PKGDIR)/config_template
	$(INSTALL) -c -m 664 $(SRCDIR)/buildtools/pkgconfig_template \
	  $(PKGDIR)/pkgconfig_template

advise_installnetpbm:
	@echo
	@echo "Netpbm has been successfully packaged under directory"
	@echo "$(PKGDIR).  Run 'installnetpbm' to install it on your system."

.PHONY: install-run
ifeq ($(DEFAULT_TARGET),merge)
install-run: install-merge
else
install-run: install-nonmerge 
endif

.PHONY: install-merge install-nonmerge
install-merge: install.merge install.lib install.data

install-nonmerge: install.bin install.lib install.data

.PHONY: merge
merge: lib/all netpbm

MERGELIBS = 
ifneq ($(ZLIB),NONE)
  MERGELIBS += $(ZLIB)
endif
ifneq ($(JPEGLIB),NONE)
  MERGELIBS += $(JPEGLIB)
endif
ifneq ($(TIFFLIB),NONE)
  MERGELIBS += $(TIFFLIB)
endif
ifneq ($(URTLIB),NONE)
  MERGELIBS += $(URTLIB)
endif
ifneq ($(LINUXSVGALIB),NONE)
  MERGELIBS += $(LINUXSVGALIB)
endif

ifneq ($(shell pkg-config --modversion libpng$(PNGVER)),)
  PNGLD = $(shell pkg-config --libs libpng$(PNGVER))
else
  ifneq ($(shell libpng$(PNGVER)-config --version),)
    PNGLD = $(shell libpng$(PNGVER)-config --ldflags)
  else
    PNGLD = $(shell $(LIBOPT) $(LIBOPTR) $(PNGLIB) $(ZLIB))
  endif
endif

ifneq ($(shell pkg-config --modversion libxml-2.0),)
  XML2LD=$(shell pkg-config --libs libxml-2.0)
else
  ifneq ($(shell xml2-config --version),)
    XML2LD=$(shell xml2-config --libs)
  else
    XML2LD=
  endif
endif

ifneq ($(shell pkg-config x11 --libs),)
  X11LD = $(shell pkg-config x11 --libs)
else
  X11LD = $(shell $(LIBOPT) $(LIBOPTR) $(X11LIB))
endif



# If URTLIB is BUNDLED_URTLIB, then we're responsible for building it, which
# means it needs to be a dependency:
ifeq ($(URTLIB),$(BUNDLED_URTLIB))
  URTLIBDEP = $(URTLIB)
endif

# We have two different ways to do the merge build:
#  
#   1) Each directory produces an object file merge.o containing all the code
#      in that directory and its descendants that needs to go into the 'netpbm'
#      program.  The make files do this recursively, via a link command that
#      combines multiple relocateable object files into one.  All we do here
#      at the top level is make merge.o and link it with netpbm.o and the
#      libraries.
#
#      This is the clean way, and we use it whenever we can.  But we don't
#      know how to do the link on every platform.
#
#   2) Each directory produces a list of all the object files in that 
#      directory and its descendants that need to go into the 'netpbm'
#      program.  This list is in a file called 'mergelist'.  The make files
#      do this recursively.  Here at the top level, we make mergelist and
#      then do one large link of everything listed in it, plus netpbm.o and
#      the libraries.
#
#      This doesn't require any special link command like (1), but is
#      not very clean.  The dependencies don't work right.  And at least
#      one linker (on DJGPP) can't handle that many input files.

ifeq ($(LDRELOC),NONE)
  OBJECT_DEP = mergelist
  OBJECT_LIST = `cat mergelist`
else
  OBJECT_DEP = merge.o
  OBJECT_LIST = merge.o
endif

netpbm:%:%.o $(OBJECT_DEP) $(NETPBMLIB) $(URTLIBDEP) $(LIBOPT)
# Note that LDFLAGS might contain -L options, so order is important.
	$(LD) -o $@ $< $(OBJECT_LIST) \
          $(LDFLAGS) $(shell $(LIBOPT) $(NETPBMLIB) $(MERGELIBS)) \
	  $(PNGLD) $(XML2LD) $(X11LD) $(MATHLIB) $(NETWORKLD) $(LADD)

netpbm.o: mergetrylist

ifneq ($(NETPBMLIBTYPE),unixstatic)
install.lib: lib/install.lib
else
install.lib:
endif

.PHONY: install-dev
# Note that you might install the development package and NOT the runtime
# package.  If you have a special system for building stuff, maybe for 
# multiple platforms, that's what you'd do.  Ergo, install.lib is here even
# though it is also part of the runtime install.
install-dev: install.hdr install.staticlib install.lib install.sharedlibstub

.PHONY: install.hdr
install.hdr: lib/install.hdr $(PKGDIR)/include/netpbm
	$(INSTALL) -c -m $(INSTALL_PERM_HDR) \
	    $(BUILDDIR)/pm_config.h $(PKGDIR)/include/netpbm

.PHONY: lib/install.hdr
lib/install.hdr:
	$(MAKE) -C $(dir $@) -f $(SRCDIR)/lib/Makefile \
	    SRCDIR=$(SRCDIR) BUILDDIR=$(BUILDDIR) $(notdir $@)

ifeq ($(STATICLIB_TOO),Y)
BUILD_STATIC = Y
else
  ifeq ($(NETPBMLIBTYPE),unixstatic)
    BUILD_STATIC = Y
  else
    BUILD_STATIC = N
  endif
endif

.PHONY: install.staticlib
install.staticlib: 
ifeq ($(BUILD_STATIC),Y)
	$(MAKE) -C lib -f $(SRCDIR)/lib/Makefile \
	SRCDIR=$(SRCDIR) BUILDDIR=$(BUILDDIR) install.staticlib 
endif

.PHONY: install.sharedlibstub
install.sharedlibstub:
	$(MAKE) -C lib -f $(SRCDIR)/lib/Makefile \
	    SRCDIR=$(SRCDIR) BUILDDIR=$(BUILDDIR) install.sharedlibstub 

# Make the 'deb' target after making 'package'.  It generates a .deb
# file in the current directory.
.PHONY: deb
deb:
	buildtools/debian/mkdeb --buildtools=buildtools --pkgdir=$(PKGDIR)


.PHONY: check
.PHONY: check-tree
.PHONY: check-package
.PHONY: check-install

# Variables from the make env we pass down to the test scripts.
CHECK_VARS = \
	BUILDDIR=$(BUILDDIR) \
	RGBDEF=$(RGBDEF) \
	PALMMAPDIR=$(PALMMAPDIR) \
	BUILD_FIASCO=$(BUILD_FIASCO) \
	JASPERLIB="$(JASPERLIB)" \
	JBIGLIB="$(JBIGLIB)" \
	JPEGLIB="$(JPEGLIB)" \
	PNGLIB="$(PNGLIB)" \
	TIFFLIB="$(TIFFLIB)" \
	URTLIB="$(URTLIB)" \
	X11LIB="$(X11LIB)" \
	XML2_LIBS="$(XML2_LIBS)" \
	LEX="$(LEX)" \
	ZLIB="$(ZLIB)" \

# Test files in source tree.
# BUILDBINDIRS is a list of directories which contain target binaries

check-tree : BUILDBINDIRS :=./analyzer \
./converter/other \
./converter/other/cameratopam \
./converter/other/fiasco \
./converter/other/jbig \
./converter/other/jpeg2000 \
./converter/other/pamtosvg \
./converter/other/pnmtopalm \
./converter/pbm \
./converter/pbm/pbmtoppa \
./converter/pgm \
./converter/ppm \
./converter/ppm/ppmtompeg \
./converter/ppm \
./editor \
./editor/pamflip \
./editor/specialty \
./generator \
./generator/pamtris \
./other \
./other/pamx

check-tree : SRCBINDIRS :=./converter/ \
./converter/other/ \
./converter/ppm/hpcdtoppm/ \
./editor \
./generator \
./other \
./

# Create colon-separated PATH list from the above.
# Use realpath function (appears in GNU Make v.3.81) if available.

# Kludge to test whether realpath is available:
ifeq ($(realpath $(CURDIR)/.),$(CURDIR))
  check-tree : RBINDIRS :=$(sort \
     $(foreach dir,$(BUILDBINDIRS),$(realpath $(BUILDDIR)/$(dir))) \
     $(foreach dir,$(SRCBINDIRS),$(realpath $(SRCDIR)/$(dir))))
else
  check-tree : RBINDIRS :=$(sort \
     $(foreach dir,$(BUILDBINDIRS),$(BUILDDIR)/$(dir)) \
     $(foreach dir,$(SRCBINDIRS),$(SRCDIR)/$(dir)))
endif

# Kludge to express characters given special meanings by GNU Make.
# See GNU Make texinfo manual "Function Call Syntax".
empty :=
space := $(empty) $(empty)
colon :=:

check-tree : PBM_TEST_PATH := $(subst $(space),$(colon),$(strip $(RBINDIRS)))
check-tree : PBM_LIBRARY_PATH ?= $(BUILDDIR)/lib
check-tree : RGBDEF ?= $(SRCDIR)/lib/rgb.txt
check-tree : PALMMAPDIR ?= $(SRCDIR)/converter/other/pnmtopalm


# Create RESULTDIR.
# If it already exists, rename and convert to an archive directory.
# Use numbered backup.
# Note: Renaming fails with old versions of mv which do not have -T.  

resultdir-backup: FORCE
	if [ -d $(RESULTDIR) ]; \
	   then mv -T --backup=numbered $(RESULTDIR) $(RESULTDIR).bak; \
	fi; \
	mkdir -p $(RESULTDIR); \

.PHONY: testdir test

testdir:
	$(MAKE) -C test

check-tree: testdir resultdir-backup
	cd $(RESULTDIR); \
	  $(CHECK_VARS) \
	  CHECK_TYPE=tree \
	  PBM_TEST_PATH=$(PBM_TEST_PATH) \
	  LD_LIBRARY_PATH=$(PBM_LIBRARY_PATH):${LD_LIBRARY_PATH} \
	  $(SRCDIR)/test/Execute-Tests 2>&1

# Execute-Tests needs to know BUILDDIR in order to locate testrandom.
# This applies to all check varieties.

# Check after the packaging stage
# This works on typical Linux systems.
# This is the default check.

check-package : PBM_TEST_PATH := $(PKGDIR)/bin
check-package : PBM_LIBRARY_PATH := $(PKGDIR)/lib
check-package : RGBDEF ?= $(PKGDIR)/misc/rgb.txt
check-package : PALMMAPDIR ?= $(PKGDIR)/misc/
check: check-package

check-package: testdir resultdir-backup
	cd $(RESULTDIR); \
	  $(CHECK_VARS) \
	  CHECK_TYPE=package \
	  PBM_TEST_PATH=$(PBM_TEST_PATH) \
	  LD_LIBRARY_PATH=$(PBM_LIBRARY_PATH):${LD_LIBRARY_PATH} \
	  $(SRCDIR)/test/Execute-Tests 2>&1


# Check after install
check-install: PALMMAPDIR ?= ""
check-install: testdir resultdir-backup
	cd $(RESULTDIR); \
	  $(CHECK_VARS) \
	  CHECK_TYPE=install \
	  $(SRCDIR)/test/Execute-Tests 2>&1



clean: localclean

.PHONY: localclean
localclean:
	rm -f netpbm build_started build_complete
	rm -f pm_config.h inttypes_netpbm.h version.h
	rm -f *.deb

# Note that removing config.mk must be the last thing we do,
# because no other makes will work after that is done.
distclean: localdistclean
.PHONY: localdistclean
localdistclean: localclean
	-rm -f `find -type l`
	-rm -f TAGS
	-rm -f config.mk

# 'tags' generates/updates an Emacs tags file, named TAGS, in the current
# directory.  Use with Emacs command 'find-tag'.

.PHONY: tags
tags:
	find . -name "*.c" -o -name "*.h" -o -name "*.cpp" -o -name "*.hpp" | \
	  etags -

# The following endif is for the else block that contains virtually the
# whole file, for the test of the existence of CURDIR.
endif
