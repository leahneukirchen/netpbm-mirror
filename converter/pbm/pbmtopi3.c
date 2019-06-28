/* pbmtopi3.c - read a PBM image and produce a Atari Degas .pi3 file
**
** Module created from other pbmplus tools by David Beckemeyer.
**
** Copyright (C) 1988 by David Beckemeyer and Jef Poskanzer.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

/* Output file should always be 32034 bytes. */

#include <stdio.h>
#include "pm_c_util.h"
#include "pbm.h"



static void
putinit(FILE * const ofP)  {

    unsigned int i;

    pm_writebigshort(ofP, (short) 2);
    pm_writebigshort(ofP, (short) 0x777);

    for (i = 1; i < 16; ++i) {
        pm_writebigshort (ofP, (short) 0);
    }
}



int
main(int argc, const char ** argv) {

    unsigned int const outRows = 400;
    unsigned int const outCols = 640;
    unsigned int const outColByteCt = pbm_packed_bytes(outCols);

    FILE * ifP;

    int inRows, inCols, format;
    unsigned int row;
    unsigned int inColByteCt;
    unsigned int i;
    bit * bitrow;

    pm_proginit(&argc, argv);

    if (argc-1 < 1)
        ifP = stdin;
    else  {
        ifP = pm_openr(argv[1]);

        if (argc-1 > 1)
            pm_error("Too many arguments.  The only possible argument "
                     "is the input file name");
    }

    pbm_readpbminit(ifP, &inCols, &inRows, &format);

    inColByteCt = pbm_packed_bytes(inCols);

    bitrow = pbm_allocrow_packed(MAX(outCols, inCols));
    
    /* Add padding to round cols up to 640 */
    for (i = inColByteCt; i < outColByteCt; ++i)
        bitrow[i] = 0x00;

    putinit(stdout);

    for (row = 0; row < MIN(inRows, outRows); ++row) {
        pbm_readpbmrow_packed(ifP, bitrow, inCols, format);
        pbm_cleanrowend_packed(bitrow, inCols);
        fwrite (bitrow, outColByteCt, 1, stdout);
    }
    pm_close(ifP);

    if (row < outRows)  {
        unsigned int i;

        /* Clear entire row */
        for (i = 0; i < outColByteCt; ++i)
            bitrow[i] = 0x00;

        while (row++ < outRows)
            fwrite(bitrow, outColByteCt, 1, stdout);
    }

    pbm_freerow_packed(bitrow);

    return 0;
}
