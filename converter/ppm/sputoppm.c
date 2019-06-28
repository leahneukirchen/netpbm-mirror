/* sputoppm.c - read an uncompressed Spectrum file and produce a PPM
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


typedef struct {
    pixel pal[ROWS][48];  /* Spectrum palettes, three per row */
} Pal;



static void
readPalettes(FILE * const ifP,
             Pal *  const palP) {

    unsigned int row;

    /* Clear the first palette line. */
    {
        unsigned int j;
        for (j = 0; j < 48; ++j)
            PPM_ASSIGN(palP->pal[0][j], 0, 0, 0);
    }
    /* Read the palettes. */
    for (row = 1; row < ROWS; ++row) {
        unsigned int j;
        for (j = 0; j < 48; ++j) {
            short k;
            pm_readbigshort(ifP, &k);
            PPM_ASSIGN(palP->pal[row][j],
                       (k & 0x700) >> 8,
                       (k & 0x070) >> 4,
                       (k & 0x007) >> 0);
        }
    }
}



int
main(int argc, const char ** argv) {

    FILE * ifP;
    unsigned int i;
    pixel * pixelrow;
    unsigned int row;
    Pal pal;
    short screen[ROWS*COLS/4];      /* simulates the Atari's video RAM */

    pm_proginit(&argc, argv);

    /* Check args. */
    if ( argc > 2 )
        pm_usage( "[spufile]" );

    if ( argc == 2 )
        ifP = pm_openr( argv[1] );
    else
        ifP = stdin;

    /* Read the SPU file */

    /* Read the screen data. */
    for (i = 0; i < ROWS*COLS/4; ++i)
        pm_readbigshort(ifP, &screen[i]);

    readPalettes(ifP, &pal);

    pm_close(ifP);

    /* Ok, get set for writing PPM. */
    ppm_writeppminit(stdout, COLS, ROWS, MAXVAL, 0);
    pixelrow = ppm_allocrow(COLS);

    /* Now do the conversion. */
    for (row = 0; row < ROWS; ++row) {
        unsigned int col;
        for (col = 0; col < COLS; ++col) {
            /* Compute pixel value. */
            unsigned int const ind = 80 * row + ((col >> 4) << 2);
            unsigned int const b = 0x8000 >> (col & 0xf);
            unsigned int c;
            unsigned int plane;
            unsigned int x1;

            c = 0;  /* initial value */
            for (plane = 0; plane < 4; ++plane) {
                if (b & screen[ind + plane])
                    c |= (1 << plane);
            }
            /* Compute palette index. */
            x1 = 10 * c;
            if ((c & 1) != 0)
                x1 -= 5;
            else
                ++x1;
            if ((col >= x1 ) && (col < (x1 + 160)))
                c += 16;
            if (col >= (x1 + 160))
                c += 32;

            /* Set the proper color. */
            pixelrow[col] = pal.pal[row][c];
        }
        ppm_writeppmrow(stdout, pixelrow, COLS, MAXVAL, 0);
    }

    ppm_freerow(pixelrow);
    pm_close(stdout);

    return 0;
}



