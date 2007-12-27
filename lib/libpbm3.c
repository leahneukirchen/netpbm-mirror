/* libpbm3.c - pbm utility library part 3
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
#include "bitreverse.h"

#if defined(__GNUC__) && (__GNUC__ * 100 + __GNUC_MINOR__ >= 301) && defined (__SSE__)
/* intel MMX-SSE enhancement for pbm_writepbmowraw() */
/* GCC only.  Turn on with -msse */

#define HAVE_MMX_SSE 1
#else
#define HAVE_MMX_SSE 0
#endif


void
pbm_writepbminit(FILE * const fileP, 
                 int    const cols, 
                 int    const rows, 
                 int    const forceplain) {

    if (!forceplain && !pm_plain_output) {
        fprintf(fileP, "%c%c\n%d %d\n", PBM_MAGIC1, RPBM_MAGIC2, cols, rows);
#ifdef VMS
        set_outfile_binary();
#endif
    } else
        fprintf(fileP, "%c%c\n%d %d\n", PBM_MAGIC1, PBM_MAGIC2, cols, rows);
}



static void
writePackedRawRow(FILE *                const fileP,
                  const unsigned char * const packed_bits,
                  int                   const cols) {

    int bytesWritten;
    bytesWritten = fwrite(packed_bits, 1, pbm_packed_bytes(cols), fileP);
    if (bytesWritten < pbm_packed_bytes(cols)) 
        pm_error("I/O error writing packed row to raw PBM file.");
} 



static void
packBitsWithMmxSse(FILE *          const fileP,
                   const bit *     const bitrow,
                   unsigned char * const packedBits,
                   unsigned int    const cols,
                   unsigned int *  const nextColP) {
/*----------------------------------------------------------------------------
   Pack the bits of bitrow[] into bytes at 'packedBits'.  Going left to right,
   stop when there aren't enough bits left to fill a whole byte.  Return
   as *nextColP the number of the next column after the rightmost one we
   packed.

   Use the Pentium MMX and SSE facilities to pack the bits quickly, but
   perform the exact same function as the simpler packBitsGeneric().
-----------------------------------------------------------------------------*/
#if HAVE_MMX_SSE
    /*
      We use MMX/SSE facilities that operate on 8 bytes at once to pack
      the bits quickly.
    
      We use 2 MMX registers (no SSE registers).
    
      The key machine instructions are:
    
    
      PCMPEQB  Packed CoMPare EQual Byte
    
        Compares 8 bytes in parallel
        Result is x00 if equal, xFF if unequal for each byte       
    
      PMOVMSKB Packed MOVe MaSK Byte 
    
        Result is a byte of the MSBs of 8 bytes
        x00 xFF x00 xFF xFF xFF x00 x00 --> 01011100B = 0x5C     
    
    
      EMMS     Empty MMx State
    
        Free MMX registers  
    
    
      Here's a one-statement version of the code in our foor loop.  It's harder 
      to read, but if we find out this generates more efficient code, we could 
      use this.
    
        packedBits[col/8] 
          = bitreverse [ ~ (unsigned char) __builtin_ia32_pmovmskb (
            __builtin_ia32_pcmpeqb ( *(v8qi*) (&bitrow[col]), *(v8qi*) &zero64)
            ) ];
    */

    typedef int v8qi __attribute__ ((mode(V8QI)));
    typedef int di __attribute__ ((mode(DI)));

    di const zero64 = 0;        /* to clear with PXOR */

    unsigned int col;

    for (col = 0; col + 7 < cols; col += 8) {
        v8qi const compare =
            __builtin_ia32_pcmpeqb(*(v8qi*) (&bitrow[col]), *(v8qi*) &zero64);
        unsigned char const backwardWhiteMask = (unsigned char)
            __builtin_ia32_pmovmskb(compare);
        unsigned char const backwardBlackMask = ~backwardWhiteMask;
        unsigned char const blackMask = bitreverse[backwardBlackMask];

        packedBits[col/8] = blackMask;
    }
    *nextColP = col;

    __builtin_ia32_emms();

#else
    if (bitreverse == bitreverse) {}; /* avoid unused vbl compiler warning */
#endif
}



static unsigned int
bitValue(unsigned char const byteValue) {

    return byteValue == 0 ? 0 : 1;
}



static void
packBitsGeneric(FILE *          const fileP,
                const bit *     const bitrow,
                unsigned char * const packedBits,
                unsigned int    const cols,
                unsigned int *  const nextColP) {
/*----------------------------------------------------------------------------
   Pack the bits of bitrow[] into bytes at 'packedBits'.  Going left to right,
   stop when there aren't enough bits left to fill a whole byte.  Return
   as *nextColP the number of the next column after the rightmost one we
   packed.

   Don't use any special CPU facilities to do the packing.
-----------------------------------------------------------------------------*/
    unsigned int col;

    for (col = 0; col + 7 < cols; col += 8)
        packedBits[col/8] = (
            bitValue(bitrow[col+0]) << 7 |
            bitValue(bitrow[col+1]) << 6 |
            bitValue(bitrow[col+2]) << 5 |
            bitValue(bitrow[col+3]) << 4 |
            bitValue(bitrow[col+4]) << 3 |
            bitValue(bitrow[col+5]) << 2 |
            bitValue(bitrow[col+6]) << 1 |
            bitValue(bitrow[col+7]) << 0
            );
    *nextColP = col;
}



static void
packPartialBytes(const bit *     const bitrow,
                 unsigned int    const cols,
                 unsigned int    const nextCol,
                 unsigned char * const packedBits) {
              
    /* routine for partial byte at the end of packedBits[]
       Prior to addition of the above enhancement,
       this method was used for the entire process
    */                   
    
    unsigned int col;
    int bitshift;
    unsigned char item;
    
    bitshift = 7;  /* initial value */
    item = 0;      /* initial value */
    for (col = nextCol; col < cols; ++col, --bitshift)
        if (bitrow[col] != 0)
            item |= 1 << bitshift;
    
    packedBits[col/8] = item;
}



static void
writePbmRowRaw(FILE *      const fileP,
               const bit * const bitrow,
               int         const cols) {

    jmp_buf jmpbuf;
    jmp_buf * origJmpbufP;
    unsigned char * packedBits;

    packedBits = pbm_allocrow_packed(cols);

    if (setjmp(jmpbuf) != 0) {
        pbm_freerow_packed(packedBits);
        pm_setjmpbuf(origJmpbufP);
        pm_longjmp();
    } else {
        unsigned int nextCol;

        pm_setjmpbufsave(&jmpbuf, &origJmpbufP);

        if (HAVE_MMX_SSE)
            packBitsWithMmxSse(fileP, bitrow, packedBits, cols, &nextCol);
        else 
            packBitsGeneric(fileP, bitrow, packedBits, cols, &nextCol);

        if (cols % 8 > 0)
            packPartialBytes(bitrow, cols, nextCol, packedBits);
        
        writePackedRawRow(fileP, packedBits, cols);

        pm_setjmpbuf(origJmpbufP);
    }
    pbm_freerow_packed(packedBits);
}



static void
writePbmRowPlain(FILE * const fileP,
                 bit *  const bitrow, 
                 int    const cols) {
    
    int col, charcount;

    charcount = 0;
    for (col = 0; col < cols; ++col) {
        if (charcount >= 70) {
            putc('\n', fileP);
            charcount = 0;
        }
        putc(bitrow[col] ? '1' : '0', fileP);
        ++charcount;
    }
    putc('\n', fileP);
}



void
pbm_writepbmrow(FILE * const fileP, 
                bit *  const bitrow, 
                int    const cols, 
                int    const forceplain) {

    if (!forceplain && !pm_plain_output)
        writePbmRowRaw(fileP, bitrow, cols);
    else
        writePbmRowPlain(fileP, bitrow, cols);
}



void
pbm_writepbmrow_packed(FILE *                const fileP, 
                       const unsigned char * const packedBits,
                       int                   const cols, 
                       int                   const forceplain) {

    if (!forceplain && !pm_plain_output)
        writePackedRawRow(fileP, packedBits, cols);
    else {
        jmp_buf jmpbuf;
        jmp_buf * origJmpbufP;
        bit * bitrow;

        bitrow = pbm_allocrow(cols);

        if (setjmp(jmpbuf) != 0) {
            pbm_freerow(bitrow);
            pm_setjmpbuf(origJmpbufP);
            pm_longjmp();
        } else {
            unsigned int col;
            
            pm_setjmpbufsave(&jmpbuf, &origJmpbufP);

            for (col = 0; col < cols; ++col) 
                bitrow[col] = 
                    packedBits[col/8] & (0x80 >> (col%8)) ?
                    PBM_BLACK : PBM_WHITE;

            writePbmRowPlain(fileP, bitrow, cols);

            pm_setjmpbuf(origJmpbufP);
        }
        pbm_freerow(bitrow);
    }
}



void
pbm_writepbm(FILE * const fileP, 
             bit ** const bits, 
             int    const cols, 
             int    const rows, 
             int    const forceplain) {

    int row;

    pbm_writepbminit(fileP, cols, rows, forceplain);
    
    for (row = 0; row < rows; ++row)
        pbm_writepbmrow(fileP, bits[row], cols, forceplain);
}
