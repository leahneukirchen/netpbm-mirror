/* ppmtoapplevol.c - read a portable pixmap and produce an Apple volume label
 *
 * Copyright 2011 Red Hat <mjg@redhat.com>
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose and without fee is hereby granted, provided
 * that the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation.  This software is provided "as is" without express or
 * implied warranty.
 */

#include "pm.h"
#include "ppm.h"



static unsigned char const map[] = {
    0x00, 0xf6, 0xf7, 0x2a, 0xf8, 0xf9, 0x55, 0xfa,
    0xfb, 0x80, 0xfc, 0xfd, 0xab, 0xfe, 0xff, 0xd6
};



static void
writeHeader(unsigned int const cols,
            FILE *       const ofP) {

    unsigned char header[5];

    header[0] = 0x01;
    header[1] = 0x00;
    header[2] = cols;
    header[3] = 0x00;
    header[4] = 0x0c;

    fwrite(header, sizeof(header), 1, ofP);
}



int
main (int argc, const char * argv[]) {

    const char * inputFilename;
    FILE * ifP;
    int rows, cols;
    pixval maxval;
    int format;
    pixel * pixelrow;
    unsigned int row;

    pm_proginit(&argc, argv);

    if (argc-1 > 1)
        pm_error("Too many arguments: %u.  There is at most one argument: "
                 "the input file name", argc-1);

    if (argc-1 >= 1)
        inputFilename = argv[1];
    else
        inputFilename = "-";

    ifP = pm_openr(inputFilename);

    ppm_readppminit(ifP, &cols, &rows, &maxval, &format);

    if (rows != 12)
        pm_error("Input image must be 12 rows tall.  Yours is %u", rows);

    writeHeader(cols, stdout);

    pixelrow = ppm_allocrow(cols);

    for (row = 0; row < rows; row++) {
        unsigned int col;
        
        ppm_readppmrow(stdin, pixelrow, cols, maxval, format);

        for (col = 0; col < cols; ++col) {
            unsigned int const maxval15Value =
                ((unsigned int)PPM_GETR(pixelrow[col]) * 15 + maxval/2) /
                maxval;
            unsigned char const appleValue =
                (unsigned char)map[15 - maxval15Value];
            fwrite(&appleValue, sizeof(appleValue), 1, stdout);
        }
    }

    ppm_freerow(pixelrow);

    pm_close(ifP);

    return 0;
}
