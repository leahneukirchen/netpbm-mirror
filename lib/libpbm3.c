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

#include <assert.h>

#include "pm_c_util.h"
#include "pbm.h"

#if HAVE_GCC_MMXSSE
#include "bitreverse.h"
#endif

/* HAVE_GCC_MMXSSE means we have the means to use MMX and SSE CPU facilities
   to make PBM raster processing faster.  GCC only.

   The GNU Compiler -msse option makes SSE available.
   For x86-32 with MMX/SSE, "-msse" must be explicitly given.
   For x86-64 and AMD64, "-msse" is on by default.
*/

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


#if HAVE_GCC_MMXSSE
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
    /*
      We use MMX/SSE facilities that operate on 8 bytes at once to pack
      the bits quickly.
    
      We use 2 MMX registers (no SSE registers).
    
      The key machine instructions are:
    
    
      PCMPGTB  Packed CoMPare Greater Than Byte
    
        Compares 8 bytes in parallel
        Result is x00 if greater than, xFF if not for each byte       
    
      PMOVMSKB Packed MOVe MaSK Byte 
    
        Result is a byte of the MSBs of 8 bytes
        x00 xFF x00 xFF xFF xFF x00 x00 --> 01011100B = 0x5C
        
        The result is actually a 32 bit int, but the higher bits are
        always 0.  (0x0000005C in the above case)
    
      EMMS     Empty MMx State
    
        Free MMX registers  
    
    */


    typedef char v8qi __attribute__ ((vector_size(8)));
    typedef int di __attribute__ ((mode(DI)));

    unsigned int col;
    v8qi const zero64 =(v8qi)((di)0);  /* clear to zero */

    for (col = 0; col + 7 < cols; col += 8) {

        v8qi const compare =
            __builtin_ia32_pcmpgtb(*(v8qi*) (&bitrow[col]), (v8qi) zero64);
        uint32_t const backwardBlackMask =  __builtin_ia32_pmovmskb(compare);
        unsigned char const blackMask = bitreverse[backwardBlackMask];

        packedBits[col/8] = blackMask;
    }
    *nextColP = col;

    __builtin_ia32_emms();

}
#else
/* Avoid undefined function warning; never actually called */

#define packBitsWithMmxSse(a,b,c,d,e) packBitsGeneric(a,b,c,d,e)
#endif




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

        if (HAVE_GCC_MMXSSE)
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
writePbmRowPlain(FILE *      const fileP,
                 const bit * const bitrow, 
                 int         const cols) {
    
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
pbm_writepbmrow(FILE *       const fileP, 
                const bit *  const bitrow, 
                int          const cols, 
                int          const forceplain) {

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



static unsigned char
leftBits(unsigned char const x,
         unsigned int  const n) {
/*----------------------------------------------------------------------------
   Clear rightmost (8-n) bits, retain leftmost (=high) n bits.
-----------------------------------------------------------------------------*/
    unsigned char buffer;

    assert(n < 8);

    buffer = x;

    buffer >>= (8-n);
    buffer <<= (8-n);

    return buffer;
}



void
pbm_writepbmrow_bitoffset(FILE *          const fileP,
                          unsigned char * const packedBits,
                          unsigned int    const cols,
                          int             const format,
                          unsigned int    const offset) {
/*----------------------------------------------------------------------------
   Write PBM row from a packed bit buffer 'packedBits, starting at the
   specified offset 'offset' in the buffer.

   We destroy the buffer.
-----------------------------------------------------------------------------*/
    unsigned int const rsh = offset % 8;
    unsigned int const lsh = (8 - rsh) % 8;
    unsigned int const csh = cols % 8;
    unsigned char * const window = &packedBits[offset/8];
        /* Area of packed row buffer from which we take the image data.
           Aligned to nearest byte boundary to the left, so the first
           few bits might be irrelvant.

           Also our work buffer, in which we shift bits and from which we
           ultimately write the bits to the file.
        */
    unsigned int const colByteCnt = pbm_packed_bytes(cols);
    unsigned int const last = colByteCnt - 1;
        /* Position within window of rightmost byte after shift */

    bool const carryover = (csh == 0 || rsh + csh > 8);
        /* TRUE:  Input comes from colByteCnt bytes and one extra byte.
           FALSE: Input comes from colByteCnt bytes.  For example:
           TRUE:  xxxxxxii iiiiiiii iiiiiiii iiixxxxx  cols=21, offset=6 
           FALSE: xiiiiiii iiiiiiii iiiiiixx ________  cols=21, offset=1

           We treat these differently for in the FALSE case the byte after
           last (indicated by ________) may not exist.
        */
       
    if (rsh > 0) {
        unsigned int const shiftBytes =  carryover ? colByteCnt : colByteCnt-1;

        unsigned int i;
        for (i = 0; i < shiftBytes; ++i)
            window[i] = window[i] << rsh | window[i+1] >> lsh;

        if (!carryover)
            window[last] = window[last] << rsh;
    }
      
    if (csh > 0)
        window[last] = leftBits(window[last], csh);
          
    pbm_writepbmrow_packed(fileP, window, cols, 0);
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
