/* brushtopbm.c - read a doodle brush file and write a PBM image
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

#define HEADERSIZE 16   /* 16 is just a guess at the header size */



static void
getinit(FILE *         const ifP,
        unsigned int * const colsP,
        unsigned int * const rowsP) {

    unsigned char header[HEADERSIZE];
    size_t bytesRead;

    bytesRead = fread(header, sizeof(header), 1, ifP);
    if (bytesRead !=1)
        pm_error("Error reading header");   

    if (header[0] != 1)
        pm_error("bad magic number 1");
    if (header[1] != 0)
        pm_error("bad magic number 2");

    *colsP =  (header[2] << 8) + header[3];  /* Max 65535 */
    *rowsP =  (header[4] << 8) + header[5];  /* Max 65535 */
}



static void
validateEof(FILE * const ifP) {

    int rc;
    rc = getc(ifP);
    if (rc != EOF)
        pm_message("Extraneous data at end of file");
}


/*
   The routine for converting the raster closely resembles the pbm
   case of pnminvert.  Input is padded up to 16 bit border.
   afu December 2013
 */



int
main(int argc, const char ** argv)  {

    FILE * ifP;
    bit * bitrow;
    unsigned int rows, cols, row;

    pm_proginit(&argc, argv);

    if (argc-1 > 0) {
        ifP = pm_openr(argv[1]);
        if (argc-1 > 1)
            pm_error("Too many arguments (%u).  "
                     "The only argument is the brush file name.", argc-1);
    } else
        ifP = stdin;

    getinit(ifP, &cols, &rows);

    pbm_writepbminit(stdout, cols, rows, 0);

    bitrow = pbm_allocrow_packed(cols + 16);

    for (row = 0; row < rows; ++row) {
        unsigned int const inRowBytes = ((cols + 15) / 16) * 2;
        unsigned int i;
        size_t bytesRead;

        bytesRead = fread (bitrow, 1, inRowBytes, ifP); 
        if (bytesRead != inRowBytes)
            pm_error("Error reading a row of data from brushfile");

        for (i = 0; i < inRowBytes; ++i)
            bitrow[i] = ~bitrow[i];

        /* Clean off remainder of fractional last character */
        pbm_cleanrowend_packed(bitrow, cols);

        pbm_writepbmrow_packed(stdout, bitrow, cols, 0);
    }

    validateEof(ifP);

    pm_close(ifP);
    pm_close(stdout);
    
    return 0;
}
