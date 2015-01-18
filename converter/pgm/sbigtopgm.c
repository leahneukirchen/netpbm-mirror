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



struct SbigHeader {
/*----------------------------------------------------------------------------
   The information in an SBIG file header.
-----------------------------------------------------------------------------*/
    unsigned int rows;
    unsigned int cols;
    unsigned int maxval;
    bool isCompressed;
    bool haveCamera;
    char camera[80];
};



static void
readSbigHeader(FILE *              const ifP,
               struct SbigHeader * const sbigHeaderP) {

    size_t rc;
    bool gotCompression;
    bool gotWidth;
    bool gotHeight;
    char buffer[SBIG_HEADER_LENGTH];
    char * cursor;
    bool endOfHeader;

    rc = fread(buffer, SBIG_HEADER_LENGTH, 1, ifP);

    if (rc < 1)
        pm_error("error reading SBIG file header");

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

    gotCompression = false;   /* initial value */
    gotWidth = false;  /* initial value */
    gotHeight = false;  /* initial value */

    sbigHeaderP->maxval = 65535;  /* initial assumption */
    sbigHeaderP->haveCamera = false;  /* initial assumption */

    for (cursor = &buffer[0], endOfHeader = false; !endOfHeader;) {
        char * const cp = strchr(cursor, '\n');

        if (cp == NULL) {
            pm_error("malformed SBIG file header at character %u",
                     (unsigned)(cursor - &buffer[0]));
        }
        *cp = '\0';
        if (STRSEQ("ST-", cursor)) {
            char * const ep = strchr(cursor + 3, ' ');

            if (ep != NULL) {
                *ep = '\0';
                strcpy(sbigHeaderP->camera, cursor);
                sbigHeaderP->haveCamera = true;
                *ep = ' ';
            }
        }
        looseCanon(cursor);
        if (STRSEQ("st-", cursor)) {
            sbigHeaderP->isCompressed = (strstr("compressed", cursor) != NULL);
            gotCompression = true;
        } else if (STRSEQ("height=", cursor)) {
            sbigHeaderP->rows = atoi(cursor + 7);
            gotHeight = true;
        } else if (STRSEQ("width=", cursor)) {
            sbigHeaderP->cols = atoi(cursor + 6);
            gotWidth = true;
        } else if (STRSEQ("sat_level=", cursor)) {
            sbigHeaderP->maxval = atoi(cursor + 10);
        } else if (streq("end", cursor)) {
            endOfHeader = true;
        }
        cursor = cp + 1;
    }

    if (!gotCompression)
        pm_error("Required 'st-*' specification missing "
                 "from SBIG file header");
    if (!gotHeight)
        pm_error("required 'height=' specification missing"
                 "from SBIG file header");
    if (!gotWidth)
        pm_error("required 'width=' specification missing "
                 "from SBIG file header");
}



static void
writeRaster(FILE *            const ifP,
            struct SbigHeader const hdr,
            FILE *            const ofP) {

    gray * grayrow;
    unsigned int row;

    grayrow = pgm_allocrow(hdr.cols);

    for (row = 0; row < hdr.rows; ++row) {
        bool compthis;
        unsigned int col;

        if (hdr.isCompressed) {
            unsigned short rowlen;        /* Compressed row length */

            pm_readlittleshortu(ifP, &rowlen);
            
            /*	If compression results in a row length >= the uncompressed
                row length, that row is output uncompressed.  We detect this
                by observing that the compressed row length is equal to
                that of an uncompressed row.
            */

            if (rowlen == hdr.cols * 2)
                compthis = false;
            else
                compthis = hdr.isCompressed;
        } else
            compthis = hdr.isCompressed;

        for (col = 0; col < hdr.cols; ++col) {
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
        pgm_writepgmrow(ofP, grayrow, hdr.cols, hdr.maxval, 0);
    }

    pgm_freerow(grayrow);
}



int
main(int argc, const char ** argv) {

    FILE * ifP;
    struct CmdlineInfo cmdline;
    struct SbigHeader hdr;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFileName);

    readSbigHeader(ifP, &hdr);

    pm_message("SBIG %s %dx%d %s image, saturation level = %d",
               (hdr.haveCamera ? hdr.camera : "ST-?"),
               hdr.cols, hdr.rows,
               hdr.isCompressed ? "compressed" : "uncompressed",
               hdr.maxval);

    if (hdr.maxval > PGM_OVERALLMAXVAL) {
        pm_error("Saturation level (%u levels) is too large"
                 "This program's limit is %u.", hdr.maxval, PGM_OVERALLMAXVAL);
    }

    pgm_writepgminit(stdout, hdr.cols, hdr.rows, hdr.maxval, 0);

    writeRaster(ifP, hdr, stdout);

    pm_close(ifP);
    pm_close(stdout);

    return 0;
}
