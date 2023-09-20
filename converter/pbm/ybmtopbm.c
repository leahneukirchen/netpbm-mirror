/* ybmtopbm.c - read a file from Bennet Yee's 'xbm' program and write a pbm.
**
** Written by Jamie Zawinski based on code (C) 1988 by Jef Poskanzer.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

#include <limits.h>

#include "pm.h"
#include "pbm.h"
#include "bitreverse.h"

static short const ybmMagic = ( ( '!' << 8 ) | '!' );



static void
getinit(FILE *         const ifP,
        unsigned int * const colsP,
        unsigned int * const rowsP,
        int *          const depthP) {

    short int magic;
    short int cols, rows;
    int rc;

    rc = pm_readbigshort(ifP, &magic);
    if (rc == -1)
        pm_error("EOF / read error");
    else if (magic != ybmMagic)
        pm_error("bad magic number in YBM file");

    rc = pm_readbigshort(ifP, &cols);
    if (rc == -1 )
        pm_error("EOF / read error");
    else if (cols <= 0)
        pm_error("invalid width value in YBM file");

    rc = pm_readbigshort(ifP, &rows);
    if (rc == -1)
        pm_error("EOF / read error");
    else if (rows <= 0)
        pm_error("invalid height value in YBM file");

    *colsP = (unsigned int) cols;
    *rowsP = (unsigned int) rows;
    *depthP = 1;
}



int
main(int argc, const char * argv[]) {

    FILE * ifP;
    bit * bitrow;
    unsigned int rows, cols;
    unsigned int row;
    int depth;
    const char * inputFile;

    pm_proginit(&argc, argv);

    if (argc-1 < 1)
        inputFile = "-";
    else {
        inputFile = argv[1];

        if (argc-1 > 1)
            pm_error("Too many arguments.  The only argument is the optional "
                     "input file name");
    }

    ifP = pm_openr(inputFile);

    getinit(ifP, &cols, &rows, &depth);
    if (depth != 1)
        pm_error("YBM file has depth of %u, must be 1", (unsigned int) depth);
    if (cols > INT_MAX - 15)
        pm_error("YBM file has uncomputably large width %d", cols);


    pbm_writepbminit(stdout, cols, rows, 0);

    bitrow = pbm_allocrow_packed(cols + 8);

    for (row = 0; row < rows; ++row) {
        uint16_t *   const itemrow = (uint16_t *) bitrow;
        unsigned int const itemCt  = (cols + 15) / 16;

        unsigned int i;

        /* Get raster. */
        for (i = 0; i < itemCt; ++i) {
            short int item;
            pm_readbigshort(ifP, &item);
            itemrow[i] = (uint16_t) item;
        }

        for (i = 0; i < pbm_packed_bytes(cols); ++i)
            bitrow[i] = bitreverse[bitrow[i]];

        pbm_cleanrowend_packed(bitrow, cols);
        pbm_writepbmrow_packed(stdout, bitrow, cols, 0);
    }

    pbm_freerow_packed(bitrow);
    pm_close(ifP);
    pm_close(stdout);

    return 0;
}
