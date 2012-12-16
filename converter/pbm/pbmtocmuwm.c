/* pbmtocmuwm.c - read a PBM image and produce a CMU window manager bitmap
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

/* 2006.10 (afu)
   Changed bitrow from plain to raw, read function from pbm_readpbmrow() to
   pbm_readpbmrow_packed(), write function from putc() to fwrite().

   Retired bitwise transformation functions.
*/

#include "pbm.h"



static void
putinit(unsigned int const rows,
        unsigned int const cols) {

    const char initWriteError[] =
        "CMU window manager header write error";
    uint32_t const cmuwmMagic = 0xf10040bb;

    int rc;

    rc = pm_writebiglong(stdout, cmuwmMagic);
    if (rc == -1)
        pm_error(initWriteError);
    rc = pm_writebiglong(stdout, cols);
    if (rc == -1)
        pm_error(initWriteError);
    rc = pm_writebiglong(stdout, rows);
    if (rc == -1)
        pm_error(initWriteError);
    rc = pm_writebigshort(stdout, (short) 1);
    if (rc == -1)
        pm_error(initWriteError);
}



int
main(int argc,
     char * argv[]) {

    FILE * ifP;
    unsigned char * bitrow;
    int rows, cols;
    int format;
    unsigned int row;
    const char * inputFileName;

    pbm_init(&argc, argv);

    if (argc-1 > 1)
        pm_error("Too many arguments (%u).  "
                 "Only argument is optional input file", argc-1);
    if (argc-1 == 1)
        inputFileName = argv[1];
    else
        inputFileName = "-";
    
    ifP = pm_openr(inputFileName);

    pbm_readpbminit(ifP, &cols, &rows, &format);
    bitrow = pbm_allocrow_packed(cols);

    putinit(rows, cols);
    
    /* Convert PBM raster data to CMUWM and write */ 
    for (row = 0; row < rows; ++row) {
        unsigned int const bytesPerRow = pbm_packed_bytes(cols);
        unsigned char const padding = 
            (cols % 8 == 0) ? 0x00 : ((unsigned char) ~0 >> (cols % 8));

        unsigned int i;
        size_t bytesWritten;

        pbm_readpbmrow_packed(ifP, bitrow, cols, format);

        /* Invert all bits in row - raster formats are similar.
           PBM   Black:1 White:0  "Don't care" bits at end of row
           CMUWM Black:0 White:1  End of row padded with 1
        */

        for (i = 0; i < bytesPerRow; ++i)
            bitrow[i] = ~bitrow[i];

        bitrow[bytesPerRow-1] |= padding;  /* Set row end pad bits */
        
        bytesWritten = fwrite(bitrow, 1, bytesPerRow, stdout);
        if (bytesWritten != bytesPerRow)
            pm_error("fwrite() failed to write CMU window manager bitmap");
    }

    pm_close(ifP);
    return 0;
}
