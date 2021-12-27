/*============================================================================
                                  libpamwrite.c
==============================================================================
   These are the library functions, which belong in the libnetpbm library,
   that deal with writing the PAM (Portable Arbitrary Format) image format
   raster (not the header).

   This file was originally written by Bryan Henderson and is contributed
   to the public domain by him and subsequent authors.
=============================================================================*/

/* See pmfileio.c for the complicated explanation of this 32/64 bit file
   offset stuff.
*/
#define _FILE_OFFSET_BITS 64
#define _LARGE_FILES

#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <assert.h>
#include <math.h>

#include "netpbm/pm_config.h"
#include "netpbm/pm_c_util.h"

#include "pam.h"


static __inline__ unsigned int
samplesPerPlainLine(sample       const maxval,
                    unsigned int const depth,
                    unsigned int const lineLength) {
/*----------------------------------------------------------------------------
   Return the minimum number of samples that should go in a line
   'lineLength' characters long in a plain format non-PBM PNM image
   with depth 'depth' and maxval 'maxval'.

   Note that this number is just for aesthetics; the Netpbm formats allow
   any number of samples per line.
-----------------------------------------------------------------------------*/
    unsigned int const digitsForMaxval = (unsigned int)
        (log(maxval + 0.1 ) / log(10.0));
        /* Number of digits maxval has in decimal */
        /* +0.1 is an adjustment to overcome precision problems */
    unsigned int const fit = lineLength / (digitsForMaxval + 1);
        /* Number of maxval-sized samples that fit in a line */
    unsigned int const retval = (fit > depth) ? (fit - (fit % depth)) : fit;
        /* 'fit', rounded down to a multiple of depth, if possible */

    return retval;
}



static void
writePamPlainPbmRow(const struct pam *  const pamP,
                    const tuple *       const tuplerow) {

    int col;
    unsigned int const samplesPerLine = 70;

    for (col = 0; col < pamP->width; ++col)
        fprintf(pamP->file,
                ((col+1) % samplesPerLine == 0 || col == pamP->width-1)
                    ? "%1u\n" : "%1u",
                tuplerow[col][0] == PAM_PBM_BLACK ? PBM_BLACK : PBM_WHITE);
}



static void
writePamPlainRow(const struct pam *  const pamP,
                 const tuple *       const tuplerow) {

    int const samplesPerLine =
        samplesPerPlainLine(pamP->maxval, pamP->depth, 79);

    int col;
    unsigned int samplesInCurrentLine;
        /* number of samples written from start of line  */

    samplesInCurrentLine = 0;

    for (col = 0; col < pamP->width; ++col) {
        unsigned int plane;
        for (plane = 0; plane < pamP->depth; ++plane){
            fprintf(pamP->file, "%lu ",tuplerow[col][plane]);

            ++samplesInCurrentLine;

            if (samplesInCurrentLine >= samplesPerLine) {
                fprintf(pamP->file, "\n");
                samplesInCurrentLine = 0;
            }
        }
    }
    fprintf(pamP->file, "\n");
}



static void
formatPbm(const struct pam * const pamP,
          const tuple *      const tuplerow,
          unsigned char *    const outbuf,
          unsigned int       const nTuple,
          unsigned int *     const rowSizeP) {
/*----------------------------------------------------------------------------
   Create the image of 'nTuple' consecutive tuples of a row in the raster of a
   raw format PBM image.

   Put the image at *outbuf; put the number of bytes of it at *rowSizeP.
-----------------------------------------------------------------------------*/
    unsigned char accum;
    int col;

    assert(nTuple <= pamP->width);

    accum = 0;  /* initial value */

    for (col=0; col < nTuple; ++col) {
        accum |=
            (tuplerow[col][0] == PAM_PBM_BLACK ? PBM_BLACK : PBM_WHITE)
                << (7-col%8);
        if (col%8 == 7) {
                outbuf[col/8] = accum;
                accum = 0;
        }
    }
    if (nTuple % 8 != 0) {
        unsigned int const lastByteIndex = nTuple/8;
        outbuf[lastByteIndex] = accum;
        *rowSizeP = lastByteIndex + 1;
    } else
        *rowSizeP = nTuple/8;
}



/* Though it is possible to simplify the sampleToBytesN() and
   formatNBpsRow() functions into a single routine that handles all
   sample widths, we value efficiency higher here.  Earlier versions
   of Netpbm (before 10.25) did that, with a loop, and performance
   suffered visibly.
*/

static __inline__ void
sampleToBytes2(unsigned char       buf[2],
               sample        const sampleval) {

    buf[0] = (sampleval >> 8) & 0xff;
    buf[1] = (sampleval >> 0) & 0xff;
}



static __inline__ void
sampleToBytes3(unsigned char       buf[3],
               sample        const sampleval) {

    buf[0] = (sampleval >> 16) & 0xff;
    buf[1] = (sampleval >>  8) & 0xff;
    buf[2] = (sampleval >>  0) & 0xff;
}



static __inline__ void
sampleToBytes4(unsigned char       buf[4],
               sample        const sampleval) {

    buf[0] = (sampleval >> 24 ) & 0xff;
    buf[1] = (sampleval >> 16 ) & 0xff;
    buf[2] = (sampleval >>  8 ) & 0xff;
    buf[3] = (sampleval >>  0 ) & 0xff;
}



static __inline__ void
format1Bps(const struct pam * const pamP,
           const tuple *      const tuplerow,
           unsigned char *    const outbuf,
           unsigned int       const nTuple,
           unsigned int *     const rowSizeP) {
/*----------------------------------------------------------------------------
   Create the image of 'nTuple' consecutive tuples of a row in the raster of a
   raw format Netpbm image that has one byte per sample (ergo not PBM).

   Put the image at *outbuf; put the number of bytes of it at *rowSizeP.
-----------------------------------------------------------------------------*/
    int col;
    unsigned int bufferCursor;

    assert(nTuple <= pamP->width);

    bufferCursor = 0;  /* initial value */

    for (col = 0; col < nTuple; ++col) {
        unsigned int plane;
        for (plane=0; plane < pamP->depth; ++plane)
            outbuf[bufferCursor++] = (unsigned char)tuplerow[col][plane];
    }
    *rowSizeP = nTuple * 1 * pamP->depth;
}



static __inline__ void
format2Bps(const struct pam * const pamP,
           const tuple *      const tuplerow,
           unsigned char *    const outbuf,
           unsigned int       const nTuple,
           unsigned int *     const rowSizeP) {
/*----------------------------------------------------------------------------
  Analogous to format1BpsRow().
-----------------------------------------------------------------------------*/
    unsigned char (* const ob)[2] = (unsigned char (*)[2]) outbuf;

    int col;
    unsigned int bufferCursor;

    assert(nTuple <= pamP->width);

    bufferCursor = 0;  /* initial value */

    for (col=0; col < nTuple; ++col) {
        unsigned int plane;
        for (plane = 0; plane < pamP->depth; ++plane)
            sampleToBytes2(ob[bufferCursor++], tuplerow[col][plane]);
    }

    *rowSizeP = nTuple * 2 * pamP->depth;
}



static __inline__ void
format3Bps(const struct pam * const pamP,
           const tuple *      const tuplerow,
           unsigned char *    const outbuf,
           unsigned int       const nTuple,
           unsigned int *     const rowSizeP) {
/*----------------------------------------------------------------------------
  Analogous to format1BpsRow().
-----------------------------------------------------------------------------*/
    unsigned char (* const ob)[3] = (unsigned char (*)[3]) outbuf;

    int col;
    unsigned int bufferCursor;

    assert(nTuple <= pamP->width);

    bufferCursor = 0;  /* initial value */

    for (col=0; col < nTuple; ++col) {
        unsigned int plane;
        for (plane = 0; plane < pamP->depth; ++plane)
            sampleToBytes3(ob[bufferCursor++], tuplerow[col][plane]);
    }

    *rowSizeP = nTuple * 3 * pamP->depth;
}



static __inline__ void
format4Bps(const struct pam * const pamP,
           const tuple *      const tuplerow,
           unsigned char *    const outbuf,
           unsigned int       const nTuple,
           unsigned int *     const rowSizeP) {
/*----------------------------------------------------------------------------
  Analogous to format1BpsRow().
-----------------------------------------------------------------------------*/
    unsigned char (* const ob)[4] = (unsigned char (*)[4]) outbuf;

    int col;
    unsigned int bufferCursor;

    assert(nTuple <= pamP->width);

    bufferCursor = 0;  /* initial value */

    for (col=0; col < nTuple; ++col) {
        unsigned int plane;
        for (plane = 0; plane < pamP->depth; ++plane)
            sampleToBytes4(ob[bufferCursor++], tuplerow[col][plane]);
    }

    *rowSizeP = nTuple * 4 * pamP->depth;
}



void
pnm_formatpamtuples(const struct pam * const pamP,
                    const tuple *      const tuplerow,
                    unsigned char *    const outbuf,
                    unsigned int       const nTuple,
                    unsigned int *     const rowSizeP) {
/*----------------------------------------------------------------------------   Create the image of 'nTuple' consecutive tuples of a row in the raster of a
   raw (not plain) format Netpbm image, as described by *pamP and tuplerow[].
   Put the image at *outbuf.

   'outbuf' must be the address of space allocated with pnm_allocrowimage().

   We return as *rowSizeP the number of bytes in the image.
-----------------------------------------------------------------------------*/
    if (nTuple > pamP->width) {
        pm_error("pnm_formatpamtuples called to write more tuples (%u) "
                 "than the width of a row (%u)",
                 nTuple, pamP->width);
    }

    if (PAM_FORMAT_TYPE(pamP->format) == PBM_TYPE)
        formatPbm(pamP, tuplerow, outbuf, nTuple, rowSizeP);
    else {
        switch(pamP->bytes_per_sample){
        case 1: format1Bps(pamP, tuplerow, outbuf, nTuple, rowSizeP); break;
        case 2: format2Bps(pamP, tuplerow, outbuf, nTuple, rowSizeP); break;
        case 3: format3Bps(pamP, tuplerow, outbuf, nTuple, rowSizeP); break;
        case 4: format4Bps(pamP, tuplerow, outbuf, nTuple, rowSizeP); break;
        default:
            pm_error("invalid bytes per sample passed to "
                     "pnm_formatpamrow(): %u",  pamP->bytes_per_sample);
        }
    }
}



void
pnm_formatpamrow(const struct pam * const pamP,
                 const tuple *      const tuplerow,
                 unsigned char *    const outbuf,
                 unsigned int *     const rowSizeP) {
/*----------------------------------------------------------------------------
  Same as 'pnm_formatpamtuples', except formats an entire row.
-----------------------------------------------------------------------------*/
    pnm_formatpamtuples(pamP, tuplerow, outbuf, pamP->width, rowSizeP);
}



static void
writePamRawRow(const struct pam * const pamP,
               const tuple *      const tuplerow,
               unsigned int       const count) {
/*----------------------------------------------------------------------------
   Write multiple ('count') copies of the same row ('tuplerow') to the file,
   in raw (not plain) format.
-----------------------------------------------------------------------------*/
    jmp_buf jmpbuf;
    jmp_buf * origJmpbufP;
    unsigned int rowImageSize;
    unsigned char * outbuf;  /* malloc'ed */

    outbuf = pnm_allocrowimage(pamP);

    pnm_formatpamrow(pamP, tuplerow, outbuf, &rowImageSize);

    if (setjmp(jmpbuf) != 0) {
        pnm_freerowimage(outbuf);
        pm_setjmpbuf(origJmpbufP);
        pm_longjmp();
    } else {
        unsigned int i;

        pm_setjmpbufsave(&jmpbuf, &origJmpbufP);

        for (i = 0; i < count; ++i) {
            size_t bytesWritten;

            bytesWritten = fwrite(outbuf, 1, rowImageSize, pamP->file);
            if (bytesWritten != rowImageSize)
                pm_error("fwrite() failed to write an image row to the file.  "
                         "errno=%d (%s)", errno, strerror(errno));
        }
        pm_setjmpbuf(origJmpbufP);
    }
    pnm_freerowimage(outbuf);
}



void
pnm_writepamrow(const struct pam * const pamP,
                const tuple *      const tuplerow) {

    /* For speed, we don't check any of the inputs for consistency
       here (unless it's necessary to avoid crashing).  Any consistency
       checking should have been done by a prior call to
       pnm_writepaminit().
    */

    if (pamP->format == PAM_FORMAT || !(pm_plain_output || pamP->plainformat))
        writePamRawRow(pamP, tuplerow, 1);
    else {
        switch (PAM_FORMAT_TYPE(pamP->format)) {
        case PBM_TYPE:
            writePamPlainPbmRow(pamP, tuplerow);
            break;
        case PGM_TYPE:
        case PPM_TYPE:
            writePamPlainRow(pamP, tuplerow);
            break;
        case PAM_TYPE:
            assert(false);
            break;
        default:
            pm_error("Invalid 'format' value %u in pam structure",
                     pamP->format);
        }
    }
}



void
pnm_writepamrowmult(const struct pam * const pamP,
                    const tuple *      const tuplerow,
                    unsigned int       const count) {
/*----------------------------------------------------------------------------
   Write multiple ('count') copies of the same row ('tuplerow') to the file.
-----------------------------------------------------------------------------*/
   if (pm_plain_output || pamP->plainformat) {
       unsigned int i;
       for (i = 0; i < count; ++i)
           pnm_writepamrow(pamP, tuplerow);
   } else
       /* Simple common case - use fastpath */
       writePamRawRow(pamP, tuplerow, count);
}



void
pnm_writepamrowpart(const struct pam * const pamP,
                    const tuple *      const tuplerow,
                    unsigned int       const firstRow,
                    unsigned int       const firstCol,
                    unsigned int       const rowCt,
                    unsigned int       const colCt) {
/*----------------------------------------------------------------------------
   Write part of multiple consecutive rows to the file.

   For each of 'rowCt' consecutive rows starting at 'firstRow', write the
   'colCt' columns starting at 'firstCol'.  The tuples to write are those in
   'tuplerow', starting at the beginning of 'tuplerow'.

   Fail if the file is not seekable (or not known to be seekable) or the
   output format is not raw (i.e. is plain) or the output format is PBM.
-----------------------------------------------------------------------------*/
    unsigned int const bytesPerTuple = pamP->depth * pamP->bytes_per_sample;

    jmp_buf jmpbuf;
    jmp_buf * origJmpbufP;
    unsigned int tupleImageSize;
    unsigned char * outbuf;  /* malloc'ed */

    if (pamP->len < PAM_STRUCT_SIZE(raster_pos) || !pamP->raster_pos)
        pm_error("pnm_writepamrowpart called on nonseekable file");

    if (PAM_FORMAT_TYPE(pamP->format) == PBM_TYPE)
        pm_error("pnm_witepamrowpart called for PBM image");

    if (pm_plain_output || pamP->plainformat)
        pm_error("pnm_writepamrowpart called for plain format image");

    outbuf = pnm_allocrowimage(pamP);

    pnm_formatpamtuples(pamP, tuplerow, outbuf, colCt, &tupleImageSize);

    if (setjmp(jmpbuf) != 0) {
        pnm_freerowimage(outbuf);
        pm_setjmpbuf(origJmpbufP);
        pm_longjmp();
    } else {
        unsigned int row;

        pm_setjmpbufsave(&jmpbuf, &origJmpbufP);

        for (row = firstRow; row < firstRow + rowCt; ++row) {
            pm_filepos const firstTuplePos =
                pamP->raster_pos +
                (row * pamP->width + firstCol) * bytesPerTuple;
            size_t bytesWritten;

            pm_seek2(pamP->file, &firstTuplePos, sizeof(firstTuplePos));

            bytesWritten = fwrite(outbuf, 1, tupleImageSize, pamP->file);

            if (bytesWritten != tupleImageSize)
                pm_error("fwrite() failed to write %u image tuples "
                         "to the file.  errno=%d (%s)",
                         colCt, errno, strerror(errno));
        }
        pm_setjmpbuf(origJmpbufP);
    }
    pnm_freerowimage(outbuf);
}



void
pnm_writepam(struct pam * const pamP,
             tuple **     const tuplearray) {

    int row;

    pnm_writepaminit(pamP);

    for (row = 0; row < pamP->height; ++row)
        pnm_writepamrow(pamP, tuplearray[row]);
}


