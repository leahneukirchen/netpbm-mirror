ifeq ($(SRCDIR)x,x)
  SRCDIR = $(CURDIR)/../..
  BUILDDIR = $(SRCDIR)
endif
SUBDIR = converter/other
VPATH=.:$(SRCDIR)/$(SUBDIR)

include $(BUILDDIR)/config.mk

TEST_PKGCONFIG_LIBXML2 = if pkg-config libxml-2.0; then echo exists; fi

ifneq ($(shell $(TEST_PKGCONFIG_LIBXML2)),)
  # pkg-config libxml2 works on this system
  XML2_LIBS = $(shell pkg-config libxml-2.0 --libs)
  XML2_CFLAGS = $(shell pkg-config libxml-2.0 --cflags)
else
  ifeq ($(shell xml2-config --version),)
    XML2_LIBS=NONE
    XML2_CFLAGS=NONE
  else
    XML2_LIBS=$(shell xml2-config --libs)
    XML2_CFLAGS=$(shell xml2-config --cflags)
  endif
endif

SUBDIRS = jbig pnmtopalm jpeg2000 cameratopam pamtosvg
ifneq ($(BUILD_FIASCO), N)
  SUBDIRS += fiasco
endif

ifneq ($(TIFFLIB),NONE)
  ifneq ($(TIFFHDR_DIR)x,x)
    EXTERN_INCLUDES += -I$(TIFFHDR_DIR)
  endif
endif

TEST_PKGCONFIG_LIBPNG = if pkg-config libpng$(PNGVER); then echo exists; fi

ifneq ($(shell $(TEST_PKGCONFIG_LIBPNG)),)
  # pkg-config libpng works on this system
  HAVE_PNGLIB = Y
  EXTERN_INCLUDES += $(shell pkg-config libpng$(PNGVER) --cflags)
else
  ifneq ($(shell libpng$(PNGVER)-config --version),)
    # No pkg-config, but we have libpng-config on this system
    HAVE_PNGLIB = Y
    EXTERN_INCLUDES += $(shell libpng$(PNGVER)-config --cflags)
  else
    # System can't tell use where libpng is; use stuff from config.mk
    ifneq ($(PNGLIB),NONE)
      HAVE_PNGLIB = Y
      ifneq ($(PNGHDR_DIR)x,x)
        EXTERN_INCLUDES += -I$(PNGHDR_DIR)
      endif
      ifneq ($(ZHDR_DIR)x,x)
        EXTERN_INCLUDES += -I$(ZHDR_DIR)
      endif
    endif
  endif
endif

ifneq ($(JPEGLIB),NONE)
  ifneq ($(JPEGHDR_DIR)x,x)
    EXTERN_INCLUDES += -I$(JPEGHDR_DIR)
  endif
endif
ifneq ($(URTLIB),NONE)
  ifneq ($(URTHDR_DIR)x,x)
    EXTERN_INCLUDES += -I$(URTHDR_DIR)
  endif
endif
ifneq ($(XML2_LIBS),NONE)
  ifneq ($(XML2_CFLAGS),NONE)
    EXTERN_INCLUDES += $(XML2_CFLAGS)
  endif
endif

ifeq ($(TIFFLIB),NONE)
  TIFF_PREREQ_MISSING = Y
endif

TIFFLIB_EXTRALIBS =
ifeq ($(TIFFLIB_NEEDS_JPEG),Y)
  ifeq ($(JPEGLIB),NONE)
    TIFF_PREREQ_MISSING = Y
  else
    TIFFLIB_EXTRALIBS += $(JPEGLIB)
  endif
endif
ifeq ($(TIFFLIB_NEEDS_Z),Y)
  ifeq ($(ZLIB),NONE)
    TIFF_PREREQ_MISSING = Y
  else
    TIFFLIB_EXTRALIBS += $(ZLIB)
  endif
endif

# TIFFLIB_USERLIBS is for the user to set manually, on the make command line
# or manually added to config.mk.  There are arcane situations where the TIFF
# library refers to additional libraries not handled above.

TIFFLIB_EXTRALIBS += $(TIFFLIB_USERLIBS)

# Pnmtops's "flate" compression function requires libz.  But if we don't
# have libz, we still build Pnmtops; we just omit the flate compression
# capability.
ifeq ($(ZLIB),NONE)
  PNMTOPS_ZLIB_OPT =
  PNMTOPS_NOFLATE_OPT = -DNOFLATE
else
  PNMTOPS_ZLIB_OPT = $(ZLIB)
  PNMTOPS_NOFLATE_OPT =
endif


PORTBINARIES =  avstopam bmptopnm fitstopnm \
		gemtopnm giftopnm hdifftopam infotopam \
		pamtoavs pamtodjvurle pamtofits pamtogif \
		pamtohdiff pamtohtmltbl pamtompfont pamtooctaveimg \
		pamtopam pamtopfm pamtopnm pamtouil \
		pamtoxvmini \
		pbmtopgm pfmtopam \
	        pgmtopbm pgmtoppm ppmtopgm pnmtoddif \
		pnmtopclxl \
		pnmtosgi pnmtosir pamtotga pnmtoxwd \
		rlatopam sgitopnm sirtopnm sunicontopnm xwdtopnm zeisstopnm

ifneq ($(DONT_HAVE_PROCESS_MGMT),Y)
  PORTBINARIES += pstopnm
  BINARIES += pnmtops
endif

BINARIES = \
  $(PORTBINARIES) \
  pamtopdbimg \
  pamtosrf \
  pdbimgtopam \
  pnmtorast \
  rasttopnm \
  srftopam \

ifeq ($(HAVE_PNGLIB),Y)
  BINARIES += pnmtopng pngtopam pamrgbatopng
endif
ifneq ($(JPEGLIB),NONE)
  BINARIES += jpegtopnm pnmtojpeg
endif
ifneq ($(TIFF_PREREQ_MISSING),Y)
  BINARIES += tifftopnm pamtotiff pnmtotiffcmyk
endif
ifneq ($(URTLIB),NONE)
  BINARIES += rletopnm pnmtorle
endif

ifneq ($(XML2_LIBS),NONE)
  BINARIES += svgtopam
endif 

MERGEBINARIES = $(BINARIES)

EXTRA_OBJECTS = exif.o rast.o ipdb.o srf.o
ifeq ($(HAVE_PNGLIB),Y)
  EXTRA_OBJECTS += pngtxt.o
  EXTRA_OBJECTS += pngx.o
endif
ifneq ($(JPEGLIB),NONE)
  EXTRA_OBJECTS += jpegdatasource.o
endif
ifneq (($TIFF_PREREQ_MISSING),Y)
  EXTRA_OBJECTS += tiff.o
endif

OBJECTS = $(BINARIES:%=%.o) $(EXTRA_OBJECTS)
MERGE_OBJECTS = $(MERGEBINARIES:%=%.o2) $(EXTRA_OBJECTS)

pnmtops.o pnmtops.o2: CFLAGS_TARGET=$(PNMTOPS_NOFLATE_OPT)

SCRIPTS = anytopnm pnmtoplainpnm

.PHONY: all
all:	$(BINARIES) $(SUBDIRS:%=%/all)

include $(SRCDIR)/common.mk

ifeq ($(NEED_RUNTIME_PATH),Y)
  LIBOPTR = -runtime
else
  LIBOPTR =
endif

LIBOPTS_TIFF = $(shell $(LIBOPT) $(NETPBMLIB) \
  $(LIBOPTR) $(TIFFLIB) $(TIFFLIB_EXTRALIBS))

tifftopnm pamtotiff pnmtotiffcmyk: %: %.o tiff.o $(NETPBMLIB) $(LIBOPT)
	$(LD) -o $@ $@.o tiff.o \
	  $(LIBOPTS_TIFF) $(MATHLIB) $(LDFLAGS) $(LDLIBS) $(RPATH) $(LADD)

ifneq ($(shell $(TEST_PKGCONFIG_LIBPNG)),)
  # pkg-config libpng works on this system
  PNGLIB_LIBOPTS = $(shell pkg-config libpng$(PNGVER) --libs)
else
  ifneq ($(shell libpng$(PNGVER)-config --version),)
    # No pkg-config, but we have libpng-config on this system
    PNGLIB_LIBOPTS = $(shell libpng$(PNGVER)-config --ldflags)
  else
    # System can't tell us where libpng is; use stuff from config.mk
    PNGLIB_LIBOPTS = $(shell $(LIBOPT) $(LIBOPTR) $(PNGLIB) $(ZLIB))
  endif
endif

pngtopam: %: %.o pngx.o $(NETPBMLIB) $(LIBOPT)
	$(LD) -o $@ $@.o pngx.o \
	  $(shell $(LIBOPT) $(NETPBMLIB)) \
	  $(PNGLIB_LIBOPTS) $(MATHLIB) $(LDFLAGS) $(LDLIBS) $(RPATH) $(LADD)

pnmtopng: %: %.o pngx.o pngtxt.o $(NETPBMLIB) $(LIBOPT)
	$(LD) -o $@ $@.o pngx.o pngtxt.o \
	  $(shell $(LIBOPT) $(NETPBMLIB)) \
	  $(PNGLIB_LIBOPTS) $(MATHLIB) $(LDFLAGS) $(LDLIBS) $(RPATH) $(LADD)

pamrgbatopng: %: %.o pngx.o $(NETPBMLIB) $(LIBOPT)
	$(LD) -o $@ $@.o pngx.o \
	  $(shell $(LIBOPT) $(NETPBMLIB)) $(PNGLIB_LIBOPTS) \
	  $(MATHLIB) $(LDFLAGS) $(LDLIBS) $(RPATH) $(LADD)

jpegtopnm: %: %.o jpegdatasource.o exif.o $(NETPBMLIB) $(LIBOPT)
	$(LD) -o $@ $< jpegdatasource.o exif.o \
	  $(shell $(LIBOPT) $(NETPBMLIB) $(LIBOPTR) $(JPEGLIB)) \
	  $(MATHLIB) $(LDFLAGS) $(LDLIBS) $(RPATH) $(LADD) 

pnmtojpeg: %: %.o $(NETPBMLIB) $(LIBOPT)
	$(LD) -o $@ $@.o \
	  $(shell $(LIBOPT) $(NETPBMLIB) $(LIBOPTR) $(JPEGLIB)) \
	  $(MATHLIB) $(LDFLAGS) $(LDLIBS) $(RPATH) $(LADD)

srftopam pamtosrf: %: %.o srf.o $(NETPBMLIB) $(LIBOPT)
	$(LD) -o $@ $@.o srf.o \
	  $(shell $(LIBOPT) $(NETPBMLIB)) \
	  $(MATHLIB) $(LDFLAGS) $(LDLIBS) $(RPATH) $(LADD)

svgtopam: %: %.o $(NETPBMLIB) $(LIBOPT)
	$(LD) -o $@ $@.o \
	  $(shell $(LIBOPT) $(NETPBMLIB) $(LIBOPTR)) \
	  $(XML2_LIBS) $(MATHLIB) $(LDFLAGS) $(LDLIBS) $(RPATH) $(LADD)

# If URTLIB is BUNDLED_URTLIB, then we're responsible for building it, which
# means it needs to be a dependency:
ifeq ($(URTLIB), $(BUNDLED_URTLIB))
  URTLIBDEP = $(URTLIB)
endif

rletopnm pnmtorle: %: %.o $(NETPBMLIB) $(URTLIBDEP) $(LIBOPT)
	$(LD) -o $@ $@.o \
	  $(shell $(LIBOPT) $(URTLIB) $(NETPBMLIB)) \
	  $(MATHLIB) $(LDFLAGS) $(LDLIBS) $(RPATH) $(LADD)

pnmtops: %: %.o $(NETPBMLIB) $(LIBOPT)
	$(LD) -o $@ $@.o \
	  $(shell $(LIBOPT) $(NETPBMLIB) $(PNMTOPS_ZLIB_OPT)) \
	  $(MATHLIB) $(LDFLAGS) $(LDLIBS) $(RPATH) $(LADD)

pnmtorast rasttopnm: %: %.o rast.o $(NETPBMLIB) $(LIBOPT)
	$(LD) -o $@ $@.o rast.o \
	  $(shell $(LIBOPT) $(NETPBMLIB)) \
	  $(MATHLIB) $(LDFLAGS) $(LDLIBS) $(RPATH) $(LADD)

pdbimgtopam pamtopdbimg: %: %.o ipdb.o $(NETPBMLIB) $(LIBOPT)
	$(LD) -o $@ $@.o ipdb.o \
	  $(shell $(LIBOPT) $(NETPBMLIB)) \
	  $(MATHLIB) $(LDFLAGS) $(LDLIBS) $(RPATH) $(LADD)

bmptopnm.o bmptopnm.o2: bmp.h

pamtotga.o pamtotga.o2: tga.h

install.bin: install.bin.local
.PHONY: install.bin.local
install.bin.local: $(PKGDIR)/bin
# Remember that $(SYMLINK) might just be a copy command.
# backward compatibility: program used to be named pnmnoraw
	cd $(PKGDIR)/bin ; \
	$(SYMLINK) pnmtoplainpnm pnmnoraw
# backward compatibility: program used to be gemtopbm
	cd $(PKGDIR)/bin ; \
	$(SYMLINK) gemtopnm$(EXE) gemtopbm
# In October 2001, pnmtojpeg replaced ppmtojpeg
ifneq ($(JPEGLIB),NONE)
	cd $(PKGDIR)/bin ; \
	$(SYMLINK) pnmtojpeg$(EXE) ppmtojpeg
endif
# In March 2002, bmptopnm replaced bmptoppm
	cd $(PKGDIR)/bin ; \
	$(SYMLINK) bmptopnm$(EXE) bmptoppm
# In May 2002, pamtouil replaced ppmtouil
	cd $(PKGDIR)/bin ; \
	$(SYMLINK) pamtouil$(EXE) ppmtouil
# In March 2005, we realized that pamtopnm obviates pnmtopnm
	cd $(PKGDIR)/bin ; \
	$(SYMLINK) pamtopnm$(EXE) pnmtopnm
# In October 2005, pamtofits replaced pnmtofits
	cd $(PKGDIR)/bin ; \
	$(SYMLINK) pamtofits$(EXE) pnmtofits
ifneq ($(TIFF_PREREQ_MISSING),Y)
# In October 2005, pamtotiff replaced pnmtotiff
	cd $(PKGDIR)/bin ; \
	$(SYMLINK) pamtotiff$(EXE) pnmtotiff
endif
ifeq ($(HAVE_PNGLIB),Y)
# In September 2009, pngtopam replaced pngtopnm
	cd $(PKGDIR)/bin ; \
	$(SYMLINK) pngtopam$(EXE) pngtopnm
endif
