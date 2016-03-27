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
#include "bitreverse.h"

#define YBM_MAGIC  ( ( '!' << 8 ) | '!' )
#define INT16MAX 32767

static void
putinit(int const cols,
        int const rows) {

    pm_writebigshort(stdout, YBM_MAGIC);
    pm_writebigshort(stdout, cols);
    pm_writebigshort(stdout, rows);
}



int
main(int argc, const char *argv[]) {

    FILE * ifP;
    bit * bitrow;
    int rows;
    int cols;
    int format;
    unsigned int row;
    const char * inputFileName;

    pm_proginit(&argc, argv);

    if (argc-1 < 1)
        inputFileName = "-";
    else {
        inputFileName = argv[1];

        if (argc-1 > 1)
            pm_error("Too many arguments.  The only argument is the optional "
                     "input file name");
    }

    ifP = pm_openr(inputFileName);

    pbm_readpbminit(ifP, &cols, &rows, &format);

    if (rows > INT16MAX || cols > INT16MAX)
        pm_error("Input image is too large.");

    bitrow = pbm_allocrow_packed(cols + 8);
    
    putinit(cols, rows);

    bitrow[pbm_packed_bytes(cols + 8) - 1] = 0x00;
    for (row = 0; row < rows; ++row) {
        uint16_t *   const itemrow = (uint16_t *) bitrow;
        unsigned int const itemCt   = (cols + 15) / 16;

        unsigned int i;

        pbm_readpbmrow_packed(ifP, bitrow, cols, format);
        pbm_cleanrowend_packed(bitrow, cols);

        for (i = 0; i < pbm_packed_bytes(cols); ++i)
            bitrow[i] = bitreverse[bitrow[i]];

        for (i = 0; i < itemCt; ++i)
            pm_writebigshort(stdout, itemrow[i]);
    }

    pbm_freerow_packed(bitrow);

    if (ifP != stdin)
        fclose(ifP);

    return 0;
}


