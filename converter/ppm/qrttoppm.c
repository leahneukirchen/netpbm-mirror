/* qrttoppm.c - read a QRT ray-tracer output file and produce a PPM
**
** Copyright (C) 1989 by Jef Poskanzer.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

#include "mallocvar.h"
#include "ppm.h"



int
main(int argc, const char ** argv) {

    FILE * ifP;
    pixel * pixelrow;
    unsigned int rows, cols;
    unsigned int row;
    pixval maxval;
    unsigned char * buf;

    pm_proginit(&argc, argv);

    if (argc-1 > 1)
        pm_error("Too many arguments (%u).  The only possible argument "
                 "is the input file name", argc-1);

    if (argc-1 >= 1)
        ifP = pm_openr(argv[1]);
    else
        ifP = stdin;

    /* Read in the QRT file.  First the header. */
    cols = (unsigned char)getc(ifP);
    cols += (unsigned char)getc(ifP) << 8;
    rows = (unsigned char)getc(ifP);
    rows += (unsigned char)getc(ifP) << 8;

    if (cols <= 0 || rows <= 0)
        pm_error("Invalid size: %u %u", cols, rows);

    maxval = 255;

    ppm_writeppminit(stdout, cols, rows, maxval, 0);

    pixelrow = ppm_allocrow(cols);

    MALLOCARRAY(buf, 3 * cols);

    if (!buf)
        pm_error("Failed to allocate buffer for %u columns", cols);

    for (row = 0; row < rows; ++row) {
        unsigned int col;

        getc(ifP); /* discard */
        getc(ifP); /* linenum */

        if (fread(buf, 3 * cols, 1, ifP) != 1)
            pm_error("EOF / read error");

        for (col = 0; col < cols; ++col) {
            PPM_ASSIGN(pixelrow[col],
                       buf[col], buf[cols + col], buf[2 * cols + col]);
        }
        ppm_writeppmrow(stdout, pixelrow, cols, maxval, 0);
    }

    free(buf);
    ppm_freerow(pixelrow);

    pm_close(ifP);
    pm_close(stdout);

    exit(0);
}



