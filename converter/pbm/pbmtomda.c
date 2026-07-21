/*=============================================================================
                                pbmtomda
===============================================================================
    Convert a PBM image to Microdesign area.

    See the file mdaspec.txt for a specification of the MDA format.

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
=============================================================================*/

#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pm_c_util.h"
#include "mallocvar.h"
#include "shhopt.h"
#include "pbm.h"



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
    optStruct3 opt;

    unsigned int option_def_index;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0,   "dscale", OPT_FLAG,  NULL, &cmdlineP->dscale,   0);
    OPTENT3(0,   "invert", OPT_FLAG,  NULL, &cmdlineP->invert,   0);

    opt.opt_table     = option_def;
    opt.short_allowed = false; /* We have no short (old-fashioned) options */
    opt.allowNegNum   = false; /* We have no parms that are negative numbers */

    pm_optParseOptions4(&argc, argv, opt, sizeof(opt), 0);
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


static Mdbyte
encode(const bit *  const bitrow,
       unsigned int const col) {
/*----------------------------------------------------------------------------
   One-byte encoding of the 8 pixels in 'bitrow' starting at Column 'col'.
-----------------------------------------------------------------------------*/
    unsigned int n;
    unsigned int mask;
    Mdbyte b;

    for (n = 0, mask = 0x80, b = 0; n < 8; ++n) {
        if (bitrow[col+n] == PBM_BLACK)
            b |= mask;
        mask = mask >> 1;
    }
    return b;
}



static void
makePadRow(unsigned int const cols,
           bool         const mustInvert,
           Mdbyte *     const mdrow) {

    Mdbyte const padByte = mustInvert ? 0xff : 0x00;

    unsigned int byteIdx;

    for (byteIdx = 0; byteIdx < cols/8; ++byteIdx)
        mdrow[byteIdx] = padByte;
}



static void
encodeRowIntoNonCompressedBitmap(const bit *  const bitrow,
                                 unsigned int const cols,
                                 bool         const mustInvert,
                                 Mdbyte *     const mdrow) {

    unsigned int byteIdx;

    for (byteIdx = 0; byteIdx < cols/8; ++byteIdx) {
        Mdbyte const b = encode(bitrow, byteIdx * 8);

        mdrow[byteIdx] = mustInvert ? b : ~b;
    }
}



static void
rleCompressRow(const Mdbyte * const mdrow,
               unsigned int   const rowByteCt,
               FILE *         const ofP) {

    unsigned int i;

    for (i = 0; i < rowByteCt; ) {
        Mdbyte const b = mdrow[i];

        if (b != 0xFF && b != 0) {
            /* Normal byte */
            putchar(b);
            ++i;
        } else {
            /* RLE a run of 0s or 0xFFs */

            unsigned int x1;

            for (x1 = i; x1 < rowByteCt; ++x1) {
                if (mdrow[x1] != b) break;
                assert(x1 >= i);
                if (x1 - i > 256) break;
            }
            assert(x1 >= i);
            x1 -= i;    /* x1 = no. of repeats */
            if (x1 == 256) x1 = 0;
            putc(b, ofP);
            putc(x1, ofP);
            i += x1;
        }
    }
}



static void
writeRaster(bit **       const bits,
            unsigned int const cols,
            unsigned int const outRowCt,
            unsigned int const inRowCt,
            bool         const mustInvert,
            bool         const mustScale,
            FILE *       const ofP) {
/*----------------------------------------------------------------------------
  Write the raster in MD2 format to *ofP.

  'bits' is the raster.  It is 'inRowCt' by 'cols'.

  'outRowCt' is the number of rows for the MD2 output to have; we pad on
  the bottom as necessary.

  'cols' is the number of columns in the raster.  Note that 'bits' has one
  byte per pixel, while MD2 has 8 pixels per byte.

  'mustScale' means to take only every other row of 'bits' ('outRowCt'
  reflects the reduction in number of MD2 rows).
-----------------------------------------------------------------------------*/
    unsigned int const step = mustScale ? 2 : 1;
    unsigned int const rowByteCt = cols/8;
        /* Number of bytes in an MD2 row, before compression */

    unsigned int outrow;
    Mdbyte * mdrow;  /* malloc'ed */

    MALLOCARRAY(mdrow, rowByteCt);

    if (mdrow == NULL)
        pm_error("Unable to allocate memory for %u columns", cols);

    for (outrow = 0; outrow < outRowCt; ++outrow) {
        unsigned int const inrow = outrow * step;

        if (inrow >= inRowCt)
            makePadRow(cols, mustInvert, mdrow);
        else
            encodeRowIntoNonCompressedBitmap(
                bits[inrow], cols, mustInvert, mdrow);

        rleCompressRow(mdrow, rowByteCt, ofP);
    }
    free(mdrow);
}



static void
warnPadding(unsigned int const outRowCtUnrounded,
            unsigned int const outRowCt) {

    if (outRowCt != outRowCtUnrounded) {
        pm_message("Adding %u rows of padding to bottom to reach a multiple "
                   "of 4 rows as required by the MDA format",
                   outRowCt - outRowCtUnrounded);
    }
}



static void
warnTruncating(unsigned int const cols) {

    if (ROUNDDN(cols, 8) != cols) {
        pm_message("Truncating %u columns on right to reach a multiple "
                   "of 8 columns as required by the MDA format",
                   cols - ROUNDDN(cols, 8));

        if (ROUNDDN(cols, 8) == 0) {
            pm_message("Resulting image has zero width, "
                       "which may be unusable");
        }
    }
}



static void
writeMdaHeader(unsigned int const rows,
               unsigned int const cols,
               FILE *       const ofP) {

    const char * const headerValue = ".MDAMicroDesignPCWv1.00\r\npbm2mda\r\n";

    Mdbyte header[128];
    int rc;

    /* Output v2-format MDA images. Simulate MDA header...
     * 2004-01-11: Hmm. Apparently some (but not all) MDA-reading
     * programs insist on the program identifier being exactly
     * 'MicroDesignPCW'. The spec does not make this clear. */
    memcpy(header + 0, headerValue, strlen(headerValue));
    memset(header + strlen(headerValue),
           0x00,
           sizeof(header)-strlen(headerValue));

    rc = fwrite(header, 1, sizeof(header), stdout);
    if (rc < sizeof(header))
        pm_error("Unable to write header to output file.  errno=%d (%s)",
                 errno, strerror(errno));

    pm_writelittleshort(stdout, rows);
    pm_writelittleshort(stdout, cols/8);
}



int
main(int argc, const char ** argv) {

    struct CmdlineInfo cmdline;
    FILE * ifP;
    unsigned int outRowCtUnrounded;  /* Before rounding up to multiple of 4 */
    unsigned int outRowCt;
    int inRowCt;
    int cols;
    bit ** bits;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFileNm);

    bits = pbm_readpbm(ifP, &cols, &inRowCt);

    outRowCtUnrounded = cmdline.dscale ? inRowCt/2 : inRowCt;

    outRowCt = ROUNDUP(outRowCtUnrounded, 4);
        /* MDA wants rows a multiple of 4 */

    warnPadding(outRowCtUnrounded, outRowCt);
    warnTruncating(cols);

    writeMdaHeader(outRowCt, cols, stdout);

    writeRaster(bits, cols, outRowCt, inRowCt,
                !!cmdline.invert, !!cmdline.dscale, stdout);

    pm_close(ifP);
    fflush(stdout);
    pbm_freearray(bits, inRowCt);

    return 0;
}



