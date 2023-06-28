/* pnmtosir.c - read a portable anymap and produce a Solitaire Image Recorder
**      file (MGI TYPE 11 or MGI TYPE 17)
**
** Copyright (C) 1991 by Marvin Landis
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

#include <stdbool.h>
#include "pnm.h"

#define MAXCOLORS 256



int
main(int argc, const char * argv[]) {
    
    FILE * ifP;
    xel ** xels;
    int rows, cols, format;
    unsigned int n;
    bool isGrayscale;
    xelval maxval;
    unsigned short Header[16];
    unsigned short LutHeader[16];
    unsigned short Lut[2048];

    pm_proginit(&argc, argv);

    if (argc-1 > 1)
        pm_error("There is only one possible argument: the input file.  "
                 "You specified %d", argc-1);

    if (argc-1 > 0) {
        const char * const inputFileName = argv[1];
        ifP = pm_openr(inputFileName);
    }  else {
        ifP = stdin;
    }
    
    xels = pnm_readpnm(ifP, &cols, &rows, &maxval, &format);
    
    /* Figure out the colormap. */
    switch (PNM_FORMAT_TYPE(format) ) {
    case PPM_TYPE:
        isGrayscale = false;
        pm_message("Writing a 24-bit SIR format (MGI TYPE 11)");
        break;

    case PGM_TYPE:
        isGrayscale = true;
        pm_message("Writing a grayscale SIR format (MGI TYPE 17)");
        break;

    default:
        isGrayscale = true;
        pm_message("Writing a monochrome SIR format (MGI TYPE 17)");
        break;
    }

    /* Set up the header. */
    Header[0] = 0x3a4f;
    Header[1] = 0;
    if (isGrayscale)
        Header[2] = 17;
    else
        Header[2] = 11;
    Header[3] = cols;
    Header[4] = rows;
    Header[5] = 0;
    Header[6] = 1;
    Header[7] = 6;
    Header[8] = 0;
    Header[9] = 0;
    for (n = 0; n < 10; n++)
        pm_writelittleshort(stdout,Header[n]);
    for (n = 10; n < 256; n++)
        pm_writelittleshort(stdout,0);

    /* Create color map */
    LutHeader[0] = 0x1524;
    LutHeader[1] = 0;
    LutHeader[2] = 5;
    LutHeader[3] = 256;
    LutHeader[4] = 256;
    for (n = 0; n < 5; ++n)
        pm_writelittleshort(stdout,LutHeader[n]);
    for (n = 5; n < 256; ++n)
        pm_writelittleshort(stdout,0);
 
    for (n = 0; n < 3; ++n) {
        unsigned int m;
        for (m = 0; m < 256; ++m)
            Lut[m * 4 + n] = m << 8;
    }
    for (n = 0; n < 1024; ++n)
        pm_writelittleshort(stdout,Lut[n]);
 
    /* Finally, write out the data. */
    switch (PNM_FORMAT_TYPE(format)) {
    case PPM_TYPE: {
        unsigned int row;
        for (row = 0; row < rows; ++row) {
            unsigned int col;
            for (col = 0; col < cols; ++col) {
                unsigned char const ub =
                    (char) (PPM_GETR(xels[row][col]) * (255 / maxval)); 
                fputc(ub, stdout);
            }
        }
        for (row = 0; row < rows; ++row) {
            unsigned int col;
            for (col = 0; col < cols; ++col) {  
                unsigned const char ub =
                    (char) (PPM_GETG(xels[row][col]) * (255 / maxval));
                fputc(ub, stdout);
            }
        }
        for (row = 0; row < rows; ++row) {
            unsigned int col;
            for (col = 0; col < cols; ++col) {  
                unsigned const char ub =
                    (char) (PPM_GETB(xels[row][col]) * (255 / maxval));
                fputc(ub, stdout);
            }
        }
    } break;

    default: {
        unsigned int row;
        for (row = 0; row < rows; ++row) {
            unsigned int col;
            for (col = 0; col < cols; ++col) {
                unsigned long const val = PNM_GET1(xels[row][col]);
                unsigned const char ub = (char) (val * (255 / maxval));
                fputc(ub, stdout);
            }
        }
    } break;
    }
    
    pm_close(ifP);

    return 0;
}


