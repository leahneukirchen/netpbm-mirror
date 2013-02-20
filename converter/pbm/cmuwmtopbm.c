/* cmuwmtopbm.c - read a CMU window manager bitmap and produce a PBM image.
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
   Changed bitrow from plain to raw, read function from getc() to fread(),
   write function from pbm_writepbmrow() to pbm_writepbmrow_packed().
   Retired bitwise transformation functions.

   This program does not check the pad bits at the end of each row.
*/


#include "pbm.h"
#include "cmuwm.h"



static void
readCmuwmHeader(FILE *         const ifP,
                unsigned int * const colsP,
                unsigned int * const rowsP,
                unsigned int * const depthP) {

    const char * const initReadError =
        "CMU window manager header EOF / read error";

    long l;
    short s;
    int rc;

    rc = pm_readbiglong(ifP, &l);
    if (rc == -1 )
        pm_error(initReadError);
    if ((uint32_t)l != CMUWM_MAGIC)
        pm_error("bad magic number in CMU window manager file");
    rc = pm_readbiglong(ifP, &l);
    if (rc == -1)
        pm_error(initReadError);
    *colsP = l;
    rc = pm_readbiglong(ifP, &l);
    if (rc == -1 )
        pm_error(initReadError);
    *rowsP = l;
    rc = pm_readbigshort(ifP, &s);
    if (rc == -1)
        pm_error(initReadError);
    *depthP = s;
}



int
main(int     argc,
     char * argv[]) {

    FILE * ifP;
    unsigned char * bitrow;
    unsigned int rows, cols, depth;
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

    readCmuwmHeader(ifP, &cols, &rows, &depth);
    if (depth != 1)
        pm_error("CMU window manager file has depth of %u, must be 1", depth);

    pbm_writepbminit(stdout, cols, rows, 0);
    bitrow = pbm_allocrow_packed(cols);

    for (row = 0; row < rows; ++row) {
        unsigned int const bytesPerRow = pbm_packed_bytes(cols);
        unsigned int byteSeq;
        size_t bytesRead;

        bytesRead = fread(bitrow, 1, bytesPerRow, ifP);
        if (bytesRead != bytesPerRow)
            pm_error("CWU window manager bitmap EOF / read error");
            
        /* Invert all bits in row - raster formats are similar.
           CMUWM Black:0 White:1  End of row padded with 1
           PBM   Black:1 White:0  End preferably padded with 0
        */
   
        for (byteSeq = 0; byteSeq < bytesPerRow; ++byteSeq)
            bitrow[byteSeq] = ~bitrow[byteSeq];
                
        pbm_writepbmrow_packed(stdout, bitrow, cols, 0);
    }

    pm_close(ifP);
    pm_close(stdout);

    return 0;
}
