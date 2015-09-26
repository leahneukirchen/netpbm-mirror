/* escp2topbm.c - read an Epson ESC/P2 printer file and
**                 create a pbm file from the raster data,
**                 ignoring all other data.
**                 Can be regarded as a simple raster printer emulator
**                 with a RLE run length decoder.
**                 This program was made primarily for the test of pbmtoescp2
**
** Copyright (C) 2003 by Ulrich Walcher (u.walcher@gmx.de)
**                       and Jef Poskanzer.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.

** Major changes were made in July 2015 by Akira Urushibata.
** Most notably the output functions were rewritten.
** The -plain option is honored as in other programs.

*   Implementation note (July 2015)
*
*   The input file does not have a global header.  Image data is divided
*   into stripes (or data blocks).   Each stripe has a header with
*   local values for width, height, horizontal/vertical resolution
*   and compression mode.
*
*   We calculate the global height by adding up the local (stripe)
*   height values, which may vary.
*
*   The width value in the first stripe is used throughout; if any
*   subsequent stripe reports a different value we abort.
*
*   We ignore the resolution fields.  Changes in compression mode
*   are tolerated; they pose no problem.
*
*   The official manual says resolution changes within an image are
*   not allowed.  It does not mention whether changes in stripe height or
*   width values should be allowed.
*
*   A different implementation approach would be to write temporary
*   PBM files for each stripe and concatenate them at the final stage
*   with a system call to "pnmcat -tb".  This method has the advantage
*   of being capable of handling variations in width.
*/


#include <stdbool.h>

#include "mallocvar.h"
#include "pbm.h"

#define ESC 033



static int
huntEsc(FILE * const ifP) {
/*-----------------------------------------------------------------------------
  Hunt for valid start of image stripe in input.

  Return values:
    ESC: start of image stripe (data block)
    EOF: end of file
    0: any other char or sequence
-----------------------------------------------------------------------------*/
    int const ch1 = getc(ifP);

    switch (ch1) {
    case EOF: return EOF;
    case ESC: {
        int const ch2 = getc(ifP);

        switch (ch2) {
        case EOF: return EOF;
        case '.': return ESC;
        default:  return 0;
        }
    } break;
    default: return 0;
    }
}



static unsigned char
readChar(FILE * const ifP) {

    int const ch = getc(ifP);

    if (ch == EOF)
        pm_error("EOF encountered while reading image data.");

    return (unsigned char) ch;
}



static void       
readStripeHeader(unsigned int * const widthThisStripeP,
                 unsigned int * const rowsThisStripeP,
                 unsigned int * const compressionP,
                 FILE *         const ifP) {

    unsigned char stripeHeader[6];
    unsigned int widthThisStripe, rowsThisStripe;
    unsigned int compression;

    if (fread(stripeHeader, sizeof(stripeHeader), 1, ifP) != 1)
        pm_error("Error reading image data.");

    compression     = stripeHeader[0];
    /* verticalResolution   = stripeHeader[1]; */
    /* horizontalResolution = stripeHeader[2]; */
    rowsThisStripe  = stripeHeader[3];  
    widthThisStripe = stripeHeader[5] * 256 + stripeHeader[4];

    if (widthThisStripe == 0 || rowsThisStripe == 0)
        pm_error("Error: Abnormal value in data block header:  "
                 "Says stripe has zero width or height");

    if (compression != 0 && compression != 1)
        pm_error("Error: Unknown compression mode %u", compression);

    *widthThisStripeP = widthThisStripe;
    *rowsThisStripeP  = rowsThisStripe;
    *compressionP     = compression;
}



/* RLE decoder */
static void
decEpsonRLE(unsigned int    const blockSize, 
            unsigned char * const outBuffer,
            FILE *          const ifP) {

    unsigned int dpos;

    for (dpos = 0; dpos < blockSize; ) {
        unsigned char const flag = readChar(ifP);

        if (flag < 128) {
            /* copy through */

            unsigned int const nonrunLength = flag + 1;

            unsigned int i;

            for (i = 0; i < nonrunLength; ++i)
                outBuffer[dpos+i] = readChar(ifP);

            dpos += nonrunLength;
        } else if (flag == 128) {
            pm_message("Code 128 detected in compressed input data: ignored");
        } else {
            /* inflate this run */

            unsigned int const runLength = 257 - flag;
            unsigned char const repeatChar = readChar( ifP );

            unsigned int i;

            for (i = 0; i < runLength; ++i)
                outBuffer[dpos + i] = repeatChar;  
            dpos += runLength;
        }
    }
    if (dpos != blockSize)
      pm_error("Corruption detected in compressed input data");
}



static void
processStripeRaster(unsigned char ** const bitarray,
                    unsigned int     const rowsThisStripe,
                    unsigned int     const width,
                    unsigned int     const compression,
                    FILE *           const ifP,
                    unsigned int *   const rowIdxP) {
         
    unsigned int const initialRowIdx = *rowIdxP;
    unsigned int const widthInBytes = pbm_packed_bytes(width);
    unsigned int const blockSize = rowsThisStripe * widthInBytes;
    unsigned int const margin = compression==1 ? 256 : 0;

    unsigned char * imageBuffer;
    unsigned int i;
    unsigned int rowIdx;

    /* We allocate a new buffer each time this function is called.  Add some
       margin to the buffer for compressed mode to cope with overruns caused
       by corrupt input data.
    */

    MALLOCARRAY(imageBuffer, blockSize + margin);

    if (imageBuffer == NULL)
        pm_error("Failed to allocate buffer for a block of size %u",
                 blockSize);

    if (compression == 0) {
        if (fread(imageBuffer, blockSize, 1, ifP) != 1)
            pm_error("Error reading image data");
    } else /* compression == 1 */
        decEpsonRLE(blockSize, imageBuffer, ifP);

    /* Hand over image data to output by pointer assignment */
    for (i = 0, rowIdx = initialRowIdx; i < rowsThisStripe; ++i)
        bitarray[rowIdx++] = &imageBuffer[i * widthInBytes];

    *rowIdxP = rowIdx;
}



static void
expandBitarray(unsigned char *** const bitarrayP,
               unsigned int   *  const bitarraySizeP) {

    unsigned int const heightIncrement = 5120;
    unsigned int const heightMax = 5120 * 200;
        /* 5120 rows is sufficient for US legal at 360 DPI */

    *bitarraySizeP += heightIncrement;
    if (*bitarraySizeP > heightMax)
        pm_error("Image too tall");
    else
        REALLOCARRAY_NOFAIL(*bitarrayP, *bitarraySizeP); 
}



static void
writePbmImage(unsigned char ** const bitarray,
              unsigned int     const width,
              unsigned int     const height) {

    unsigned int row;

    if (height == 0)
        pm_error("No image");

    pbm_writepbminit(stdout, width, height, 0);
 
    for (row = 0; row < height; ++row) {
        pbm_cleanrowend_packed(bitarray[row], width);
        pbm_writepbmrow_packed(stdout, bitarray[row], width, 0);
    }
}



int
main(int          argc,
     const char * argv[]) {

    FILE * ifP;
    unsigned int width;
        /* Width of the image, or zero to mean width is not yet known.
           (We get the width from the first stripe in the input; until
           we've seen that stripe, we don't know the width)
        */
    unsigned int height;
        /* Height of the image as seen so far.  (We process a stripe at a
           time, increasing this value as we go).
        */
    unsigned int rowIdx;
    unsigned char ** bitarray;
    unsigned int bitarraySize;
    const char * fileName;
    bool eof;

    pm_proginit(&argc, argv);

    if (argc-1 > 1)
        pm_error("Too many arguments (%u).  Only argument is filename.",
                 argc-1);

    if (argc == 2)
        fileName = argv[1];
    else
        fileName = "-";

    ifP = pm_openr(fileName);

    /* Initialize bitarray */
    bitarray = NULL;  bitarraySize = 0;
    expandBitarray(&bitarray, &bitarraySize);

    for (eof = false, width = 0, height = 0, rowIdx = 0; !eof; ) {
        int const r = huntEsc(ifP);

        if (r == EOF)
            eof = true;
        else {
            if (r == ESC) {
                unsigned int compression;
                unsigned int rowsThisStripe;
                unsigned int widthThisStripe;
            
                readStripeHeader(&widthThisStripe, &rowsThisStripe,
                                 &compression, ifP);

                if (rowsThisStripe == 0)
                    pm_error("Abnormal data block height value: 0");
                else if (rowsThisStripe != 24 && rowsThisStripe != 8 &&
                         rowsThisStripe != 1) {
                    /* The official Epson manual says valid values are 1, 8,
                       24 but we just print a warning message and continue if
                       other values are detected.
                    */ 
                    pm_message("Abnormal data block height value: %u "
                               "(ignoring)",
                               rowsThisStripe);
                }
                if (width == 0) {
                    /* Get width from 1st stripe header */
                    width = widthThisStripe;
                } else if (width != widthThisStripe) {
                    /* width change not allowed */
                    pm_error("Error: Width changed in middle of image "
                             "from %u to %u",
                             width, widthThisStripe);
                }
                height += rowsThisStripe;
                if (height > bitarraySize)
                    expandBitarray(&bitarray, &bitarraySize);

                processStripeRaster(bitarray, rowsThisStripe, width,
                                    compression, ifP, &rowIdx);
            } else {
                /* r != ESC; do nothing */
            }
        }
    }

    writePbmImage(bitarray, width, height);

    fclose(stdout);
    fclose(ifP);

    return 0;
}



