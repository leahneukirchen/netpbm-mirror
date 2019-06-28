/* pbmtosunicon.c - read a PBM image and produce a Sun icon file
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

/* 2006.10 (afu)
   Changed bitrow from plain to raw, read function from pbm_readpbmrow() to
   pbm_readpbmrow_packed.  Applied wordint to scoop up 16 bit output items.
   putitem changed to better express the output format.
   Retired bitwise transformation functions.
*/

#include "pm_config.h"
#include "pbm.h"

static struct ItemPutter {
    unsigned short int itemBuff[8];
    unsigned int       itemCnt;    /* takes values 0 to 8 */
    FILE *             putFp;
} ip;



static void
putinit(FILE * const ofP) {
    ip.putFp   = ofP;
    ip.itemCnt = 0;
}



static void
putitem(uint16_t const item) {

    if (ip.itemCnt == 8 ) {
        /* Buffer is full.  Write out one line. */
        int rc;
    
        rc = fprintf(ip.putFp,
                     "\t0x%04x,0x%04x,0x%04x,0x%04x,"
                     "0x%04x,0x%04x,0x%04x,0x%04x,\n",
                     ip.itemBuff[0], ip.itemBuff[1],
                     ip.itemBuff[2], ip.itemBuff[3],
                     ip.itemBuff[4], ip.itemBuff[5],
                     ip.itemBuff[6], ip.itemBuff[7]);
        if (rc < 0)        
           pm_error("fprintf() failed to write Icon bitmap");
           
        ip.itemCnt = 0;
    }
    ip.itemBuff[ip.itemCnt++] = item & 0xffff;
        /* Only lower 16 bits are used */
}



static void
putterm(void) {

    unsigned int i;

    for (i = 0; i < ip.itemCnt; ++i) {
        int rc;
        rc = fprintf(ip.putFp, "%s0x%04x%c", i == 0  ? "\t" : "",
                     ip.itemBuff[i],
                     i == ip.itemCnt - 1 ? '\n' : ',');
        if (rc < 0)        
            pm_error("fprintf() failed to write Icon bitmap");
    }
}     



static void
writeIconHeader(FILE *       const ofP,
                unsigned int const width,
                unsigned int const height) {

    int rc;

    rc = fprintf(ofP,
                 "/* Format_version=1, Width=%u, Height=%u", width, height);
    if (rc < 0)
        pm_error("fprintf() failed to write Icon header");
        
    rc = fprintf(ofP, ", Depth=1, Valid_bits_per_item=16\n */\n");
    if (rc < 0)
        pm_error("fprintf() failed to write Icon header");
}



static void
writeIcon(FILE *       const ifP,
          unsigned int const cols,
          unsigned int const rows,
          int          const format,
          FILE *       const ofP) {

    unsigned int const items = (cols + 15) / 16;
    unsigned int const pad = items * 16 - cols;

    unsigned char * const bitrow = pbm_allocrow_packed(items * 16);
    unsigned int row;

    bitrow[0] = bitrow[items * 2 - 1] = 0;

    writeIconHeader(ofP, cols + pad, rows);

    putinit(ofP);

    for (row = 0; row < rows; ++row) {
        unsigned int itemSeq;

        pbm_readpbmrow_bitoffset(ifP, bitrow, cols, format, pad/2);

        for (itemSeq = 0; itemSeq < items; ++itemSeq) {
            /* Read bits from bitrow, send to format & print function. */
            
            putitem((bitrow[itemSeq*2]<<8) + bitrow[itemSeq*2+1]);
        }
    }
    putterm();
    pbm_freerow_packed(bitrow);
}



int
main(int argc,
     char * argv[]) {

    FILE * ifP;
    int rows, cols;
    int format;
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

    writeIcon(ifP, cols, rows, format, stdout);

    pm_close(ifP);

    return 0;
}

