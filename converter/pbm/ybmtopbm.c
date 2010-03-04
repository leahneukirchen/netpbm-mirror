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

#include "pm.h"
#include "pbm.h"

static short const ybmMagic = ( ( '!' << 8 ) | '!' );




static int item;
static int bitsperitem, bitshift;



static void
getinit(FILE *  const ifP,
        short * const colsP,
        short * const rowsP,
        short * const depthP,
        short * const padrightP) {

    short magic;
    int rc;

    rc = pm_readbigshort(ifP, &magic);
    if (rc == -1)
        pm_error("EOF / read error");

    if (magic != ybmMagic)
        pm_error("bad magic number in YBM file");

    rc = pm_readbigshort(ifP, colsP);
    if (rc == -1 )
        pm_error("EOF / read error");

    rc = pm_readbigshort(ifP, rowsP);
    if (rc == -1)
        pm_error("EOF / read error");

    *depthP = 1;
    *padrightP = ((*colsP + 15) / 16) * 16 - *colsP;
    bitsperitem = 0;
}



static bit
getbit(FILE * const ifP) {

    bit b;

    if (bitsperitem == 0) {
        item = getc(ifP) | getc(ifP) << 8;
        if (item == EOF)
            pm_error("EOF / read error");
        bitsperitem = 16;
        bitshift = 0;
    }

    b = ((item >> bitshift) & 1 ) ? PBM_BLACK : PBM_WHITE;
    --bitsperitem;
    ++bitshift;
    return b;
}



int
main(int argc, const char * argv[]) {

    FILE * ifP;
    bit * bitrow;
    short rows, cols, padright;
    unsigned int row;
    short depth;
    const char * inputFile;

    pm_proginit(&argc, argv);

    if (argc-1 < 1)
        inputFile = "-";
    else {
        inputFile = argv[1];

        if (argc-1 > 2)
            pm_error("Too many arguments.  The only argument is the optional "
                     "input file name");
    }

    ifP = pm_openr(inputFile);

    getinit(ifP, &cols, &rows, &depth, &padright);
    if (depth != 1)
        pm_error("YBM file has depth of %u, must be 1", (unsigned)depth);
    
    pbm_writepbminit(stdout, cols, rows, 0);

    bitrow = pbm_allocrow(cols);

    for (row = 0; row < rows; ++row) {
        /* Get data. */
        unsigned int col;
        for (col = 0; col < cols; ++col)
            bitrow[col] = getbit(ifP);
        /* Discard line padding */
        for (col = 0; col < padright; ++col)
            getbit(ifP);
        pbm_writepbmrow(stdout, bitrow, cols, 0);
    }

    pm_close(ifP);
    pm_close(stdout);

    return 0;
}
