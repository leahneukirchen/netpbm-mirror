/* pbmtoybm.c - read a pbm and write a file for Bennet Yee's 'xbm' and 'face'
** programs.
**
** Written by Jamie Zawinski based on code (C) 1988 by Jef Poskanzer.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
**
** Feb 2010 afu
** Added dimension check to prevent short int from overflowing
** Changed code style (ANSI-style function definitions, etc.)
*/

#include <stdio.h>

#include "pm.h"
#include "pbm.h"

#define YBM_MAGIC  ( ( '!' << 8 ) | '!' )
#define INT16MAX 32767

static long item;
static int bitsperitem, bitshift;


static void
putitem(void) {

    pm_writebigshort(stdout, item);

    item        = 0;
    bitsperitem = 0;
    bitshift    = 0;
}



static void
putinit(int const cols,
        int const rows) {

    pm_writebigshort(stdout, YBM_MAGIC);
    pm_writebigshort(stdout, cols);
    pm_writebigshort(stdout, rows);

    item        = 0;
    bitsperitem = 0;
    bitshift    = 0;
}



static void
putbit(bit const b) {

    if (bitsperitem == 16)
        putitem();

    ++bitsperitem;

    if (b == PBM_BLACK)
        item += 1 << bitshift;

    ++bitshift;
}



static void
putrest(void) {

    if (bitsperitem > 0)
        putitem();
}



int
main(int argc, const char *argv[]) {

    FILE * ifP;
    bit * bitrow;
    int rows;
    int cols;
    int format;
    unsigned int padright;
    unsigned int row;
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

    pbm_readpbminit(ifP, &cols, &rows, &format);

    if (rows > INT16MAX || cols > INT16MAX)
        pm_error("Input image is too large.");

    bitrow = pbm_allocrow(cols);
    
    /* Compute padding to round cols up to the nearest multiple of 16. */
    padright = ((cols + 15) / 16) * 16 - cols;

    putinit(cols, rows);
    for (row = 0; row < rows; ++row) {
        unsigned int col;

        pbm_readpbmrow(ifP, bitrow, cols, format);

        for (col = 0; col < cols; ++col)
            putbit(bitrow[col]);

        for (col = 0; col < padright; ++col)
            putbit(0);
    }

    if (ifP != stdin)
        fclose(ifP);

    putrest();

    return 0;
}
