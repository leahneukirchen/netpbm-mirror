/* libppm1.c - ppm utility library part 1
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

/* See pmfileio.c for the complicated explanation of this 32/64 bit file
   offset stuff.
*/
#define _FILE_OFFSET_BITS 64
#define _LARGE_FILES  

#include <string.h>
#include <stdio.h>
#include <errno.h>

#include "ppm.h"
#include "libppm.h"
#include "pgm.h"
#include "libpgm.h"
#include "pbm.h"
#include "libpbm.h"
#include "pam.h"
#include "libpam.h"
#include "fileio.h"
#include "mallocvar.h"
#include "nstring.h"


pixel *
ppm_allocrow(unsigned int const cols) {

    pixel * pixelrow;

    MALLOCARRAY(pixelrow, cols);

    if (pixelrow == 0)
        pm_error("Unable to allocate space for a %u-column pixel row", cols);

    return pixelrow;
}



void
ppm_init( argcP, argv )
    int* argcP;
    char* argv[];
    {
    pgm_init( argcP, argv );
    }

void
ppm_nextimage(FILE * const fileP, 
              int *  const eofP) {
    pm_nextimage(fileP, eofP);
}



void
ppm_readppminitrest(FILE *   const fileP, 
                    int *    const colsP, 
                    int *    const rowsP, 
                    pixval * const maxvalP) {
    unsigned int maxval;

    /* Read size. */
    *colsP = (int)pm_getuint(fileP);
    *rowsP = (int)pm_getuint(fileP);

    /* Read maxval. */
    maxval = pm_getuint(fileP);
    if (maxval > PPM_OVERALLMAXVAL)
        pm_error("maxval of input image (%u) is too large.  "
                 "The maximum allowed by the PPM is %u.",
                 maxval, PPM_OVERALLMAXVAL); 
    if (maxval == 0)
        pm_error("maxval of input image is zero.");

    *maxvalP = maxval;
}



static void
validateComputableSize(unsigned int const cols,
                       unsigned int const rows) {
/*----------------------------------------------------------------------------
   Validate that the dimensions of the image are such that it can be
   processed in typical ways on this machine without worrying about
   overflows.  Note that in C, arithmetic is always modulus
   arithmetic, so if your values are too big, the result is not what
   you expect.  That failed expectation can be disastrous if you use
   it to allocate memory.

   A common operation is adding 1 or 2 to the highest row or
   column number in the image, so we make sure that's possible.
-----------------------------------------------------------------------------*/
    if (cols > INT_MAX - 2)
        pm_error("image width (%u) too large to be processed", cols);
    if (rows > INT_MAX - 2)
        pm_error("image height (%u) too large to be processed", rows);
}



void
ppm_readppminit(FILE *   const fileP, 
                int *    const colsP, 
                int *    const rowsP, 
                pixval * const maxvalP, 
                int *    const formatP) {

    int realFormat;

    /* Check magic number. */
    realFormat = pm_readmagicnumber(fileP);
    switch (PAM_FORMAT_TYPE(realFormat)) {
    case PPM_TYPE:
        *formatP = realFormat;
        ppm_readppminitrest(fileP, colsP, rowsP, maxvalP);
        break;

    case PGM_TYPE:
        *formatP = realFormat;
        pgm_readpgminitrest(fileP, colsP, rowsP, maxvalP);
        break;

    case PBM_TYPE:
        *formatP = realFormat;
        /* See comment in pgm_readpgminit() about this maxval */
        *maxvalP = PPM_MAXMAXVAL;
        pbm_readpbminitrest(fileP, colsP, rowsP);
        break;

    case PAM_TYPE:
        pnm_readpaminitrestaspnm(fileP, colsP, rowsP, maxvalP, formatP);
        break;

    default:
        pm_error("bad magic number %d - not a PPM, PGM, PBM, or PAM file",
                 PAM_FORMAT_TYPE(*formatP));
    }
    validateComputableSize(*colsP, *rowsP);
}



static void
readPpmRow(FILE *       const fileP, 
           pixel *      const pixelrow, 
           unsigned int const cols, 
           pixval       const maxval, 
           int          const format) {

    unsigned int col;

    for (col = 0; col < cols; ++col) {
        pixval const r = pm_getuint(fileP);
        pixval const g = pm_getuint(fileP);
        pixval const b = pm_getuint(fileP);
        
        if (r > maxval)
            pm_error("Red sample value %u is greater than maxval (%u)",
                     r, maxval);
        if (g > maxval)
            pm_error("Green sample value %u is greater than maxval (%u)",
                     g, maxval);
        if (b > maxval)
            pm_error("Blue sample value %u is greater than maxval (%u)",
                     b, maxval);
        
        PPM_ASSIGN(pixelrow[col], r, g, b);
    }
}



static void
interpRasterRowRaw(const unsigned char * const rowBuffer,
                   pixel *               const pixelrow,
                   unsigned int          const cols,
                   unsigned int          const bytesPerSample) {

    unsigned int bufferCursor;

    bufferCursor = 0;  /* start at beginning of rowBuffer[] */
        
    if (bytesPerSample == 1) {
        unsigned int col;
        for (col = 0; col < cols; ++col) {
            pixval const r = rowBuffer[bufferCursor++];
            pixval const g = rowBuffer[bufferCursor++];
            pixval const b = rowBuffer[bufferCursor++];
            PPM_ASSIGN(pixelrow[col], r, g, b);
        }
    } else  {
        /* two byte samples */
        unsigned int col;
        for (col = 0; col < cols; ++col) {
            pixval r, g, b;
                    
            r = rowBuffer[bufferCursor++] << 8;
            r |= rowBuffer[bufferCursor++];
                    
            g = rowBuffer[bufferCursor++] << 8;
            g |= rowBuffer[bufferCursor++];
                    
            b = rowBuffer[bufferCursor++] << 8;
            b |= rowBuffer[bufferCursor++];
                    
            PPM_ASSIGN(pixelrow[col], r, g, b);
        }
    }
}



static void
readRppmRow(FILE *       const fileP, 
            pixel *      const pixelrow, 
            unsigned int const cols, 
            pixval       const maxval, 
            int          const format) {

    unsigned int const bytesPerSample = maxval < 256 ? 1 : 2;
    unsigned int const bytesPerRow    = cols * 3 * bytesPerSample;
        
    unsigned char * rowBuffer;
    const char * error;
    
    MALLOCARRAY(rowBuffer, bytesPerRow);
        
    if (rowBuffer == NULL)
        asprintfN(&error, "Unable to allocate memory for row buffer "
                  "for %u columns", cols);
    else {
        ssize_t rc;

        rc = fread(rowBuffer, 1, bytesPerRow, fileP);
    
        if (feof(fileP))
            asprintfN(&error, "Unexpected EOF reading row of PPM image.");
        else if (ferror(fileP))
            asprintfN(&error, "Error reading row.  fread() errno=%d (%s)",
                      errno, strerror(errno));
        else {
            size_t const bytesRead = rc;
            if (bytesRead != bytesPerRow)
                asprintfN(&error, "Error reading row.  Short read of %u bytes "
                          "instead of %u", (unsigned)bytesRead, bytesPerRow);
            else {
                interpRasterRowRaw(rowBuffer, pixelrow, cols, bytesPerSample);
                error = NULL;
            }
        }
        free(rowBuffer);
    }
    if (error) {
        pm_errormsg("%s", error);
        strfree(error);
        pm_longjmp();
    }
}



static void
readPgmRow(FILE *       const fileP, 
           pixel *      const pixelrow, 
           unsigned int const cols, 
           pixval       const maxval, 
           int          const format) {

    jmp_buf jmpbuf;
    jmp_buf * origJmpbufP;
    gray * grayrow;

    grayrow = pgm_allocrow(cols);

    if (setjmp(jmpbuf) != 0) {
        pgm_freerow(grayrow);
        pm_setjmpbuf(origJmpbufP);
        pm_longjmp();
    } else {
        unsigned int col;
    
        pm_setjmpbufsave(&jmpbuf, &origJmpbufP);

        pgm_readpgmrow(fileP, grayrow, cols, maxval, format);

        for (col = 0; col < cols; ++col) {
            pixval const g = grayrow[col];
            PPM_ASSIGN(pixelrow[col], g, g, g);
        }
        pm_setjmpbuf(origJmpbufP);
    }
    pgm_freerow(grayrow);
}



static void
readPbmRow(FILE *       const fileP, 
           pixel *      const pixelrow, 
           unsigned int const cols, 
           pixval       const maxval, 
           int          const format) {

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

        pbm_readpbmrow(fileP, bitrow, cols, format);

        for (col = 0; col < cols; ++col) {
            pixval const g = (bitrow[col] == PBM_WHITE) ? maxval : 0;
            PPM_ASSIGN(pixelrow[col], g, g, g);
        }
        pm_setjmpbuf(origJmpbufP);
    }
    pbm_freerow(bitrow);
}



void
ppm_readppmrow(FILE *  const fileP, 
               pixel * const pixelrow, 
               int     const cols, 
               pixval  const maxval, 
               int     const format) {

    switch (format) {
    case PPM_FORMAT:
        readPpmRow(fileP, pixelrow, cols, maxval, format);
        break;

    /* For PAM, we require a depth of 3, which means the raster format
       is identical to Raw PPM!  How convenient.
    */
    case PAM_FORMAT:
    case RPPM_FORMAT:
        readRppmRow(fileP, pixelrow, cols, maxval, format);
        break;

    case PGM_FORMAT:
    case RPGM_FORMAT:
        readPgmRow(fileP, pixelrow, cols, maxval, format);
        break;

    case PBM_FORMAT:
    case RPBM_FORMAT:
        readPbmRow(fileP, pixelrow, cols, maxval, format);
        break;

    default:
        pm_error("Invalid format code");
    }
}



pixel**
ppm_readppm(FILE *   const fileP, 
            int *    const colsP, 
            int *    const rowsP, 
            pixval * const maxvalP) {

    jmp_buf jmpbuf;
    jmp_buf * origJmpbufP;
    pixel ** pixels;
    int cols, rows;
    pixval maxval;
    int format;

    ppm_readppminit(fileP, &cols, &rows, &maxval, &format);

    pixels = ppm_allocarray(cols, rows);

    if (setjmp(jmpbuf) != 0) {
        ppm_freearray(pixels, rows);
        pm_setjmpbuf(origJmpbufP);
        pm_longjmp();
    } else {
        unsigned int row;

        pm_setjmpbufsave(&jmpbuf, &origJmpbufP);

        for (row = 0; row < rows; ++row)
            ppm_readppmrow(fileP, pixels[row], cols, maxval, format);

        *colsP = cols;
        *rowsP = rows;
        *maxvalP = maxval;

        pm_setjmpbuf(origJmpbufP);
    }
    return pixels;
}



void
ppm_check(FILE *               const fileP, 
          enum pm_check_type   const check_type, 
          int                  const format, 
          int                  const cols, 
          int                  const rows, 
          pixval               const maxval,
          enum pm_check_code * const retval_p) {

    if (rows < 0)
        pm_error("Invalid number of rows passed to ppm_check(): %d", rows);
    if (cols < 0)
        pm_error("Invalid number of columns passed to ppm_check(): %d", cols);
    
    if (check_type != PM_CHECK_BASIC) {
        if (retval_p) *retval_p = PM_CHECK_UNKNOWN_TYPE;
    } else if (PPM_FORMAT_TYPE(format) == PBM_TYPE) {
        pbm_check(fileP, check_type, format, cols, rows, retval_p);
    } else if (PPM_FORMAT_TYPE(format) == PGM_TYPE) {
        pgm_check(fileP, check_type, format, cols, rows, maxval, retval_p);
    } else if (format != RPPM_FORMAT) {
        if (retval_p) *retval_p = PM_CHECK_UNCHECKABLE;
    } else {        
        pm_filepos const bytes_per_row = cols * 3 * (maxval > 255 ? 2 : 1);
        pm_filepos const need_raster_size = rows * bytes_per_row;
        
        pm_check(fileP, check_type, need_raster_size, retval_p);
    }
}
