/*=============================================================================
                                  pbmtomacp
===============================================================================
  Read a PBM file and produce a MacPaint bitmap file

  Copyright (C) 2015 by Akira Urushibata ("douso").

  Replacement of a previous program of the same name written in 1988
  by Douwe van der Schaaf (...!mcvax!uvapsy!vdschaaf).

  Permission to use, copy, modify, and distribute this software and its
  documentation for any purpose and without fee is hereby granted, provided
  that the above copyright notice appear in all copies and that both that
  copyright notice and this permission notice appear in supporting
  documentation.  This software is provided "as is" without express or implied
  warranty.
=============================================================================*/

/*

  Implemention notes

  Header size is 512 bytes.  There is no MacBinary header.

  White margin which is added for input files with small dimensions
  is treated separately from the active image raster.  The margins
  are directly coded based on the number of rows/columns.

  Output file size never exceeds 53072 bytes.  When -norle is specified,
  output is always 53072 bytes.  It is conceivable that decoders which
  examine the size of Macpaint files (for general validation or for
  determination of header type and size) do exist.

  The uncompressed output (-norle case) fully conforms to Macpaint
  specifications.  No special treatment by the decoder is required.
*/

#include <assert.h>

#include "pm_c_util.h"
#include "pbm.h"
#include "shhopt.h"
#include "mallocvar.h"
#include "macp.h"

#define MIN3(a,b,c)     (MIN((MIN((a),(b))),(c)))

struct cmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFilespec;  /* Filespec of input file */
    unsigned int left;
    unsigned int right;
    unsigned int top;
    unsigned int bottom;
    unsigned int leftSpec;
    unsigned int rightSpec;
    unsigned int topSpec;
    unsigned int bottomSpec;
    bool         norle;         /* If true do not pack data */
};



static void
parseCommandLine(int                        argc,
                 char              ** const argv,
                 struct cmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   Parse program command line described in Unix standard form by argc
   and argv.  Return the information in the options as *cmdlineP.
-----------------------------------------------------------------------------*/
    optEntry * option_def;  /* malloc'ed */
        /* Instructions to OptParseOptions3 on how to parse our options.  */
    optStruct3 opt;

    unsigned int norleSpec;

    unsigned int option_def_index;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENTRY */
    OPTENT3(0, "left",     OPT_UINT,  &cmdlineP->left,
            &cmdlineP->leftSpec,     0);
    OPTENT3(0, "right",    OPT_UINT,  &cmdlineP->right,
            &cmdlineP->rightSpec,    0);
    OPTENT3(0, "top",      OPT_UINT,  &cmdlineP->top,
            &cmdlineP->topSpec,      0);
    OPTENT3(0, "bottom",   OPT_UINT,  &cmdlineP->bottom,
            &cmdlineP->bottomSpec,   0);
    OPTENT3(0, "norle", OPT_FLAG,  NULL,
            &norleSpec, 0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We have no parms that are negative numbers */

    pm_optParseOptions3(&argc, argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    free(option_def);

    cmdlineP->norle = norleSpec;

    if (argc-1 == 0)
        cmdlineP->inputFilespec = "-";
    else if (argc-1 != 1)
        pm_error("Program takes zero or one argument (filename).  You "
                 "specified %d", argc-1);
    else
        cmdlineP->inputFilespec = argv[1];
}



struct CropPadDimensions {
    unsigned int imageWidth;   /* Active image content */
    unsigned int imageHeight;
    unsigned int leftCrop;     /* Cols cropped off from input */
    unsigned int topCrop;      /* Rows cropped off from input */
    unsigned int topMargin;    /* White padding for output */
    unsigned int bottomMargin;
    unsigned int leftMargin;
};



static void
calculateCropPad(struct cmdlineInfo       const cmdline,
                 struct CropPadDimensions     * cropPad,
                 int                      const cols,
                 int                      const rows ) {
/*--------------------------------------------------------------------------
Validate -left -right -top -bottom from command line.  Determine
what rows, columns to take from input if any of these are specified.

Center image if it is smaller than the fixed Macpaint format size.
----------------------------------------------------------------------------*/
    int right, bottom, width, height;
    int const left = cmdline.leftSpec ? cmdline.left : 0;
    int const top  = cmdline.topSpec  ? cmdline.top  : 0;

    if( cmdline.leftSpec ) {
        if(cmdline.rightSpec && left >= cmdline.right )
            pm_error("-left value must be smaller than -right value");
        else if( left > cols -1 )
            pm_error("Specified -left value is beyond right edge "
                     "of input image");
    }
    if( cmdline.topSpec ) {
        if(cmdline.bottomSpec && top >= cmdline.bottom )
            pm_error("-top value must be smaller than -bottom value");
        else if( top > rows -1 )
            pm_error("Specified -top value is beyond bottom edge "
                     "of input image");
    }

    if( cmdline.rightSpec ) {
        if( cmdline.right > cols -1 )
            pm_message("Specified -right value %d is beyond edge of "
                       "input image", cmdline.right);

            right = MIN3( cmdline.right, cols - 1, left + MACP_COLS - 1 );
    }
    else
        right = MIN( cols - 1,  left + MACP_COLS - 1 );

    if( cmdline.bottomSpec ) {
        if( cmdline.bottom > rows -1 )
          pm_message("Specified -bottom value %d is beyond edge of "
                     "input image", cmdline.bottom);

            bottom = MIN3( cmdline.bottom, rows - 1, top + MACP_ROWS - 1);
    }
    else
        bottom = MIN( rows - 1, top + MACP_ROWS - 1 );

    cropPad->leftCrop = left;
    cropPad->topCrop  = top;

    width = right - left + 1;
    cropPad->leftMargin  = ( MACP_COLS - width ) / 2;

    assert(width > 0 && width <= MACP_COLS);
    if(width < cols)
        pm_message("%d of %d input columns will be output", width, cols);

    height = bottom - top + 1;
    cropPad->topMargin    = ( MACP_ROWS - height ) / 2;
    cropPad->bottomMargin = cropPad->topMargin + height - 1;

    assert(height > 0 && height <= MACP_ROWS);
    if(height < rows)
        pm_message("%d out of %d input rows will be output", height, rows);

    cropPad->imageWidth  = width;
    cropPad->imageHeight = height;

}



static void
writeMacpHeader( ) {

    int i;
    char const ch = 0x00;    /* header contains nothing */

    for(i = 0; i < MACP_HEAD_LEN; i++ )
        fputc( ch, stdout );
}



static void
writeMacpRowUnpacked( unsigned int const leftMarginChars,
                      const bit  * const imageBits,
                      unsigned int const imageColChars) {
/*--------------------------------------------------------------------------
Encode (without compression) and output one row.
The row comes divided into three parts: left margin, image, right margin.
----------------------------------------------------------------------------*/
    int i;
    char const marginByte = 0x00;  /* White bits for margin */
    unsigned int const rightMarginChars =
                       MACP_COLCHARS - leftMarginChars - imageColChars;

    fputc( MACP_COLCHARS - 1, stdout );

    for(i = 0; i < leftMarginChars; ++i)
        fputc( marginByte, stdout );

    if(imageColChars > 0)
        fwrite(imageBits, 1, imageColChars, stdout);

    for(i = 0; i < rightMarginChars; ++i)
        fputc( marginByte, stdout );
}



static void
writeMacpRowPacked( unsigned int const leftMarginChars,
                    const bit  * const packedBits,
                    unsigned int const imageColChars,
                    unsigned int const rightMarginChars) {
/*--------------------------------------------------------------------------
Encode and output one row.
As in the unpacked case, the row comes divided into three parts:
left margin, image, right margin.  Unlike the unpacked case we need to
know both the size of the packed data and the size of the right margin.
----------------------------------------------------------------------------*/
    char const marginByte = 0x00;  /* White bits for margin */

    if( leftMarginChars > 0 ) {
        fputc( 257 - leftMarginChars, stdout );
        fputc( marginByte, stdout );
    }

    if( imageColChars > 0)
        fwrite( packedBits, 1, imageColChars, stdout);

    if( rightMarginChars > 0 ) {
        fputc( 257 - rightMarginChars, stdout );
        fputc( marginByte, stdout );
    }
}



static void
packit (const bit *     const sourceBits,
        unsigned int    const imageColChars,
        unsigned char * const packedBits,
        unsigned int  * const packedImageLengthP ) {
/*--------------------------------------------------------------------------
Compress according to packbits algorithm, a byte-level run-length
encoding scheme.

Each row is encoded separately.

The following code does not produce optimum output when there are 2-byte
long sequences between longer ones: the 2-byte run in between does not
get packed, using up 3 bytes where 2 would do.
----------------------------------------------------------------------------*/
#define EQUAL           1
#define UNEQUAL         0

    int charcount, newcount, packcount;
    bool status;
    bit * count;
    bit save;

    packcount = charcount = 0;  /* Initial values */
    status = EQUAL;
    while( charcount < imageColChars ) {
        save = sourceBits[charcount++];
        newcount = 1;
        while( charcount < imageColChars && sourceBits[charcount] == save ) {
            charcount++;
            newcount++;
        }
        if( newcount > 2 ) {
             count = (unsigned char *) &packedBits[packcount++];
             *count = 257 - newcount;
             packedBits[packcount++] = save;
             status = EQUAL;
        } else {
             if( status == EQUAL ) {
                  count = (unsigned char *) &packedBits[packcount++];
                  *count = newcount - 1;
             } else
                  *count += newcount;

             for( ; newcount > 0; newcount-- ) {
                 packedBits[packcount++] = save;
             }
             status = UNEQUAL;
        }
    }
    *packedImageLengthP = packcount;
}



static void
writeMacpRow( unsigned int const leftMarginChars,
              bit        * const imageBits,
              unsigned int const imageColChars,
              bool         const norle) {
/*--------------------------------------------------------------------------
Determine whether a row should be packed (compressed) or not.

If packing leads to unnecessary bloat, discard the packed data and write
in unpacked mode.
----------------------------------------------------------------------------*/
  if (norle)
    writeMacpRowUnpacked( leftMarginChars, imageBits, imageColChars );

  else {
    unsigned int packedImageLength;
    unsigned int const rightMarginChars =
        MACP_COLCHARS - leftMarginChars - imageColChars;
    unsigned char * const packedBits = malloc(MACP_COLCHARS+1);
    if(packedBits == NULL)
        pm_error("Out of memory");

    packit( imageBits, imageColChars, packedBits, &packedImageLength );
    /* Check if we are we better off with compression.
       If not, send row unpacked.  See note at top of file.
    */
    if ( packedImageLength + (!!(leftMarginChars  > 0)) *2 +
         (!!(rightMarginChars > 0)) *2 < MACP_COLCHARS )
        writeMacpRowPacked( leftMarginChars, packedBits,
                            packedImageLength, rightMarginChars);
    else /* Extremely rare */
        writeMacpRowUnpacked( leftMarginChars, imageBits, imageColChars );

    free( packedBits );
  }
}



static void
encodeRowsWithShift(bit              * const bitrow,
                    FILE             * const ifP,
                    int                const inCols,
                    unsigned int       const format,
                    bool               const norle,
                    struct CropPadDimensions const cropPad ) {
/*--------------------------------------------------------------------------
Shift input rows to put only specified columns to output.
Add padding on left and right if necessary.

No shift if the input image is the exact size (576 columns) of the Macpaint
format.  If the input image is too wide and -left was not specified, extra
content on the right is discarded.
----------------------------------------------------------------------------*/
    unsigned int row;

    int const offset     = (cropPad.leftMargin + 8 - cropPad.leftCrop % 8) % 8;
    int const leftTrim   = cropPad.leftMargin % 8;
    int const rightTrim  = ( 8 - (leftTrim + cropPad.imageWidth) % 8 ) % 8;
    int const startChar  = (cropPad.leftCrop + offset) / 8;
    int const imageChars = pbm_packed_bytes(leftTrim + cropPad.imageWidth);
    int const leftMarginChars = cropPad.leftMargin / 8;

    for(row = 0; row < cropPad.imageHeight; ++row ) {
        pbm_readpbmrow_bitoffset(ifP, bitrow, inCols, format, offset);

        /* Trim off fractional margin portion in first byte of image data */
        if(leftTrim > 0) {
            bitrow[startChar] <<= leftTrim;
            bitrow[startChar] >>= leftTrim;
        }
        /* Do the same with bits in last byte of relevant image data */
        if(rightTrim > 0) {
            bitrow[startChar + imageChars -1] >>= rightTrim;
            bitrow[startChar + imageChars -1] <<= rightTrim;
            }

        writeMacpRow( leftMarginChars,
                      &bitrow[startChar], imageChars, norle);
    }
}



static void
writeMacp( int                      const cols,
           int                      const rows,
           unsigned int             const format,
           FILE *                   const ifP,
           bool                     const norle,
           struct CropPadDimensions const cropPad ) {

    unsigned int row, skipRow;
    bit * bitrow;

    writeMacpHeader( );

    for( row = 0; row < cropPad.topMargin; ++row )
        writeMacpRow( MACP_COLCHARS, NULL, 0, norle);

    /* Allocate PBM row with one extra byte for the shift case. */
    bitrow = pbm_allocrow_packed(cols + 8);

    for(skipRow = 0; skipRow < cropPad.topCrop; ++skipRow )
         pbm_readpbmrow_packed(ifP, bitrow, cols, format);

    encodeRowsWithShift(bitrow, ifP, cols, format, norle, cropPad);

    pbm_freerow_packed(bitrow);

    for(row = cropPad.bottomMargin + 1 ; row < MACP_ROWS ; ++row )
        writeMacpRow( MACP_COLCHARS, NULL, 0, norle);
}



int
main( int argc, char *argv[] ) {
    FILE * ifP;
    int rows, cols;
    int format;
    struct cmdlineInfo cmdline;
    struct CropPadDimensions cropPad;

    pbm_init( &argc, argv );

    parseCommandLine(argc, argv, &cmdline);
    ifP = pm_openr(cmdline.inputFilespec);

    pbm_readpbminit(ifP, &cols, &rows, &format);

    calculateCropPad(cmdline, &cropPad, cols, rows);

    writeMacp( cols, rows, format, ifP, cmdline.norle, cropPad );

    pm_close( ifP );
    exit( 0 );
}

