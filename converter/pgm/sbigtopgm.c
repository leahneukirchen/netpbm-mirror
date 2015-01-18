/*

    sbigtopgm.c - read a Santa Barbara Instruments Group CCDOPS file

    Note: All SBIG CCD astronomical cameras produce 14 bits or
	  (the ST-4 and ST-5) or 16 bits (ST-6 and later) per pixel.

		  Copyright (C) 1998 by John Walker
		       http://www.fourmilab.ch/

    If you find yourself having to add functionality included subsequent
    to the implementation of this program, you can probably find
    documentation of any changes to the SBIG file format on their
    Web site: http://www.sbig.com/

    Permission to use, copy, modify, and distribute this software and
    its documentation for any purpose and without fee is hereby
    granted, provided that the above copyright notice appear in all
    copies and that both that copyright notice and this permission
    notice appear in supporting documentation.	This software is
    provided "as is" without express or implied warranty.

*/

#include <string.h>

#include "pm_c_util.h"
#include "mallocvar.h"
#include "nstring.h"
#include "shhopt.h"
#include "pm.h"
#include "pgm.h"

struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFileName;
};



static void
parseCommandLine(int argc, const char ** argv,
                 struct CmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   Note that the file spec array we return is stored in the storage that
   was passed to as as the argv array.
-----------------------------------------------------------------------------*/
    optEntry * option_def;
        /* Instructions to pm_optParseOptions3 on how to parse our options.
         */
    optStruct3 opt;

    unsigned int option_def_index;

    MALLOCARRAY_NOFAIL(option_def, 100);
    
    option_def_index = 0;   /* incremented by OPTENT3 */

    opt.opt_table     = option_def;
    opt.short_allowed = FALSE; /* We have no short (old-fashioned) options */
    opt.allowNegNum   = FALSE; /* We have no parms that are negative numbers */
    
    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others */

    if (argc-1 < 0)
        cmdlineP->inputFileName = "-";
    else {
        cmdlineP->inputFileName = argv[1];

        if (argc-1 > 1)
            pm_error("Too many arguments.  The only possible argument is the "
                     "optional input file name");
    }
}



#define SBIG_HEADER_LENGTH  2048      /* File header length */



static void
looseCanon(char * const cpArg) {
/*----------------------------------------------------------------------------
  Canonicalize a line from the file header so items more sloppily formatted
  than those written by CCDOPS are still accepted.
-----------------------------------------------------------------------------*/
    char * cp;
    char * op;
    char c;

    cp = cpArg;  /* initial value */
    op = cpArg;  /* initial value */

    while ((c = *cp++) != 0) {
        if (!ISSPACE(c)) {
            if (ISUPPER(c))
                c = tolower(c);
            *op++ = c;
        }
    }
    *op++ = '\0';
}



int
main(int argc, const char ** argv) {

    FILE * ifP;
    gray * grayrow;
    unsigned int row;
    int maxval;
    int comp, rows, cols;
    char header[SBIG_HEADER_LENGTH];
    char * hdr;
    char camera[80];
    size_t rc;
    struct CmdlineInfo cmdline;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFileName);

    rc = fread(header, SBIG_HEADER_LENGTH, 1, ifP);

    if (rc < 1)
        pm_error("error reading SBIG file header");

    /*	Walk through the header and parse relevant parameters.	*/

    comp = -1;
    cols = -1;
    rows = -1;

    /*	The SBIG header specification equivalent to maxval is
        "Sat_level", the saturation level of the image.  This
        specification is optional, and was not included in files
        written by early versions of CCDOPS. It was introduced when it
        became necessary to distinguish 14-bit images with a Sat_level
        of 16383 from 16-bit images which saturate at 65535.  In
        addition, co-adding images or capturing with Track and
        Accumulate can increase the saturation level.  Since files
        which don't have a Sat_level line in the header were most
        probably written by early drivers for the ST-4 or ST-5, it
        might seem reasonable to make the default for maxval 16383,
        the correct value for those cameras.  I chose instead to use
        65535 as the default because the overwhelming majority of
        cameras in use today are 16 bit, and it's possible some
        non-SBIG software may omit the "optional" Sat_level
        specification.	Also, no harm is done if a larger maxval is
        specified than appears in the image--a simple contrast stretch
        will adjust pixels to use the full 0 to maxval range.  The
        converse, pixels having values greater than maxval, results in
        an invalid file which may cause problems in programs which
        attempt to process it.
	*/

    maxval = 65535;

    hdr = header;

    strcpy(camera, "ST-?");  /* initial value */

    for (;;) {
        char *cp = strchr(hdr, '\n');

        if (cp == NULL) {
            pm_error("malformed SBIG file header at character %u",
                     (unsigned)(hdr - header));
        }
        *cp = '\0';
        if (strncmp(hdr, "ST-", 3) == 0) {
            char * const ep = strchr(hdr + 3, ' ');

            if (ep != NULL) {
                *ep = '\0';
                strcpy(camera, hdr);
                *ep = ' ';
            }
        }
        looseCanon(hdr);
        if (STRSEQ(hdr, "st-")) {
            comp = strstr(hdr, "compressed") != NULL;
        } else if (STRSEQ(hdr, "height=")) {
            rows = atoi(hdr + 7);
        } else if (STRSEQ(hdr, "width=")) {
            cols = atoi(hdr + 6);
        } else if (STRSEQ(hdr, "sat_level=")) {
            maxval = atoi(hdr + 10);
        } else if (streq(hdr, "end")) {
            break;
        }
        hdr = cp + 1;
    }

    if (comp == -1 || rows == -1 || cols == -1)
        pm_error("required specification missing from SBIG file header");

    pm_message("SBIG %s %dx%d %s image, saturation level = %d",
               camera, cols, rows, comp ? "compressed" : "uncompressed",
               maxval);

    if (maxval > PGM_OVERALLMAXVAL) {
        pm_error("Saturation level (%d levels) is too large"
                 "This program's limit is %d.", maxval, PGM_OVERALLMAXVAL);
    }

    pgm_writepgminit(stdout, cols, rows, maxval, 0);
    grayrow = pgm_allocrow(cols);

    for (row = 0; row < rows; ++row) {
        bool compthis;
        unsigned int col;

        if (comp) {
            unsigned short rowlen;        /* Compressed row length */

            pm_readlittleshortu(ifP, &rowlen);
            
            /*	If compression results in a row length >= the uncompressed
                row length, that row is output uncompressed.  We detect this
                by observing that the compressed row length is equal to
                that of an uncompressed row.
            */

            if (rowlen == cols * 2)
                compthis = false;
            else
                compthis = comp;
        } else
            compthis = comp;

        for (col = 0; col < cols; ++col) {
            unsigned short g;

            if (compthis) {
                if (col == 0) {
                    pm_readlittleshortu(ifP, &g);
                } else {
                    int const delta = getc(ifP);

                    if (delta == 0x80)
                        pm_readlittleshortu(ifP, &g);
                    else
                        g += ((signed char) delta);
                }
            } else
                pm_readlittleshortu(ifP, &g);
            grayrow[col] = g;
        }
        pgm_writepgmrow(stdout, grayrow, cols, maxval, 0);
    }
    pm_close(ifP);
    pm_close(stdout);

    return 0;
}
