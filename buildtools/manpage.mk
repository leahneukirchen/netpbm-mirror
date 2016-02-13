# Make Unix man pages from Netpbm HTML user manual
# GNU make version 3.81 or newer recommended.
# Tested with GNU make version 3.80.

# CAVEAT: USERGUIDE must be a valid directory: even for "make clean"!

# MAKEFILE_DIR is the directory with this file: manpage.mk.
# Should be buildtools.
# Use $(realpath) and $(lastword) if available.
# (both appear in GNU make v. 3.81)

ifeq ($(realpath $(CURDIR)/.),$(CURDIR))
  MAKEFILE_DIR := $(dir $(realpath $(lastword $(MAKEFILE_LIST))))
else
  MAKEFILE_DIR := $(dir $(word $(words $(MAKEFILE_LIST)),$(MAKEFILE_LIST)))
endif

# Python script makeman should be in the same directory.
MAKEMAN ?= $(MAKEFILE_DIR)makeman

# Install location of manpages.
# Subdirectories man{1,3,5} must exist.
MANDIR ?= /usr/share/man/

# Directory with the HTML input files.  It must be explicitly set and
# must be a valid directory.

ifeq ($(USERGUIDE),)
  $(error error: Variable USERGUIDE must be explicitly set)
else
  ifeq ($(wildcard $(USERGUIDE)/*html),)
    $(error error: No HTML files found in $(USERGUIDE))
  endif
endif

# In the past the following default value was used.
# It works if you've done a SVN checkout for netpbm and userguide in the
# same directory, and you are working in a subdirectory of netpbm, say
# ./buildtools .
# USERGUIDE = ../../userguide

# The files that don't get converted to manual pages.
# Override at the command line if necessary.

# error.html: appears when problem occurs when fetching HTML files with wget.
# directory.html: list of Netpbm programs.
# libnetpbm_dir.html: directory to pages describing libnetpbm functions
# hpcdtoppm:  Not distributed via Sourceforge for copyright restrictions.
# ppmsvgalib: Not used in systems with X Window System.
# vidtoppm:   Does not compile due to missing header files.

EXCEPTIONS = \
	directory.html \
	error.html \
	hpcdtoppm.html \
	liberror.html \
	libnetpbm_dir.html \
	ppmsvgalib.html \
	vidtoppm.html

# File lists

# We do not provide a list of troff manpages to be generated.
# Instead the list is generated afresh from HTML file names.  Reasons:
# 1. Any list would have to be updated every time an HTML file is added.
# 2. The suffix (man section) depends on content (a "META" tag) of the
#    HTML file.  (The mankeman script is clever.)
# 3. In one instance the file stem name changes: index.html => netpbm.1

# HTML files in USERGUIDE
HTML_ALL := $(sort $(notdir $(wildcard $(USERGUIDE)/*.html)))
HTMLMANUALS := $(filter-out $(EXCEPTIONS),$(HTML_ALL))
HTML_REJECT := $(filter $(EXCEPTIONS),$(HTML_ALL))

# Subsets of HTMLMANUALS, by target man section
HTML3 := $(shell cd $(USERGUIDE) && \
                fgrep -l -i '<META NAME="manual_section" CONTENT="3">' \
                      $(HTMLMANUALS))
HTML5 := $(shell cd $(USERGUIDE) && \
                fgrep -l -i '<META NAME="manual_section" CONTENT="5">' \
                      $(HTMLMANUALS))
HTML1 := $(filter-out $(HTML3) $(HTML5),$(HTMLMANUALS))

# Troff man pages, by section
MAN1 := $(patsubst index.1,netpbm.1,$(HTML1:.html=.1))
MAN3 := $(HTML3:.html=.3)
MAN5 := $(HTML5:.html=.5)
MANPAGES := $(MAN1) $(MAN3) $(MAN5)

# XML
XML1 := $(MAN1:.1=.xml)
XML3 := $(MAN3:.3=.xml)
XML5 := $(MAN5:.5=.xml)
XMLPAGES = $(XML1) $(XML3) $(XML5)

.PHONY : report
report: htmlcount manpagecount

.PHONY : manpagecount
manpagecount:
	@echo Number of actual / expected troff man pages in current directory:
	@echo Section 1: $(words $(wildcard $(MAN1))) / $(words $(MAN1)) 
	@echo Section 3: $(words $(wildcard $(MAN3))) / $(words $(MAN3)) 
	@echo Section 5: $(words $(wildcard $(MAN5))) / $(words $(MAN5)) 
	@echo total: $(words $(wildcard $(MANPAGES))) / $(words $(MANPAGES))
	@echo

.PHONY : htmlcount
htmlcount:
	@echo HTML files in USERGUIDE directory: $(USERGUIDE)
	@echo Total HTML files: $(words $(HTML_ALL))
	@echo Rejected HTML files: $(HTML_REJECT) : $(words $(HTML_REJECT))
	@echo Valid HTML files: $(words $(HTMLMANUALS))
	@echo

.PHONY : reportvalid
reportvalid:
	@echo Source HTML files in USERGUIDE directory: $(USERGUIDE)
	@echo $(HTMLMANUALS)

# Note that this may give different results from "ls ."
.PHONY : reportman
reportman:
	@echo $(MANPAGES)

# Static rules for converting HTML to troff man -- reports bad lines
# to standard error.
%.1 %.3 %.5: $(USERGUIDE)/%.html
	@echo Converting $< to $@
	@python $(MAKEMAN) -d $(USERGUIDE) $(<F) 

netpbm.1: $(USERGUIDE)/index.html
	@echo Converting $< to $@
	@python $(MAKEMAN) -d $(USERGUIDE) index.html
	@mv index.1 netpbm.1

# Generate man pages
.PHONY : manpages
manpages: $(MANPAGES)

# Static rules for converting troff man to XML.
$(XML1): %.xml: %.1
	doclifter -v $<
	mv $<.xml $@

$(XML3): %.xml: %.3
	doclifter -v $<
	mv $<.xml $@

$(XML5): %.xml: %.5
	doclifter -v $<
	mv $<.xml $@

# Generate XML pages.
# TODO: Does not work completely.  Some pages have glitches.
.PHONY : xmlpages
xmlpages: manpages $(XMLPAGES)

# Validate XML pages.
# TODO: Not working.
.PHONY : xmlvalidate
xmlvalidate: xmlpages
	xmllint -xinclude --postvalid $< >> /dev/null

# This will install the generated man pages.
# Note that lists MAN1 MAN3 MAN5 depend upon the names of HTML files
# in the USERGUIDE directory, even after man page generation.
# If the current directory has "pbm.1" but USERGUIDE does not have
# "pbm.html", the document will not be installed.
# If the USERGUIDE directory is empty, no documents will be installed.

.PHONY : installman
installman: report
	set -x
	for f in $(wildcard $(MAN1)); do if [ -f $$f ]; then gzip <$$f >$(MANDIR)/man1/$$f.gz; fi; done
	for f in $(wildcard $(MAN3)); do if [ -f $$f ]; then gzip <$$f >$(MANDIR)/man3/$$f.gz; fi; done
	for f in $(wildcard $(MAN5)); do if [ -f $$f ]; then gzip <$$f >$(MANDIR)/man5/$$f.gz; fi; done


# This will uninstall the man pages.
# Only pages with corresponing files in USERGUIDE are deleted.
.PHONY : uninstallman
uninstallman: report
	for f in $(MAN1); do if [ -f $(MANDIR)/man1/$$f.gz ]; then rm -f $(MANDIR)/man1/$$f.gz; fi; done
	for f in $(MAN3); do if [ -f $(MANDIR)/man3/$$f.gz ]; then rm -f $(MANDIR)/man3/$$f.gz; fi; done
	for f in $(MAN5); do if [ -f $(MANDIR)/man5/$$f.gz ]; then rm -f $(MANDIR)/man5/$$f.gz; fi; done


# Legacy uninstall target.
#oldclean:
#	# Clean up old locations on Fedora Core 2
#	rm -f $(MANDIR)/man1/extendedopacity.1.gz 
#	rm -f $(MANDIR)/man3/directory.3.gz
#	rm -f $(MANDIR)/man3/libnetpbm_dir.3.gz
#	# remove pointer man pages (that say, "The man page isn't here")
#	# which might have been installed previously
#	for f in $(MAN1); do rm -f $(MANDIR)/man1/$$f; done
#	for f in $(MAN3); do rm -f $(MANDIR)/man3/$$f; done
#	for f in $(MAN5); do rm -f $(MANDIR)/man5/$$f; done


.PHONY: clean
clean:
	  @rm -f *.[135] $(XML)
