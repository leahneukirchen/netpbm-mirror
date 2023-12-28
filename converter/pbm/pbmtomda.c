/***************************************************************************

    PBMTOMDA: Convert PBM to Microdesign area
    Copyright (C) 1999,2004 John Elliott <jce@seasip.demon.co.uk>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

******************************************************************************/

#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pbm.h"
#include "mallocvar.h"
#include "shhopt.h"

struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFileNm;
    unsigned int dscale;
    unsigned int invert;
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
    OPTENT3(0,   "dscale", OPT_FLAG,  NULL, &cmdlineP->dscale,   0);
    OPTENT3(0,   "invert", OPT_FLAG,  NULL, &cmdlineP->invert,   0);

    opt.opt_table     = option_def;
    opt.short_allowed = false; /* We have no short (old-fashioned) options */
    opt.allowNegNum   = false; /* We have no parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others */

    if (argc-1 < 1)
        cmdlineP->inputFileNm = "-";
    else if (argc-1 == 1)
        cmdlineP->inputFileNm = argv[1];
    else
        pm_error("Program takes at most one argument:  input file name");

    free(option_def);
}



/* I'm being somewhat conservative in the PBM -> MDA translation. I output
 * only the MD2 format and don't allow RLE over the ends of lines.
 */

typedef unsigned char Mdbyte;

/* Encode 8 pixels as a byte */

static Mdbyte
encode(bit ** const bits,
       int    const row,
       int    const col) {

    int n;
    int mask;
    Mdbyte b;

    mask = 0x80;   /* initial value */
    b = 0;  /* initial value */

    for (n = 0; n < 8; n++) {
        if (bits[row][col+n] == PBM_BLACK) b |= mask;
        mask = mask >> 1;
    }
    return b;
}



static void
doTranslation(bit **       const bits,
              unsigned int const nOutCols,
              unsigned int const nOutRows,
              unsigned int const nInRows,
              bool         const mustInvert,
              bool         const mustScale) {
/*----------------------------------------------------------------------------
  Translate a pbm to MD2 format, one row at a time
-----------------------------------------------------------------------------*/
    unsigned int const step = mustScale ? 2 : 1;

    unsigned int row;
    Mdbyte * mdrow;  /* malloc'ed */

    MALLOCARRAY(mdrow, nOutCols);

    if (mdrow == NULL)
        pm_error("Unable to allocate memory for %u columns", nOutCols);

    for (row = 0; row < nOutRows; row += step) {
        unsigned int col;

        /* Encode image into non-compressed bitmap */
        for (col = 0; col < nOutCols; ++col) {
            Mdbyte b;

            if (row < nInRows)
                b = encode(bits, row, col * 8);
            else
                b = 0xff;  /* All black */

            mdrow[col] = mustInvert ? b : ~b;
        }

        /* Encoded. Now RLE it */
        for (col = 0; col < nOutCols; ) {
            Mdbyte const b = mdrow[col];

            if (b != 0xFF && b != 0) {
                /* Normal byte */
                putchar(b);
                ++col;
            } else {
                /* RLE a run of 0s or 0xFFs */

                unsigned int x1;

                for (x1 = col; x1 < nOutCols; ++x1) {
                    if (mdrow[x1] != b) break;
                    assert(x1 >= col);
                    if (x1 - col > 256) break;
                }
                assert(x1 >= col);
                x1 -= col;    /* x1 = no. of repeats */
                if (x1 == 256) x1 = 0;
                putchar(b);
                putchar(x1);
                col += x1;
            }
        }
    }
    free(mdrow);
}



int
main(int argc, const char ** argv) {

    const char * const headerValue = ".MDAMicroDesignPCWv1.00\r\npbm2mda\r\n";

    struct CmdlineInfo cmdline;
    FILE * ifP;
    unsigned int nOutRowsUnrounded;  /* Before rounding up to multiple of 4 */
    unsigned int nOutCols, nOutRows;
    int nInCols, nInRows;
    bit ** bits;
    Mdbyte header[128];
    int rc;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    /* Output v2-format MDA images. Simulate MDA header...
     * 2004-01-11: Hmm. Apparently some (but not all) MDA-reading
     * programs insist on the program identifier being exactly
     * 'MicroDesignPCW'. The spec does not make this clear. */
    memcpy(header + 0, headerValue, strlen(headerValue));
    memset(header + strlen(headerValue),
           0x00,
           sizeof(header)-strlen(headerValue));

    ifP = pm_openr(cmdline.inputFileNm);

    bits = pbm_readpbm(ifP, &nInCols, &nInRows);

    nOutRowsUnrounded = cmdline.dscale ? nInRows/2 : nInRows;

    nOutRows = ((nOutRowsUnrounded + 3) / 4) * 4;
        /* MDA wants rows a multiple of 4 */
    nOutCols = nInCols / 8;

    rc = fwrite(header, 1, 128, stdout);
    if (rc < 128)
        pm_error("Unable to write header to output file.  errno=%d (%s)",
                 errno, strerror(errno));

    pm_writelittleshort(stdout, nOutRows);
    pm_writelittleshort(stdout, nOutCols);

    doTranslation(bits, nOutCols, nOutRows, nInRows,
                  !!cmdline.invert, !!cmdline.dscale);

    pm_close(ifP);
    fflush(stdout);
    pbm_freearray(bits, nInRows);

    return 0;
}



