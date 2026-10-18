/***************************************************************************

    MDATOPBM: Convert Microdesign area to portable bitmap
    Copyright (C) 1999 John Elliott <jce@seasip.demon.co.uk>

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

    See the file mdaspec.txt for a specification of the MDA format.
******************************************************************************/
#include <stdbool.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>

#include "mallocvar.h"
#include "shhopt.h"
#include "pbm.h"


struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFileName;
    unsigned int doubleheight;
    unsigned int invert;
    unsigned int ascii;
};



static void
parseCommandLine(int argc, const char ** argv,
                 struct CmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   Note that the file spec array we return is stored in the storage that
   was passed to as as the argv array.
-----------------------------------------------------------------------------*/
    optEntry * option_def;
        /* Instructions to pm_optParseOptions4 on how to parse our options.
         */
    optStruct3 opt;

    unsigned int option_def_index;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0,   "doubleheight", OPT_FLAG,  NULL, &cmdlineP->doubleheight,  0);
    OPTENT3(0,   "D",            OPT_FLAG,  NULL, &cmdlineP->doubleheight,  0);
    OPTENT3(0,   "invert",       OPT_FLAG,  NULL, &cmdlineP->invert,        0);
    OPTENT3(0,   "I",            OPT_FLAG,  NULL, &cmdlineP->invert,        0);
    OPTENT3(0,   "ascii",        OPT_FLAG,  NULL, &cmdlineP->ascii,         0);
    OPTENT3(0,   "A",            OPT_FLAG,  NULL, &cmdlineP->ascii,         0);

    opt.opt_table     = option_def;
    opt.short_allowed = false; /* We have no short (old-fashioned) options */
    opt.allowNegNum   = false; /* We have no parms that are negative numbers */

    pm_optParseOptions4(&argc, argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others */

    if (argc-1 < 1)
        cmdlineP->inputFileName = "-";
    else {
        cmdlineP->inputFileName = argv[1];

        if (argc-1 > 1) {
            pm_error("Too many arguments.  The only possible non-option "
                     "argument is the input file name");
        }
    }

    free(option_def);
}



typedef unsigned char mdbyte;   /* Must be exactly one byte */

static mdbyte header[128];      /* MDA file header */
static bit **data;          /* PBM image */
static mdbyte *mdrow;           /* MDA row after decompression (MD3 only) */

static mdbyte
getbyte(FILE * const ifP) {
    /* Read a byte from the input stream, with error trapping */
    int b;

    b = fgetc(ifP);

    if (b == EOF)
        pm_error("Unexpected end of MDA file");

    return (mdbyte)b;
}



static void
renderByte(unsigned int   const nInCols,
           unsigned int   const nOutRows,
           bool           const doubleheight,
           unsigned int * const colP,
           unsigned int * const xP,
           unsigned int * const yP,
           int            const b,
           bool           const invert) {
/*----------------------------------------------------------------------------
  Convert a byte to 8 cells in the destination bitmap

  As input

    *colP = source column
    *xP   = destination column
    *yP   = destination row
    b     = byte to draw

  As output, update *colP, *xP and *yP to point to the next bit of the raster.
-----------------------------------------------------------------------------*/
    int const y3 =  doubleheight ? *yP * 2 : *yP;

    assert(*colP < nInCols);

    if (y3 < nOutRows) {
        unsigned int n;
        unsigned int mask;

        for (n = 0, mask = 0x80; n < 8; ++n) {
            data[y3][*xP] = invert ?
                ((b & mask) ? PBM_BLACK : PBM_WHITE) :
                ((b & mask) ? PBM_WHITE : PBM_BLACK);
            mask >>= 1;
            if (doubleheight)
                data[y3+1][*xP] = data[y3][*xP];
            ++(*xP);
        }
        ++(*colP);       /* Next byte */
        if ((*colP) >= nInCols) {
            /* Onto next line? */
            *colP = 0;
            *xP = 0;
            ++(*yP);
        }
    }
}



static void
md2Trans(FILE *       const ifP,
         unsigned int const nInRows,
         unsigned int const nInCols,
         unsigned int const nOutRows,
         bool         const doubleheight,
         unsigned int const nOutCols,
         bool         const invert) {
/*----------------------------------------------------------------------------
   Convert a MicroDesign 2 area to PBM

   MD2 has RLE encoding that may go over

   *ifP must be positioned to the MDA raster, which the MDA header says is
   'nInRows' by 'nInCols'.

   'nInCols' must be at least 1.
-----------------------------------------------------------------------------*/
    unsigned int x1, y1, col;    /* multiple lines. */
    mdbyte b;

    assert(nInCols > 0);  /* calling requirement */

    x1 = y1 = col = 0;

    while (y1 < nInRows) {
        b = getbyte(ifP);

        if (b == 0 || b == 0xFF) {
            /* RLE sequence */
            int c;
            c = getbyte(ifP);
            if (c == 0)
                c = 256;
            while (c > 0) {
                renderByte(nInCols, nOutRows, doubleheight, &col, &x1, &y1,
                           b, invert);
                --c;
            }
        } else
            /* Not RLE */
            renderByte(nInCols, nOutRows, doubleheight, &col, &x1, &y1,
                       b, invert);
    }
}



static void
md3Trans(FILE *       const ifP,
         unsigned int const nInRows,
         unsigned int const nInCols,
         unsigned int const nOutRows,
         bool         const doubleheight,
         unsigned int const nOutCols,
         bool         const invert) {
/*----------------------------------------------------------------------------
   Convert MD3 file. MD3 are encoded as rows, and there are three types.

   Arguments are like 'md2Trans'.
-----------------------------------------------------------------------------*/
    unsigned int y1;

    assert(nInCols > 0);  /* calling requirement */

    for (y1 = 0; y1 < nInRows; ++y1) {
        mdbyte b;

        b = getbyte(ifP);   /* Row type */
        switch(b)  {
        case 0: {  /* All the same byte */
            int c;
            unsigned int i;
            c = getbyte(ifP);
            for (i = 0; i < nInCols; ++i)
                mdrow[i] = c;
        } break;

        case 1:      /* Encoded data */
        case 2: {     /* Encoded as XOR with previous row */
            unsigned int col;
            col = 0;
            while (col < nInCols) {
                int c;
                c = getbyte(ifP);
                if (c >= 129) {
                    /* RLE sequence */
                    unsigned int i;
                    int d;
                    c = 257 - c;
                    d = getbyte(ifP);
                    for (i = 0; i < c; ++i) {
                        if (b == 1)
                            mdrow[col++] = d;
                        else
                            mdrow[col++] ^= d;
                    }
                } else {
                    /* not RLE sequence */
                    unsigned int i;
                    ++c;
                    for (i = 0; i < c; ++i) {
                        int d;
                        d = getbyte(ifP);
                        if (b == 1)
                            mdrow[col++] = d;
                        else
                            mdrow[col++] ^= d;
                    }
                }
            }
        } break;
        }
        {
            /* Row loaded. Convert it. */
            unsigned int x1;
            unsigned int col;
            unsigned int i;

            for (i = 0, x1 = 0, col = 0; i < nInCols; ++i) {
                unsigned int d;
                d = y1;
                renderByte(nInCols, nOutRows, doubleheight,
                           &col, &x1, &d, mdrow[i], invert);
            }
        }
    }
}




int
main(int argc, const char **argv) {

    struct CmdlineInfo cmdline;
    FILE * ifP;
    unsigned int nInRows, nInCols;
        /* Height, width of input (rows x bytes) */
    unsigned int nOutCols, nOutRows;
        /* Height, width of output (rows x bytes) */

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFileName);

    /* Read MDA file header */

    if (fread(header, 1, 128, ifP) < 128)
        pm_error("Not a .MDA file");

    if (strncmp((char*) header, ".MDA", 4) &&
        strncmp((char*) header, ".MDP", 4))
        pm_error("Not a .MDA file");

    {
        short yy;
        pm_readlittleshort(ifP, &yy); nInRows = (unsigned short)yy;
        pm_readlittleshort(ifP, &yy); nInCols = (unsigned short)yy;
    }

    nOutCols = 8 * nInCols;

    nOutRows = cmdline.doubleheight ? nInRows * 2 : nInRows;

    data = pbm_allocarray(nOutCols, nOutRows);

    MALLOCARRAY_NOFAIL(mdrow, nInCols);

    if (nInCols < 1)
        pm_error("Input image is zero width; we cannot handle that");

    if (header[21] == '0')
        md2Trans(ifP, nInRows, nInCols,
                 nOutRows, !!cmdline.doubleheight, nOutCols, !!cmdline.invert);
    else
        md3Trans(ifP, nInRows, nInCols,
                 nOutRows, !!cmdline.doubleheight, nOutCols, !!cmdline.invert);

    pbm_writepbm(stdout, data, nOutCols, nOutRows, cmdline.ascii);

    if (ifP != stdin)
        pm_close(ifP);
    fflush(stdout);
    pbm_freearray(data, nOutRows);
    free(mdrow);

    return 0;
}



