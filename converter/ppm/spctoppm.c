/* spctoppm.c - read a compressed Spectrum file and produce a PPM image
**
** Copyright (C) 1991 by Steve Belczyk and Jef Poskanzer
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

#include "ppm.h"

#define ROWS 200
#define COLS 320
#define MAXVAL 7

static unsigned char screen[ROWS*COLS/2];
static short sscreen[ROWS*COLS/4];
static pixel pal[ROWS][48];



static void
doChar(unsigned int  const n,
       unsigned char const c) {

    unsigned int const i =
        160 + 2 * (n / 7960) + 8 * ((n % 7960) / 2) + (n % 2);
        /* Screen index */

    screen[i] = c;
}



static void
doRaster(FILE *       const ifP,
         unsigned int const rasterLength) {

    unsigned int i;
    unsigned int bytesProcessedCt;
        /* Number of bytes (compressed) from the input image processed
           so far.
        */
    unsigned int bytesStoredCt;
        /* Number of bytes (uncompressed) stored in the screen buffer
           so far.
        */

    pm_message("BRYAN: rasterLength=%u", rasterLength);
    /* Zero out first scan line. */
    for (i = 0; i < 160; ++i)
        screen[i] = 0;

    bytesProcessedCt = 0;  /* initial value */

    bytesStoredCt = 0;  /* initial value */

    while (bytesProcessedCt < rasterLength) {
        int const h = getc(ifP);
            /* next record header */

        ++bytesProcessedCt;

        if ((h >= 0) && (bytesProcessedCt < rasterLength)) {
            unsigned int i;

            for (i = 0; i <= h; ++i) {
                int const c = getc(ifP);

                ++bytesProcessedCt;
                doChar(bytesStoredCt, c);
                ++bytesStoredCt;
            }
        } else if ((h < 0) && (bytesProcessedCt < rasterLength)) {
            int const c = getc(ifP);
            unsigned int i;

            ++bytesProcessedCt;

            for (i = 0; i < (2 - h); ++i) {
                doChar(bytesStoredCt, c);
                ++bytesStoredCt;
            }
        }
    }

    /* Convert the char version of the screen to short. */
    for (i = 0; i < ROWS*COLS/4; ++i)
        sscreen[i] = (screen[i*2+0] << 8) + (screen[i*2+1] & 0xff);
    pm_message("BRYAN: done with reaster");
}



static void
doColormap(FILE * const ifP) {

    unsigned int i;

    /* Clear first three palettes. */
    for (i = 0; i < 48; ++i)
        PPM_ASSIGN(pal[0][i], 0, 0, 0);

    /* Read the palettes. */
    for (i = 1; i < ROWS; ++i) {
        unsigned int j;
        for (j = 0; j < 3; ++j) {
            short mask;
            unsigned int b;

            pm_readbigshort(ifP, &mask);

            for (b = 0; b < 15; ++b) {
                if (mask & (1 << b)) {
                    short k;
                    pm_readbigshort(ifP, &k);

                    PPM_ASSIGN(pal[i][(j*16)+b],
                        (k & 0x700) >> 8,
                        (k & 0x070) >> 4,
                        (k & 0x007));
                }
            }
        }
    }
}



static void
writePpm(FILE *         const ofP,
         unsigned int   const cols,
         unsigned int   const rows,
         pixval         const maxval) {

    pixel * pixelrow;
    unsigned int row;

    ppm_writeppminit(ofP, COLS, ROWS, (pixval) MAXVAL, 0);

    pixelrow = ppm_allocrow(COLS);

    for (row = 0; row < ROWS; ++row) {
        unsigned int col;

        for (col = 0; col < COLS; ++col) {
            int const ind = (80 * row) + ((col >> 4) << 2);
            int const b = 0x8000 >> (col & 0xf);

            unsigned int plane;
            unsigned int c;
            unsigned int x1;

            for (plane = 0, c = 0; plane < 4; ++plane) {
                if (b & sscreen[ind+plane])
                    c |= (1 << plane);
            }

            /* Compute palette index. */
            x1 = 10 * c;  /* initial value */
            if (c & 0x1)
                x1 -= 5;
            else
                ++x1;
            if ((col >= x1) && (col < (x1 + 160)))
                c += 16;
            if (col >= (x1 + 160))
                c += 32;

            /* Store the proper color. */
            pixelrow[col] = pal[row][c];
        }
        ppm_writeppmrow(ofP, pixelrow, COLS, MAXVAL, 0);
    }
}



int
main(int argc, const char ** argv) {

    FILE * ifP;
    int c1, c2;
    long int rasterLength;
        /* size of raster data in input file, in bytes */
    long int colormapLength;
        /* size of colormap data in input file in bytes */

    pm_proginit(&argc, argv);

    if (argc-1 < 1)
        ifP = stdin;
    else {
        if (argc-1 >= 1) {
            ifP = pm_openr(argv[1]);

            if (argc-1 > 1) {
                pm_error("Too many arguments.  "
                         "The only possible argument is the "
                         "input file name");
            }
        }
    }

    /* Check SPC file header. */
    c1 = getc(ifP);
    c2 = getc(ifP);

    if ((c1 != 'S') || (c2 != 'P')) {
        pm_error("not a Spectrum picture (first two bytes are not the "
                 "ASCII characters 'SP')");
    }

    /* Skip reserved bytes. */
    getc(ifP);
    getc(ifP);

    pm_readbiglong(ifP, &rasterLength);

    pm_readbiglong(ifP, &colormapLength);

    doRaster(ifP, rasterLength);

    doColormap(ifP);

    pm_close(ifP);

    writePpm(stdout, COLS, ROWS, MAXVAL);

    pm_close(stdout);

    return 0;
}

