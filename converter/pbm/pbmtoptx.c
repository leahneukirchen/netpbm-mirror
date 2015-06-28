/* pbmtoptx.c - read a portable bitmap and produce a Printronix printer file
**
** Copyright (C) 1988 by Jef Poskanzer.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

#include "pbm.h"

/* Follwing is obtained by reversing bit order (MFS-LFS) and adding 64. */
/* Note the two escape sequences: \\ and \x7f . */

static unsigned char const ptxchar[64] = 
  "@`PpHhXxDdTtLl\\|BbRrJjZzFfVvNn^~AaQqIiYyEeUuMm]}CcSsKk[{GgWwOo_\x7f";



static void
putBitrow(const bit *  const bitrow,
          unsigned int const cols) {
/*----------------------------------------------------------------------------
  Pick up items in 6 bit units from bitrow and convert each to ptx format.
----------------------------------------------------------------------------*/
    unsigned int itemCnt;

    for (itemCnt = 0; itemCnt * 6 < cols; ++itemCnt) {
        unsigned int const byteCnt = (itemCnt * 6) / 8;
        bit const byteCur  = bitrow[byteCnt];
        bit const byteNext = bitrow[byteCnt + 1];
        
        unsigned int item;

        switch (itemCnt % 4) {
        case 0: item = byteCur >> 2;                 break;
        case 1: item = byteCur << 4 | byteNext >> 4; break;
        case 2: item = byteCur << 2 | byteNext >> 6; break;
        case 3: item = byteCur;                      break;
        }
        putchar(ptxchar[item & 0x3f]);
    }
    putchar(5); putchar('\n');  /* end of row mark */
}



int
main(int argc, const char ** argv)  {

    FILE * ifP;
    bit * bitrow;
    int rows, cols, format;
    unsigned int row;

    pm_proginit(&argc, argv);

    if (argc-1 < 1)
        ifP = stdin;
    else {
        ifP = pm_openr(argv[1]);
        
        if (argc-1 > 1)
            pm_error("Too many arguments.  The only possible argument is "
                     "the input fil name");
    }

    pbm_readpbminit(ifP, &cols, &rows, &format);

    bitrow = pbm_allocrow_packed(cols + 8);

    bitrow[pbm_packed_bytes(cols)] = 0x00;

    for (row = 0; row < rows; ++row) {
        pbm_readpbmrow_packed(ifP, bitrow, cols, format);
        pbm_cleanrowend_packed(bitrow, cols);
        putBitrow(bitrow, cols);
    }

    pbm_freerow_packed(bitrow);
    pm_close(ifP);
    
    return 0;
}



