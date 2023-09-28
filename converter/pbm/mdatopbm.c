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

#include <string.h>
#include <stdio.h>
#include "pbm.h"
#include "mallocvar.h"

/* Simple MDA -> portable bitmap converter */

typedef unsigned char mdbyte;   /* Must be exactly one byte */

static mdbyte header[128];      /* MDA file header */
static bit **data;          /* PBM image */
static mdbyte *mdrow;           /* MDA row after decompression (MD3 only) */
static int bInvert = 0;     /* Invert image? */
static int bScale  = 0;     /* Scale image? */
static int bAscii  = 0;     /* Output ASCII PBM? */

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
           unsigned int * const colP,
           unsigned int * const xP,
           unsigned int * const yP,
           int            const b) {
/*----------------------------------------------------------------------------
  Convert a byte to 8 cells in the destination bitmap

  As input

    *colP = source column
    *xP  = destination column
    *yP  = destination row
    b    = byte to draw

  As output, update *colP, *xP and *yP to point to the next bit of the row.
-----------------------------------------------------------------------------*/
    int const y3 =  bScale ? *yP * 2 : *yP;

    if (y3 < nOutRows) {
        unsigned int n;
        int mask;

        for (n = 0, mask = 0x80; n < 8; ++n) {
            if (bInvert) data[y3][*xP] = (b & mask) ? PBM_BLACK : PBM_WHITE;
            else         data[y3][*xP] = (b & mask) ? PBM_WHITE : PBM_BLACK;
            mask = mask >> 1;
            if (bScale)
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
         unsigned int const nOutCols) {
/*----------------------------------------------------------------------------
   Convert a MicroDesign 2 area to PBM

   MD2 has RLE encoding that may go over
-----------------------------------------------------------------------------*/
    unsigned int x1, y1, col;    /* multiple lines. */
    mdbyte b;

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
                renderByte(nInCols, nOutRows, &col, &x1, &y1, b);
                --c;
            }
        } else
            /* Not RLE */
            renderByte(nInCols, nOutRows, &col, &x1, &y1, b);
    }
}



static void
md3Trans(FILE *       const ifP,
         unsigned int const nInRows,
         unsigned int const nInCols,
         unsigned int const nOutRows,
         unsigned int const nOutCols) {
/*----------------------------------------------------------------------------
   Convert MD3 file. MD3 are encoded as rows, and there are three types.
-----------------------------------------------------------------------------*/
    unsigned int y1;

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
                renderByte(nInCols, nOutRows, &col, &x1, &d, mdrow[i]);
            }
        }
    }
}



static void
usage(const char *s) {
    printf("mdatopbm v1.00, Copyright (C) 1999 "
           "John Elliott <jce@seasip.demon.co.uk>\n"
           "This program is redistributable under the terms of "
           "the GNU General Public\n"
           "License, version 2 or later.\n\n"
           "Usage: %s [ -a ] [ -d ] [ -i ] [ -- ] [ infile ]\n\n"
           "-a: Output an ASCII pbm file\n"
           "-d: Double height (to compensate for the PCW aspect ratio)\n"
           "-i: Invert colors\n"
           "--: No more options (use if filename begins with a dash)\n",
           s);

    exit(0);
}



int
main(int argc, const char **argv) {

    FILE * ifP;
    int n, optstop = 0;
    const char * fname;
    unsigned int nInRows, nInCols;
        /* Height, width of input (rows x bytes) */
    unsigned int nOutCols, nOutRows;
        /* Height, width of output (rows x bytes) */

    pm_proginit(&argc, argv);

    /* Parse options */

    fname = NULL;  /* initial value */
    for (n = 1; n < argc; ++n) {
        if (argv[n][0] == '-' && !optstop) {
            if (argv[n][1] == 'a' || argv[n][1] == 'A') bAscii = 1;
            if (argv[n][1] == 'd' || argv[n][1] == 'D') bScale = 1;
            if (argv[n][1] == 'i' || argv[n][1] == 'I') bInvert = 1;
            if (argv[n][1] == 'h' || argv[n][1] == 'H') usage(argv[0]);
            if (argv[n][1] == '-' && argv[n][2] == 0 && !fname) {
                /* "--" */
                optstop = 1;
            }
            if (argv[n][1] == '-' && (argv[n][2] == 'h' || argv[n][2] == 'H'))
                usage(argv[0]);
        }
        else if (argv[n][0] && !fname) {
            /* Filename */
            fname = argv[n];
        }
    }

    if (fname)
        ifP = pm_openr(fname);
    else
        ifP = stdin;

    /* Read MDA file header */

    if (fread(header, 1, 128, ifP) < 128)
        pm_error("Not a .MDA file\n");

    if (strncmp((char*) header, ".MDA", 4) &&
        strncmp((char*) header, ".MDP", 4))
        pm_error("Not a .MDA file\n");

    {
        short yy;
        pm_readlittleshort(ifP, &yy); nInRows = yy;
        pm_readlittleshort(ifP, &yy); nInCols = yy;
    }

    nOutCols = 8 * nInCols;
    nOutRows = nInRows;
    if (bScale)
        nOutRows *= 2;

    data = pbm_allocarray(nOutCols, nOutRows);

    MALLOCARRAY_NOFAIL(mdrow, nInCols);

    if (header[21] == '0')
        md2Trans(ifP, nInRows, nInCols, nOutRows, nOutCols);
    else
        md3Trans(ifP, nInRows, nInCols, nOutRows, nOutCols);

    pbm_writepbm(stdout, data, nOutCols, nOutRows, bAscii);

    if (ifP != stdin)
        pm_close(ifP);
    fflush(stdout);
    pbm_freearray(data, nOutRows);
    free(mdrow);

    return 0;
}



