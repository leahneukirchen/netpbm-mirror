/* mgrtopbm.c - read a MGR bitmap and produce a PBM image.

   Copyright information is at end of file.

   You can find MGR and some MGR format test images at
   ftp://sunsite.unc.edu/pub/Linux/apps/MGR/!INDEX.html

*/

#include <string.h>
#include <errno.h>

#include "pbm.h"
#include "mgr.h"



static void
readMgrHeader(FILE *          const ifP, 
              unsigned int *  const colsP, 
              unsigned int *  const rowsP, 
              unsigned int *  const depthP, 
              unsigned int *  const padrightP ) {
    
    struct b_header head;
    unsigned int pad;
    size_t bytesRead;

    bytesRead = fread(&head, sizeof(struct old_b_header), 1, ifP);
    if (bytesRead != 1)
        pm_error("Unable to read 1st byte of file.  "
                 "fread() returns errno %d (%s)",
                 errno, strerror(errno));
    if (head.magic[0] == 'y' && head.magic[1] == 'z') { 
        /* new style bitmap */
        size_t bytesRead;
        bytesRead = fread(&head.depth, 
                          sizeof(head) - sizeof(struct old_b_header), 1, ifP);
        if (bytesRead != 1 )
            pm_error("Unable to read header after 1st byte.  "
                     "fread() returns errno %d (%s)",
                     errno, strerror(errno));
        *depthP = (int) head.depth - ' ';
        pad = 8;
    } else if (head.magic[0] == 'x' && head.magic[1] == 'z') { 
        /* old style bitmap with 32-bit padding */
        *depthP = 1;
        pad = 32;
    } else if (head.magic[0] == 'z' && head.magic[1] == 'z') { 
        /* old style bitmap with 16-bit padding */
        *depthP = 1;
        pad = 16;
    } else if (head.magic[0] == 'z' && head.magic[1] == 'y') {
        /* old style 8-bit pixmap with 16-bit padding */
        *depthP = 8;
        pad = 16;
    } else {
        pm_error("bad magic chars in MGR file: '%c%c'",
                 head.magic[0], head.magic[1] );
        pad = 0;  /* should never reach here */
    }

    if (head.h_wide < ' ' || head.l_wide < ' ')
        pm_error("Invalid width field in MGR header");
    if (head.h_high < ' ' || head.l_high < ' ')
        pm_error("Invalid width field in MGR header");
    
    *colsP = (((int)head.h_wide - ' ') << 6) + ((int)head.l_wide - ' ');
    *rowsP = (((int)head.h_high - ' ') << 6) + ((int) head.l_high - ' ');
    *padrightP = ( ( *colsP + pad - 1 ) / pad ) * pad - *colsP;
}



int
main(int    argc,
     char * argv[]) {

    FILE * ifP;
    unsigned char * bitrow;
    unsigned int rows, cols, depth;
    unsigned int padright;
    unsigned int row;
    unsigned int itemCount;
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

    readMgrHeader(ifP, &cols, &rows, &depth, &padright);
    if (depth != 1)
        pm_error("MGR file has depth of %u, must be 1", depth);

    pbm_writepbminit(stdout, cols, rows, 0);

    bitrow = pbm_allocrow_packed(cols + padright);
    
    itemCount = (cols + padright ) / 8;

    for (row = 0; row < rows; ++row) {
        /* The raster formats are nearly identical.
           MGR may have rows padded to 16 or 32 bit boundaries.
        */
        size_t bytesRead;
        bytesRead = fread(bitrow, 1, itemCount, ifP);
        if (bytesRead < itemCount)
            pm_error("fread() failed to read mgr bitmap data");

        pbm_writepbmrow_packed(stdout, bitrow, cols, 0);
    }
    pm_close(ifP);
    pm_close(stdout);
    return 0;
}



/* 2006.10 (afu)
   Changed bitrow from plain to raw, read function from getc() to fread(),
   write function from pbm_writepbmrow() to pbm_writepbmrow_packed().
   Retired bitwise transformation functions.
   
   NOT tested for old-style format files.  Only one zz file in mgrsrc-0.69 .
  
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
