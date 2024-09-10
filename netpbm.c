/* netpbm.c - merge of all the Netpbm programs

   Derived from pbmmerge.c, etc. by Bryan Henderson May 2002.  Copyright
   notice from pbmmerge.c, etc:
**
** Copyright (C) 1991 by Jef Poskanzer.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

/* Note: be careful using any Netpbm library functions in here, since
   we don't call pnm_init()
*/

#include <stdio.h>
#include <string.h>
#include "pam.h"

#define TRY(s,m) { \
    extern int m(int argc, char *argv[]); \
    if (strcmp(cp, s) == 0) exit(m(argc, argv)); \
}



int
main(int argc, char *argv[]) {

    const char * cp;

    if (strcmp(pm_arg0toprogname(argv[0]), "netpbm") == 0) {
        ++argv;
        --argc;
        if (argc < 1 || !*argv) {
            fprintf(stderr,
                    "When you invoke this program by the name 'netpbm', "
                    "You must supply at least one argument: the name of "
                    "the Netpbm program to run, e.g. "
                    "'netpbm pamfile /tmp/myfile.ppm'\n");
            exit(1);
                }
        }

    cp = pm_arg0toprogname(argv[0]);

    /* merge.h is an automatically generated file that generates code to
       match the string 'cp' against the name of every program that is part
       of this merge and, upon finding a match, invoke that program.
    */

/* The following inclusion is full of TRY macro invocations */

#include "mergetrylist"

    fprintf(stderr,"'%s' is an unknown Netpbm program name \n", cp );

    exit(1);
}



