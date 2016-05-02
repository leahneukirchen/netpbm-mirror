/* pbmtomgr.c - read a PBM image and produce a MGR bitmap

   Copyright information is at end of file.

   You can find MGR and some MGR format test images at
   ftp://sunsite.unc.edu/pub/Linux/apps/MGR/!INDEX.html
*/

#include <assert.h>
#include "pbm.h"
#include "mgr.h"



static void
putinit(unsigned int const rows,
        unsigned int const cols) {

    struct b_header head;
    size_t writtenCount;

    /* Because of argument restrictions: maximum dimensions: */
    assert((rows & 0xfff) == rows);
    assert((cols & 0xfff) == cols);

    head.magic[0] = 'y';
    head.magic[1] = 'z';
    head.h_wide = ((cols >> 6) & 0x3f) + ' ';
    head.l_wide = (cols & 0x3f) + ' ';
    head.h_high = (( rows >> 6) & 0x3f) + ' ';
    head.l_high = (rows & 0x3f) + ' ';
    head.depth = (1 & 0x3f) + ' ';
    head._reserved = ' ';
    writtenCount = fwrite(&head, sizeof(head), 1, stdout);
    if (writtenCount != 1)
        pm_error("fwrite() failed to write the MGR header.");    
}



int
main(int argc,
     const char * argv[]) {

    FILE * ifP;
    unsigned char * bitrow;
    int rows;
    int cols;
    int format;
    unsigned int row;
    unsigned int bytesPerRow;
        /* Number of packed bytes (8 columns per byte) in a row. */
    const char * inputFileName;
    unsigned int const maxDimension = 4095;
        /* Dimensions are 2 characters of the header -- 12 bits */

    pm_proginit(&argc, argv);

    if (argc-1 > 1)
        pm_error("Too many arguments (%u).  "
                 "Only argument is optional input file", argc-1);
    if (argc-1 == 1)
        inputFileName = argv[1];
    else
        inputFileName = "-";
    
    ifP = pm_openr(inputFileName);

    pbm_readpbminit(ifP, &cols, &rows, &format);
    if (cols > maxDimension)
        pm_error("Image width too large: %u (max: %u)", cols, maxDimension);
    if (rows > maxDimension)
        pm_error("Image height too large: %u (max: %u)", rows, maxDimension);
    
    bitrow = pbm_allocrow_packed(cols);
    bytesPerRow = pbm_packed_bytes(cols);

    putinit(rows, cols);
    
    for (row = 0; row < rows; ++row) {
        /* The raster formats are identical. 
           The row end pad bits are set to 0 in mgr.
        */
        size_t bytesWritten;

        pbm_readpbmrow_packed(ifP, bitrow, cols, format);
        pbm_cleanrowend_packed(bitrow, cols);

        bytesWritten = fwrite(bitrow, 1, bytesPerRow, stdout);
        if (bytesWritten != bytesPerRow )
            pm_error("fwrite() failed to write MGR bitmap "
                     "to Standard Output.");    
    }
    pm_close(ifP);
    return 0;
}



/* 2006.10 (afu)
   Changed bitrow from plain to raw, read function from pbm_readpbmrow() to
   pbm_readpbmrow_packed(), write function from putc() to fwrite().

   Retired bitwise transformation functions.
   
   Produces only new style bitmap (8 bit padding.)  See mgrtopbm.c .
*/

/*
** Copyright (C) 1989 by Jef Poskanzer.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/
