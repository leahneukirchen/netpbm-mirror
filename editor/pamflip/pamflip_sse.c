/*=============================================================================
                                   pamflip_sse.c
===============================================================================
  This is part of the Pamflip program.  It contains code that exploits
  the SSE facility of some CPUs.

  This code was orginally written by Akira Urushibata ("Douso") in 2010 and is
  contributed to the public domain by all authors.

  The author makes the following request (which is not a reservation of legal
  rights): Please study the code and make adjustments to meet specific needs.
  This part is critical to performance.  I have seen code copied off from
  poorly implemented.  Please put a comment in the code so people will know
  where it came from.

=============================================================================*/

#include <assert.h>

#include "pm_config.h"
#include "pm_c_util.h"
#include "pam.h"

#include "flip.h"

#include "pamflip_sse.h"

#if HAVE_GCC_SSE2 && defined(__SSE2__)

/*----------------------------------------------------------------------------
   This is a specialized routine for row-for-column PBM transformations.
   (-cw, -ccw, -xy).  It requires GCC (>= v. 4.2.0) and SSE2. 

   In each cycle, we read sixteen rows from the input.  We process this band
   left to right in blocks 8 pixels wide.  We use the SSE2 instruction
   pmovmskb128, which reports the MSB of each byte in a 16 byte array, for
   fast processing.  We place the 8x16 block into a 16 byte array, and
   pmovmskb128 reports the 16 pixels on the left edge in one instruction
   execution.  pslldi128 shifts the array contents leftward.

   The following routines can write both in both directions (left and right)
   into the output rows.  They do this by controlling the vertical stacking
   order when they make the 8x16 blocks.
 
   Function transpose1to15Bitrows() is for handling the partial bits of each
   output row.  They can come from either the top or bottom of the vertical
   input column, but they always go to the right end of the output rows.

   transformRowsToColumnsPbm() does not have any instructions unique to
   GCC or SSE.  It is possible to write a non-SSE version by providing
   generic versions of transpose16Bitrows() and transpose1to15Bitrows() .
   This is just a matter of replacing the V16 union with a plain uchar
   array and writing an emulation for __builtin_pmovmskb128() .
 
   Further enhancement should be possible by employing wider bands,
   larger blocks as wider SIMD registers become available.  Another
   method is checking for white blocks and recording them in a small
   array and condensing writes into the output raster array.
-----------------------------------------------------------------------------*/

typedef char v16qi __attribute__ ((vector_size (16)));
typedef int  v4di  __attribute__ ((vector_size (16)));

union V16 {
    v16qi v;
    v4di d;
    unsigned char i[16];
};

/* Beware when making modifications to code which involve v16qi, v4di, V16.
   Certain versions of GCC get stuck with the following:

   (1) Type mismatches between v16qi and v4di.  Avoid them with casts.

   (2) Converions from a 16 byte array to v16qi (or union V16) by cast.
       __vector__ variables have to be vector from the start. 

   (3) union V16 as a register variable.

   Some GCC versions emit warnings, others abort with error.
*/



static void
transpose1to15Bitrows(unsigned int const cols,
                      unsigned int const rows,
                      bit **       const inrow,
                      uint16_t **  const outplane,
                      int          const xdir) {
/*--------------------------------------------------------------------------
  Convert input rows to output columns.  For handling partial rows.
  Note that output from this always goes to the right edge of the image.
----------------------------------------------------------------------------*/
    unsigned int const outcol16 = (rows-1)/16;

    unsigned int col;

    union V16 v16;
    v16.v = v16.v ^ v16.v;  /* clear to zero */

    for (col = 0; col < cols; ++col) {
        unsigned int const outrow = col;

        if (col % 8 == 0) {
            unsigned int i;
            for (i = 0; i < rows % 16; ++i) {
                int const idx = (xdir > 0) ?
                    (i&8) + 7-(i&7) :       /* output left to right */
                    (24- rows%16 +i) %16;  /*        right to left */
                v16.i [idx] = inrow[i][col/8];
            }
        }
        /* read 16 bits from left edge of block; write to output column  */
        outplane[outrow][outcol16] = __builtin_ia32_pmovmskb128(v16.v);
        v16.d = __builtin_ia32_pslldi128(v16.d, 1);
    }
}



static void
transpose16Bitrows(unsigned int const cols,
      	           unsigned int const rows,
                   const bit *  const block[16],
                   uint16_t **  const outplane,
                   unsigned int const outcol16) {
/*--------------------------------------------------------------------------
  Convert input rows to output columns.  Works in units of 8x16.

  Uses pre-calculated pointers ( block[i][col8] ) instead of
  (xdir > 0) ? (i & 0x08) + 7 - (i & 0x07) : (24 - rows % 16 +i) % 16
  for efficiency.

  We avoid using union V16 to keep the value in a register.  (When we do so,
  GCC (4.2, 4.4) sees the suffix x of v16.i[x] and apparently decides that
  the variable has to be addressable and therefore needs to be placed into
  memory.)
---------------------------------------------------------------------------*/
    unsigned int col;

    for (col = 0; col + 7 < cols; col += 8) {    /* Unrolled loop */
        unsigned int const col8 = col / 8;

        unsigned int outrow;
        unsigned int i;

        register v16qi vReg = {
            block[0][col8],  block[1][col8],
            block[2][col8],  block[3][col8],  
            block[4][col8],  block[5][col8],
            block[6][col8],  block[7][col8],
            block[8][col8],  block[9][col8],
            block[10][col8], block[11][col8],
            block[12][col8], block[13][col8],
            block[14][col8], block[15][col8] };

        outrow = col;  /* initial value */

        for (i = 0; i < 7; ++i) {
            /* GCC (>=4.2) automatically unrolls this loop */  
            outplane[outrow++][outcol16] = __builtin_ia32_pmovmskb128(vReg);
            vReg = (v16qi)__builtin_ia32_pslldi128 ((v4di)vReg, 1);
        }
        outplane[outrow][outcol16] = __builtin_ia32_pmovmskb128(vReg);
    }

    if (col < cols) {  /* Transpose remaining pixels at end of input rows. */
        unsigned int const col8 = col / 8;
        register v16qi vReg = {
            block[0][col8],  block[1][col8],
            block[2][col8],  block[3][col8],  
            block[4][col8],  block[5][col8],
            block[6][col8],  block[7][col8],
            block[8][col8],  block[9][col8],
            block[10][col8], block[11][col8],
            block[12][col8], block[13][col8],
            block[14][col8], block[15][col8] };

        for ( ; col < cols; ++col) { 
            unsigned int const outrow = col;

            outplane[outrow][outcol16] = __builtin_ia32_pmovmskb128(vReg);
            vReg = (v16qi)__builtin_ia32_pslldi128((v4di)vReg, 1);
        }
    }
}



static void
analyzeBlock(struct pam *   const inpamP,
             bit **         const inrow,
             int            const xdir,
             const bit **   const block,
             unsigned int * const topOfFullBlockP,
             unsigned int * const outcol16P) {

    if (xdir > 0){
        /* Write output columns left to right */
        unsigned int i;
        for (i = 0; i < 16; ++i)
            block[i] = inrow[(i & 0x8) + 7 - (i & 0x7)];
        *topOfFullBlockP = 0;
        *outcol16P = 0;
    } else {
        /* Write output columns right to left */
        *topOfFullBlockP = inpamP->height % 16;

        if (inpamP->height >= 16) {
            unsigned int i;
            for (i = 0; i < 16; ++i)
                block[i]= inrow[((i & 0x8) ^ 0x8) + (i & 0x7)];
            *outcol16P = inpamP->height/16 - 1;
        } else
            *outcol16P = 0;
    }
}



static void
doPartialBlockTop(struct pam * const inpamP,
                  bit **       const inrow,
                  int          const xdir,
                  unsigned int const topOfFullBlock,
                  uint16_t **  const outplane) {
    
    if (topOfFullBlock > 0) {
        unsigned int row;
        for (row = 0; row < topOfFullBlock; ++row)
            pbm_readpbmrow_packed(inpamP->file, inrow[row],
                                  inpamP->width, inpamP->format);

        transpose1to15Bitrows(inpamP->width, inpamP->height,
                              inrow, outplane, xdir);
            /* Transpose partial rows on top of input.  Place on right edge of
               output.
            */ 
    }
}



static void
doFullBlocks(struct pam * const inpamP,
             bit **       const inrow,
             int          const xdir,
             const bit *  const block[16],
             unsigned int const topOfFullBlock,
             unsigned int const initOutcol16,
             uint16_t **  const outplane) {

    unsigned int row;
    unsigned int outcol16;
    unsigned int modrow;
        /* Number of current row within buffer */

    for (row = topOfFullBlock, outcol16 = initOutcol16, modrow = 0;
         row < inpamP->height;
         ++row) {

        pbm_readpbmrow_packed(inpamP->file, inrow[modrow],
                              inpamP->width, inpamP->format);
        ++modrow;
        if (modrow == 16) {
            /* 16 row buffer is full.  Transpose. */
            modrow = 0; 

            transpose16Bitrows(inpamP->width, inpamP->height,
                               block, outplane, outcol16);
            outcol16 += xdir;
        }
    }
}



static void
doPartialBlockBottom(struct pam * const inpamP,
                     bit **       const inrow,
                     int          const xdir,
                     uint16_t **  const outplane) {
    
    if (xdir > 0 && inpamP->height % 16 > 0)
        transpose1to15Bitrows(inpamP->width, inpamP->height, inrow,
                              outplane, xdir);
        /* Transpose partial rows on bottom of input.  Place on right edge of
           output.
        */ 
}



static void
writeOut(struct pam * const outpamP,
         uint16_t **  const outplane,
         int          const ydir) {
             
    unsigned int row;

    for (row = 0; row < outpamP->height; ++row) {
        unsigned int const outrow = (ydir > 0) ?
            row :
            outpamP->height - row - 1;  /* reverse order */
  
        pbm_writepbmrow_packed(stdout, (bit *)outplane[outrow],
                               outpamP->width, 0);
    }
}



void
pamflip_transformRowsToColumnsPbmSse(struct pam *     const inpamP,
                                     struct pam *     const outpamP,
                                     struct xformCore const xformCore) { 
/*----------------------------------------------------------------------------
  This is a specialized routine for row-for-column PBM transformations.
  (-cw, -ccw, -xy).
-----------------------------------------------------------------------------*/
    int const xdir = xformCore.c;
        /* Input top:  output left (+1)/ right (-1)  */
    int const ydir = xformCore.b;
        /* Input left: output top  (+1)/ bottom (-1) */

    bit ** inrow;
    uint16_t ** outplane;
    const bit * block[16];
    unsigned int topOfFullBlock;
    unsigned int outcol16;

    inrow = pbm_allocarray_packed( inpamP->width, 16);
    outplane = (uint16_t **)pbm_allocarray_packed(outpamP->width + 15,
                                                  outpamP->height);
        /* We write to the output array in 16 bit units.  Add margin (15). */  

    analyzeBlock(inpamP, inrow, xdir, block, &topOfFullBlock, &outcol16);

    doPartialBlockTop(inpamP, inrow, xdir, topOfFullBlock, outplane);

    doFullBlocks(inpamP, inrow, xdir, block,
                 topOfFullBlock, outcol16, outplane);

    doPartialBlockBottom(inpamP, inrow, xdir, outplane);

    writeOut(outpamP, outplane, ydir);

    pbm_freearray(outplane, outpamP->height);
    pbm_freearray(inrow, 16);
}
#else  /* SSE functions exist */

void
pamflip_transformRowsToColumnsPbmSse(struct pam *     const inpamP,
                                     struct pam *     const outpamP,
                                     struct xformCore const xformCore) { 

    /* Nobody is supposed to call this */
    assert(false);
}
#endif 
