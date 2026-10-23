/* gouldtoppm.c - read GOULD imaging system files and write a PPM
**
** Copyright (C) 1990  Stephen P. Lesniewski
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

#include <stdio.h>

#include "ppm.h"



static void
getGouldHeader(FILE *         ifP,
               unsigned int * colsP,
               unsigned int * lineCtP,
               unsigned int * bytesperlineP,
               unsigned int * bitsperpixelP,
               unsigned int * NBP) {

    unsigned int lineCtOld, lineCtNew;
    unsigned int bytesperlineOld, bytesperlineNew, bytesperline;
    unsigned int bitsperpixel;
    unsigned int headerRecCt;
    unsigned char headerblk[512];
    size_t bytesReadCt;

    bytesReadCt = fread(headerblk, 512, 1, ifP);
    if (bytesReadCt == 0)
        pm_error("Failed to read 512 byte gould header");

    lineCtOld       = (headerblk[3]<<8) | headerblk[2];
    bytesperlineOld = (headerblk[5]<<8) | headerblk[4];
    bitsperpixel    = (headerblk[7]<<8) | headerblk[6];
    headerRecCt     = (headerblk[9]<<8) | headerblk[8];
    lineCtNew       = (headerblk[15]<<24)| (headerblk[14]<<16) |
                      (headerblk[13]<<8) | (headerblk[12]);
    bytesperlineNew = (headerblk[19]<<24)| (headerblk[18]<<16) |
                      (headerblk[17]<<8) | (headerblk[16]);
    *NBP            = (headerblk[23]<<24)| (headerblk[22]<<16) |
                      (headerblk[21]<<8) | (headerblk[20]);

    if (headerRecCt > 1) {
        unsigned int i;
        for (i = 1 ; i < headerRecCt; ++i) {
            size_t bytesReadCt;
            bytesReadCt = fread(headerblk, 512, 1, ifP);
            if (bytesReadCt == 0)
                pm_error("failed to read second 512-byte gould header %u",
                         i);
        }
    }

    bytesperline = bytesperlineOld == 0 ? bytesperlineNew : bytesperlineOld;

    *lineCtP       = lineCtOld == 0 ? lineCtNew : lineCtOld;
    *bitsperpixelP = bitsperpixel;
    *bytesperlineP = bytesperline;
    *colsP         = bytesperline * 8 / bitsperpixel;
}



int
main(int argc, const char ** const argv) {

    pixval const maxval = 255;

    FILE * ifP;
    pixel * pixrow;

    unsigned int cols, lineCt, bytesperline, bitsperpixel, NB;
    unsigned char pbuffer[4];
    unsigned int row;

    int colorType;


    pm_proginit(&argc, argv);

    if (argc-1 > 1)
        pm_error("Too many arguments.  The only possible argument is the "
                 "input file name");

    if (argc-1 > 0)
        ifP = pm_openr(argv[1]);
    else
        ifP = stdin;

    getGouldHeader(ifP, &cols, &lineCt, &bytesperline, &bitsperpixel, &NB);

    ppm_writeppminit(stdout, cols, lineCt, maxval, 0);

    colorType = bitsperpixel == 0 ? NB : bitsperpixel / 8;

    pixrow = ppm_allocrow(cols);

    for (row = 0; row < lineCt; ++row) {
        unsigned int col;
        for (col = 0; col < cols; ++col) {
            switch (colorType) {
            case 0:
                pm_error("invalid color type 0 in Gould header");

            case 1:
                fread(pbuffer, 1, 1, ifP);
                PPM_ASSIGN(pixrow[col], pbuffer[0], pbuffer[0], pbuffer[0]);
                break;

            case 2:
                fread(pbuffer, 2, 1, ifP);
                PPM_ASSIGN(pixrow[col], pbuffer[0], pbuffer[1], pbuffer[1]);
                break;

            case 3:
                fread(pbuffer, 3, 1, ifP);
                PPM_ASSIGN(pixrow[col], pbuffer[0], pbuffer[1], pbuffer[2]);
                break;

            default :
                fread(pbuffer, 3, 1, ifP);
                PPM_ASSIGN(pixrow[col], pbuffer[0], pbuffer[1], pbuffer[2]);
                break;

            } /* switch */
        }
        ppm_writeppmrow(stdout, pixrow, cols, maxval, 0);
    }

    pm_close(ifP);
    pm_close(stdout);

    return 0;
}



