/* pbmtomrf - convert pbm to mrf
 * public domain by RJM
 *
 * Adapted to Netpbm by Bryan Henderson 2003.08.09.  Bryan got his copy from
 * ftp://ibiblio.org/pub/linux/apps/convert, dated 1998.03.03.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>

#include "pm_c_util.h"
#include "pbm.h"



typedef struct bitOut {
    int bitbox;
    int bitsleft;
    FILE * fileP;
} bitOut;



static void 
bit_init(struct bitOut * const bitOutP,
         FILE *          const ofP) {

    bitOutP->bitbox = 0; 
    bitOutP->bitsleft = 8;
    bitOutP->fileP = ofP;
}



static void 
bit_output(struct bitOut * const bitOutP,
           int             const bit) {

    --bitOutP->bitsleft;
    bitOutP->bitbox |= (bit << bitOutP->bitsleft);
    if (bitOutP->bitsleft == 0) {
        fputc(bitOutP->bitbox, bitOutP->fileP);
        bitOutP->bitbox = 0;
        bitOutP->bitsleft = 8;
    }
}



static void 
bit_flush(struct bitOut * const bitOutP) {
    /* there are never 0 bits left outside of bit_output, but
     * if 8 bits are left here there's nothing to flush, so
     * only do it if bitsleft!=8.
     */
    if (bitOutP->bitsleft != 8) {
        bitOutP->bitsleft = 1;
        bit_output(bitOutP, 0);    /* yes, really. This will always work. */
    }
}



static void
determineBlackWhiteOrMix(const unsigned char * const image,
                         unsigned int          const ulCol,
                         unsigned int          const ulRow,
                         unsigned int          const imageWidth,
                         unsigned int          const size,
                         bool *                const oneColorP,
                         int *                 const colorP) {
/*----------------------------------------------------------------------------
   Determine whether a square within 'image' is all white, all black,
   or a mix.
-----------------------------------------------------------------------------*/
    unsigned int rowOfSquare;
    unsigned int t;

    for (rowOfSquare = 0, t = 0; rowOfSquare < size; ++rowOfSquare) {
        unsigned int colOfSquare;
        for (colOfSquare = 0; colOfSquare < size; ++colOfSquare) {
            unsigned int rowOfImage = ulRow + rowOfSquare;
            unsigned int colOfImage = ulCol + colOfSquare;

            t += image[rowOfImage * imageWidth + colOfImage];
        }
    }        
    /* if the total's 0, it's black. if it's size*size, it's white. */
    if (t == 0) {
        *oneColorP = TRUE;
        *colorP = 0;
    } else if (t == SQR(size)) {
        *oneColorP = TRUE;
        *colorP = 1;
    } else
        *oneColorP = FALSE;
}



static void 
doSquare(bitOut *              const bitOutP,
         const unsigned char * const image,
         unsigned int          const ulCol,
         unsigned int          const ulRow,
         unsigned int          const imageWidth,
         unsigned int          const size) {
/*----------------------------------------------------------------------------
   Do a square of side 'size', whose upper left corner is at (ulCol, ulRow).
   This is a square within 'image', which is a concatenation of rows
   'imageWidth' pixels wide, one byte per pixel.

   Write the pixel values out to the bit stream *bitOutP, in MRF format.
-----------------------------------------------------------------------------*/
    if (size == 1) {
        /* The fact that it is all one color is implied because the square is
           just one pixel; no bit goes in MRF output to state that.
        */
        bit_output(bitOutP, image[ulRow * imageWidth + ulCol] ? 1 : 0);
    } else {
        bool oneColor;
        int color;

        determineBlackWhiteOrMix(image, ulCol, ulRow, imageWidth, size,
                                 &oneColor, &color);

        if (oneColor) {
            bit_output(bitOutP, 1);  /* all same color */
            bit_output(bitOutP, color);
        } else {
            /* Square is not all the same color, so recurse.  Do each
               of the four quadrants of this square individually.
            */
            unsigned int const quadSize = size/2;

            bit_output(bitOutP, 0);    /* not all same color */

            doSquare(bitOutP, image, ulCol,            ulRow,
                     imageWidth, quadSize);
            doSquare(bitOutP, image, ulCol + quadSize, ulRow,
                     imageWidth, quadSize);
            doSquare(bitOutP, image, ulCol,            ulRow + quadSize,
                     imageWidth, quadSize);
            doSquare(bitOutP, image, ulCol + quadSize, ulRow + quadSize,
                     imageWidth, quadSize);
        }
    }
}
    


static void
fiddleRightEdge(unsigned char * const image,
                unsigned int    const w,
                unsigned int    const h,
                unsigned int    const pw,
                bool *          const flippedP) {

    unsigned int row;
    unsigned int t;

    for (row = t = 0; row < h; ++row)
        t += image[row*pw + w - 1];

    if (t*2 > h) {
        unsigned int row;

        *flippedP = TRUE;
        for (row = 0; row < h; ++row) {
            unsigned int col;
            for (col = w; col < pw; ++col)
                image[row*pw + col] = 1;
        }
    } else
        *flippedP = FALSE;
}



static void
fiddleBottomEdge(unsigned char * const image,
                 unsigned int    const w,
                 unsigned int    const h,
                 unsigned int    const pw,
                 unsigned int    const ph,
                 bool *          const flippedP) {
    
    unsigned int col;
    unsigned int t;

    for (col = t = 0; col < w; ++col)
        t += image[(h-1)*pw + col];

    if (t*2 > w) {
        unsigned int row;
        *flippedP = TRUE;
        for (row = h; row < ph; ++row) {
            unsigned int col;
            for (col = 0; col < w; ++col)
                image[row*pw + col] = 1;
        }
    } else
        *flippedP = FALSE;
}




static void
fiddleBottomRightCorner(unsigned char * const image,
                        unsigned int    const w,
                        unsigned int    const h,
                        unsigned int    const pw,
                        unsigned int    const ph) {
    unsigned int row;

    for (row = h; row < ph; ++row) {
        unsigned int col;
        
        for (col = w; col < pw; ++col)
                    image[row*pw + col] = 1;
    }
}



static void 
fiddleEdges(unsigned char * const image,
            int             const cols,
            int             const rows) {
/* the aim of this routine is play around with the edges which
 * are compressed into the mrf but thrown away when it's decompressed,
 * such that we get the best compression possible.
 * If you don't see why this is a good idea, consider the simple case
 * of a 1x1 white pixel. Placed on a black 64x64 this takes several bytes
 * to compress. On a white 64x64, it takes two bits.
 * (Clearly most cases will be more complicated, but you should get the
 * basic idea from that.)
 */

    /* there are many possible approaches to this problem, and this one's
         * certainly not the best, but at least it's quick and easy, and it's
         * better than nothing. :-)
         *
         * So, all we do is flip the runoff area of an edge to white
         * if more than half of the pixels on that edge are
         * white. Then for the bottom-right runoff square (if there is
         * one), we flip it if we flipped both edges.  
         */
        
    /* w64 is units-of-64-bits width, h64 same for height */
    unsigned int const w64 = (cols + 63) / 64;
    unsigned int const h64 = (rows + 63) / 64;

    int const pw=w64*64;
    int const ph=h64*64;

    bool flippedRight, flippedBottom;

    if (cols % 64 != 0) 
        fiddleRightEdge(image, cols, rows, pw, &flippedRight);
    else
        flippedRight = FALSE;

    if (rows % 64 != 0) 
        fiddleBottomEdge(image, cols, rows, pw, ph, &flippedBottom);
    else
        flippedBottom = FALSE;

    if (flippedRight && flippedBottom) 
        fiddleBottomRightCorner(image, cols, rows, pw, ph);
}



static void
readPbmImage(FILE *           const ifP, 
             unsigned char ** const imageP,
             int *            const colsP,
             int *            const rowsP) {
    

    /* w64 is units-of-64-bits width, h64 same for height */
    unsigned int w64, h64;

    unsigned char * image;
    int cols, rows, format;
    unsigned int row;
    bit * bitrow;
    
    pbm_readpbminit(ifP, &cols, &rows, &format);

    w64 = (cols + 63) / 64;
    h64 = (rows + 63) / 64;

    if (UINT_MAX/w64/64/h64/64 == 0)
        pm_error("Ridiculously large, unprocessable image: %u cols x %u rows",
                 cols, rows);

    image = calloc(w64*h64*64*64, 1);
    if (image == NULL)
        pm_error("Unable to get memory for raster");
                 
    /* get bytemap image rounded up into mod 64x64 squares */

    bitrow = pbm_allocrow(cols);

    for (row = 0; row < rows; ++row) {
        unsigned int col;

        pbm_readpbmrow(ifP, bitrow, cols, format);

        for (col =0; col < cols; ++col)
            image[row*(w64*64) + col] = (bitrow[col] == PBM_WHITE ? 1 : 0);
    }
    pbm_freerow(bitrow);
    *imageP = image;
    *colsP = cols;
    *rowsP = rows;
}



static void
outputMrf(FILE *          const ofP, 
          unsigned char * const image,
          unsigned int    const cols,
          unsigned int    const rows) {

    unsigned int const w64 = (cols + 63) / 64;
    unsigned int const h64 = (rows + 63) / 64;

    bitOut bitOut;

    unsigned int row;

    fprintf(ofP, "MRF1");
    fprintf(ofP, "%c%c%c%c", cols >> 24, cols >> 16, cols >> 8, cols >> 0);
    fprintf(ofP, "%c%c%c%c", rows >> 24, rows >> 16, rows >> 8, rows >> 0);
    fputc(0, ofP);   /* option byte, unused for now */
    
    /* now recursively check squares. */

    bit_init(&bitOut, ofP);

    for (row = 0; row < h64; ++row) {
        unsigned int col;
        for (col = 0; col < w64; ++col)
            doSquare(&bitOut, image, col*64, row*64, w64*64, 64);
    }
    bit_flush(&bitOut);
}



int 
main(int argc,char *argv[]) {

    FILE * ifP;
    FILE * ofP;
    unsigned char *image;
    int rows, cols;
    
    pbm_init(&argc, argv);

    if (argc-1 > 1)
        pm_error("Too many arguments: %d.  Only argument is input file", 
                 argc-1);

    if (argc-1 == 1)
        ifP = pm_openr(argv[1]);
    else
        ifP = stdin;

    ofP = stdout;
 
    readPbmImage(ifP, &image, &cols, &rows);

    pm_close(ifP);

    /* if necessary, alter the unused outside area to aid compression of
     * edges of image.
     */

    fiddleEdges(image, cols, rows);

    outputMrf(ofP, image, cols, rows);

    free(image);

    return 0;
}




