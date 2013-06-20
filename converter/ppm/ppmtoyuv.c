/* ppmtoyuv.c - convert a portable pixmap into an Abekas YUV file
**
** by Marc Boucher
** Internet: marc@PostImage.COM
** 
** Based on Example Conversion Program, A60/A64 Digital Video Interface
** Manual, page 69.
**
** Copyright (C) 1991 by DHD PostImage Inc.
** Copyright (C) 1987 by Abekas Video Systems Inc.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

#include "ppm.h"



static void
convertRow(const pixel *   const pixelrow,
           unsigned int    const cols,
           unsigned char * const yuvBuf,
           unsigned long * const uP,
           unsigned long * const vP,
           unsigned long * const u0P,
           unsigned long * const v0P,
           unsigned long * const y2CarryP) {

    unsigned int col;
    unsigned char * yuvptr;

    for (col = 0, yuvptr = &yuvBuf[0]; col < cols; col += 2) {
        unsigned long y1, y2, u1, u2, v1, v2;

        {
            /* first pixel gives Y and 0.5 of chroma */
            pixval const r = PPM_GETR(pixelrow[col]);
            pixval const g = PPM_GETG(pixelrow[col]);
            pixval const b = PPM_GETB(pixelrow[col]);
            
            y1 = 16829 * r + 33039 * g +  6416 * b + (*y2CarryP & 0xffff);
            u1 = -4853 * r -  9530 * g + 14383 * b;
            v1 = 14386 * r - 12046 * g -  2340 * b;
        }
        {
            /* second pixel gives Y and 0.25 of chroma */
            pixval const r = PPM_GETR(pixelrow[col + 1]);
            pixval const g = PPM_GETG(pixelrow[col + 1]);
            pixval const b = PPM_GETB(pixelrow[col + 1]);

            y2 = 16829 * r + 33039 * g + 6416 * b + (y1 & 0xffff);
            u2 = -2426 * r -  4765 * g + 7191 * b;
            v2 =  7193 * r -  6023 * g - 1170 * b;
        }
        /* filter the chroma */
        *uP = *u0P + u1 + u2 + (*uP & 0xffff);
        *vP = *v0P + v1 + v2 + (*vP & 0xffff);

        *u0P = u2;
        *v0P = v2;

        *yuvptr++ = (*uP >> 16) + 128;
        *yuvptr++ = (y1  >> 16) +  16;
        *yuvptr++ = (*vP >> 16) + 128;
        *yuvptr++ = (y2  >> 16) +  16;

        *y2CarryP = y2;
    }
}



int
main(int argc, const char **argv) {

    FILE * ifP;
    pixel * pixelrow;
    int rows, cols, format;
    pixval maxval;
    unsigned int row;
    unsigned char  * yuvBuf;
    unsigned long u, v, u0, v0, y2Carry;

    pm_proginit(&argc, argv);

    if (argc-1 > 1)
        pm_error("Too many arguments: %u.  The only possible argument "
                 "is the name of the input file", argc-1);

    if (argc-1 == 1)
        ifP = pm_openr(argv[1]);
    else
        ifP = stdin;

    ppm_readppminit(ifP, &cols, &rows, &maxval, &format);

    if (cols % 2 != 0)
        pm_error("Image must have even number of columns.\n"
                 "This image is %u columns wide.  Try Pamcut.", cols);

    pixelrow = ppm_allocrow(cols);
    yuvBuf = (unsigned char *) pm_allocrow(cols, 2);

    for (row = 0, u = v = u0 = v0 = y2Carry = 0; row < rows; ++row) {
        ppm_readppmrow(ifP, pixelrow, cols, maxval, format);

        convertRow(pixelrow, cols, yuvBuf, &u, &v, &u0, &v0, &y2Carry);
        
        fwrite(yuvBuf, cols*2, 1, stdout);
    }

    pm_close(ifP);

    return 0;
}
