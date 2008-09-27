/* libpbm1.c - pbm utility library part 1
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

/* See pmfileio.c for the complicated explanation of this 32/64 bit file
   offset stuff.
*/
#define _FILE_OFFSET_BITS 64
#define _LARGE_FILES  

#include <stdio.h>

#include "mallocvar.h"
#include "shhopt.h"
#include "pbm.h"



bit *
pbm_allocrow(unsigned int const cols) {

    bit * bitrow;

    MALLOCARRAY(bitrow, cols);

    if (bitrow == NULL)
        pm_error("Unable to allocate space for a %u-column bit row", cols);

    return bitrow;
}



void
pbm_init(int *   const argcP,
         char ** const argv) {

    pm_proginit(argcP, (const char **)argv);
}



void
pbm_nextimage(FILE *file, int * const eofP) {
    pm_nextimage(file, eofP);
}



void
pbm_check(FILE * file, const enum pm_check_type check_type, 
          const int format, const int cols, const int rows,
          enum pm_check_code * const retval_p) {

    if (rows < 0)
        pm_error("Invalid number of rows passed to pbm_check(): %d", rows);
    if (cols < 0)
        pm_error("Invalid number of columns passed to pbm_check(): %d", cols);
    
    if (check_type != PM_CHECK_BASIC) {
        if (retval_p) *retval_p = PM_CHECK_UNKNOWN_TYPE;
    } else if (format != RPBM_FORMAT) {
        if (retval_p) *retval_p = PM_CHECK_UNCHECKABLE;
    } else {        
        pm_filepos const bytes_per_row = (cols+7)/8;
        pm_filepos const need_raster_size = rows * bytes_per_row;
#ifdef LARGEFILEDEBUG
        pm_message("pm_filepos passed to pm_check() is %u bytes",
                   sizeof(pm_filepos));
#endif
        pm_check(file, check_type, need_raster_size, retval_p);
    }
}



static unsigned int
bitpop8(unsigned char const x) {
/*----------------------------------------------------------------------------
   Return the number of 1 bits in 'x'
-----------------------------------------------------------------------------*/
static unsigned int const p[256] = {
    0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4,
    1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
    1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
    1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
    1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
    4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8 };

    return p[x];
}



static int
bitpop(const unsigned char * const packedRow,
       unsigned int          const cols) {
/*----------------------------------------------------------------------------
  Return the number of 1 bits in 'packedRow'.
-----------------------------------------------------------------------------*/
    unsigned int const colByteCnt  = pbm_packed_bytes(cols);
    unsigned int const fullByteCnt = cols/8;

    unsigned int i;
    unsigned int sum;

    sum = 0;  /* initial value */

    for (i = 0; i < fullByteCnt; ++i)
        sum += bitpop8(packedRow[i]);

    if (colByteCnt > fullByteCnt)
        sum += bitpop8(packedRow[i] >> (8-cols%8));

    return sum;
}



bit
pbm_backgroundbitrow(unsigned const char * const packedBits,
                     unsigned int          const cols,
                     unsigned int          const offset) {
/*----------------------------------------------------------------------------
  PBM version of pnm_backgroundxelrow() with additional offset parameter.
  When offset == 0, produces the same return value as does
  pnm_backgroundxelrow(promoted_bitrow, cols, ...)
-----------------------------------------------------------------------------*/
    const unsigned char * const row = &packedBits[offset/8];
    unsigned int const rs = offset % 8;
    unsigned int const last = pbm_packed_bytes(cols + rs) - 1;

    unsigned int retval;

    unsigned int firstbit, lastbit;
    unsigned int totalBitpop, headBitpop;

    firstbit = (row[0] >> (7-rs)) & 0x01;
    lastbit  = (row[last] >> (7- (cols+rs-1)%8)) & 0x01;

    if (firstbit == lastbit)
        retval = firstbit;
    else {
        totalBitpop = bitpop(row, cols + rs);
        headBitpop  = (rs == 0) ? 0 : bitpop(row, rs);

        if (totalBitpop - headBitpop >= cols/2)
            retval = PBM_BLACK;
        else
            retval = PBM_WHITE;
    }
    return retval;
}
