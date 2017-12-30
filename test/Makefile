ifeq ($(SRCDIR)x,x)
  SRCDIR = $(CURDIR)/..
  BUILDDIR = $(SRCDIR)
endif
SUBDIR = test
VPATH = .:$(SRCDIR)/$(SUBDIR)
include $(BUILDDIR)/config.mk

MERGE_OBJECTS =

PROGS = testrandom

OKSTOGENERATE = $(patsubst %.rand-ok, %.ok, $(wildcard *.rand-ok))

all: $(PROGS) $(OKSTOGENERATE)

testrandom.o: testrandom.c
	$(CC_FOR_BUILD) -c -o $@ $(CFLAGS_FOR_BUILD) $<

testrandom: testrandom.o
	$(LD_FOR_BUILD) -o $@ $(LDFLAGS_FOR_BUILD) $<

RAND_VARIETY ?= $(shell ./testrandom -x)

$(OKSTOGENERATE): %.ok: %.rand-ok testrandom
	sed -n "/^$(RAND_VARIETY)|/s/^$(RAND_VARIETY)|//p" $< > $@

OMIT_TEST_RULE = 1
include $(SRCDIR)/common.mk

distclean clean: cleanlocal
.PHONY: cleanlocal
cleanlocal:
	rm -f $(PROGS) $(OKSTOGENERATE)
