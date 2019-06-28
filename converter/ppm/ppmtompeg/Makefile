ifeq ($(SRCDIR)x,x)
  SRCDIR = $(CURDIR)/../../..
  BUILDDIR = $(SRCDIR)
endif
SUBDIR = converter/ppm/ppmtompeg
VPATH=.:$(SRCDIR)/$(SUBDIR)

include $(BUILDDIR)/config.mk

ifeq ($(JPEGLIB),NONE)
  # 'nojpeg' is a module that implements all the jpeg access routines as
  # error messages that tell you we don't have jpeg capability
  JPEG_MODULE = nojpeg
  JPEGLIBX =
else
  # 'jpeg' is a module that accesses J-movies via the JPEG library.
  JPEG_MODULE = jpeg
  JPEGLIBX = $(JPEGLIB)
endif

COMP_INCLUDES = -I$(SRCDIR)/$(SUBDIR)/headers 

EXTERN_INCLUDES =
ifneq ($(JPEGHDR_DIR),NONE)
  ifneq ($(JPEGHDR_DIR)x,x)
    EXTERN_INCLUDES += -I$(JPEGHDR_DIR)
  endif
endif

# use -DLONG_32 iff
#	1) long's are 32 bits and
#	2) int's are not
#
# one other option:
#	-DHEINOUS_DEBUG_MODE
#

MP_BASE_OBJS = \
  mfwddct.o \
  postdct.o \
  huff.o \
  bitio.o \
  mheaders.o \

MP_ENCODE_OBJS = \
  frames.o \
  iframe.o \
  pframe.o \
  bframe.o \
  psearch.o \
  bsearch.o \
  block.o \

MP_OTHER_OBJS = \
  mpeg.o \
  subsample.o \
  param.o \
  rgbtoycc.o \
  readframe.o \
  combine.o \
  jrevdct.o \
  frame.o \
  fsize.o \
  frametype.o \
  specifics.o \
  rate.o \
  opts.o \
  input.o \

ifeq ($(OMIT_NETWORK),Y)
  MP_OTHER_OBJS += noparallel.o
else
  MP_OTHER_OBJS += parallel.o psocket.o
endif
ifeq ($(MSVCRT),Y)
  MP_OTHER_OBJS += gethostname_win32.o
else
  MP_OTHER_OBJS += gethostname.o
endif

ADDL_OBJECTS = \
  $(MP_BASE_OBJS) \
  $(MP_OTHER_OBJS) \
  $(MP_ENCODE_OBJS) \
  $(JPEG_MODULE).o \

OBJECTS = ppmtompeg.o $(ADDL_OBJECTS)
MERGE_OBJECTS = ppmtompeg.o2 $(ADDL_OBJECTS)
MP_INCLUDE = mproto.h mtypes.h huff.h bitio.h
MP_MISC = Makefile huff.table parse_huff.pl

PORTBINARIES = ppmtompeg
BINARIES = $(PORTBINARIES)
MERGEBINARIES = $(BINARIES)
SCRIPTS = 

.PHONY: all
all: ppmtompeg

include $(SRCDIR)/common.mk

ifeq ($(NEED_RUNTIME_PATH),Y)
  LIBOPTR = -runtime
else
  LIBOPTR =
endif

ppmtompeg: $(ADDL_OBJECTS) $(LIBOPT)
ppmtompeg: LDFLAGS_TARGET = \
  $(shell $(LIBOPT) $(LIBOPTR) $(JPEGLIBX)) $(NETWORKLD)

profile: $(ADDL_OBJECTS) $(LIBOPT)
profile: LDFLAGS_TARGET = \
  -Bstatic -pg \
  $(shell $(LIBOPT) $(LIBOPTR) $(JPEGLIBX)) $(NETWORKLD)

#########
# OTHER #
#########

#
# Perl is necessary if you want to modify the Huffman RLE encoding table.
#
PERL = perl

# The following stuff is for the Huffman encoding tables.  It's commented-out
# because you probably don't want to change this.  If you do, then uncommment
# it.
#
# huff.h: huff.c
#
# huff.c: parse_huff.pl huff.table
#	$(PERL) parse_huff.pl huff.table

MP_ALL_SRCS = $(patsubst %.o, %.c, $(MP_ALL_OBJS))

wc:;		wc -l *.[ch] headers/*.h *.pl *.table
ci:;		ci -l $(MP_ALL_SRCS) $(MP_INCLUDE) $(MP_MISC)
tags: $(MP_ALL_SRCS)
	ctags -t $(MP_ALL_SRCS)
	etags -f TAGS -t $(MP_ALL_SRCS) headers/*.h

#
# WARNING: this assumes you're using GNU indent...
#
indent:;	indent -T FILE -T int8 -T int16 -T int32 -T uint8 -T uint16 -T uint32  -T BitBucket -T MpegFrame -T Block -T FlatBlock $(MP_ALL_SRCS)

spotless: clean	
	rm -f huff.c huff.h *.pure.a

FORCE:

# 
# Copyright (c) 1995 The Regents of the University of California.
# All rights reserved.
# 
# Permission to use, copy, modify, and distribute this software and its
# documentation for any purpose, without fee, and without written agreement is
# hereby granted, provided that the above copyright notice and the following
# two paragraphs appear in all copies of this software.
# 
# IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO ANY PARTY FOR
# DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES ARISING OUT
# OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF
# CALIFORNIA HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
# 
# THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY WARRANTIES,
# INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
# AND FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
# ON AN "AS IS" BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
# PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
# 
