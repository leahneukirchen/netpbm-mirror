# This is the version of srcdir.mk that gets used when you build in
# the source tree.  In contrast, when you build in a separate build tree,
# you use a version of srcdir.mk that Configure created, which sets SRCDIR
# to the location of the source tree.

# This is designed to be included by the top level make file, with
# SRCDIR being passed to all the submakes.  That means that when you
# build in a separate build tree, you must build from the top; you
# can't go into a subdirectory and type "make".  When you build in the
# _source_ tree, local makes work because every local make file checks
# whether SRCDIR is set, and if it isn't, sets it with the assumption
# that the build directory and source directory are the same.

SRCDIR = $(CURDIR)
