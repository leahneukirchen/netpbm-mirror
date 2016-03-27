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
#include "runlength.h"
#include "macp.h"

#define MIN3(a,b,c)     (MIN((MIN((a),(b))),(c)))

struct CmdlineInfo {
    /* All the information the user supplied in the command line, in a form
       easy for the program to use.
    */
    const char * inputFileName;  /* File name of input file */
    unsigned int left;
    unsigned int right;
    unsigned int top;
    unsigned int bottom;
    unsigned int leftSpec;
    unsigned int rightSpec;
    unsigned int topSpec;
    unsigned int bottomSpec;
    bool         norle;
};



static void
parseCommandLine(int                        argc,
                 const char **        const argv,
                 struct CmdlineInfo * const cmdlineP) {
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

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    cmdlineP->norle = norleSpec;

    if (argc-1 < 1)
        cmdlineP->inputFileName = "-";
    else {
        cmdlineP->inputFileName = argv[1];
        if (argc-1 > 1)
            pm_error("Program takes zero or one argument (filename).  You "
                     "specified %d", argc-1);
    }

    free(option_def);
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
calculateCropPad(struct CmdlineInfo         const cmdline,
                 unsigned int               const cols,
                 unsigned int               const rows,
                 struct CropPadDimensions * const cropPadP) {
/*--------------------------------------------------------------------------
  Validate -left -right -top -bottom from command line.

  Determine what rows, columns to take from input if any of these are
  specified and return it as *cropPadP.

  'cols and 'rows' are the dimensions of the input image.

  Center image if it is smaller than the fixed Macpaint format size.
----------------------------------------------------------------------------*/
    unsigned int const left = cmdline.leftSpec ? cmdline.left : 0;
    unsigned int const top  = cmdline.topSpec  ? cmdline.top  : 0;

    unsigned int right, bottom, width, height;

    if (cmdline.leftSpec) {
        if (cmdline.rightSpec && left >= cmdline.right)
            pm_error("-left value must be smaller than -right value");
        else if (left + 1 > cols)
            pm_error("Specified -left value is beyond right edge "
                     "of input image");
    }
    if (cmdline.topSpec) {
        if (cmdline.bottomSpec && top >= cmdline.bottom)
            pm_error("-top value must be smaller than -bottom value");
        else if (top + 1 > rows)
            pm_error("Specified -top value is beyond bottom edge "
                     "of input image");
    }
    if (cmdline.rightSpec) {
        if (cmdline.right + 1 > cols)
            pm_message("Specified -right value %u is beyond edge of "
                       "input image", cmdline.right);

        right = MIN3(cmdline.right, cols - 1, left + MACP_COLS - 1);
    } else
        right = MIN(cols - 1,  left + MACP_COLS - 1);

    if (cmdline.bottomSpec) {
        if (cmdline.bottom + 1 > rows)
            pm_message("Specified -bottom value %u is beyond edge of "
                       "input image", cmdline.bottom);

            bottom = MIN3(cmdline.bottom, rows - 1, top + MACP_ROWS - 1);
    } else
        bottom = MIN(rows - 1, top + MACP_ROWS - 1);

    cropPadP->leftCrop = left;
    cropPadP->topCrop  = top;

    assert(right >= left);

    width = right - left + 1;
    assert(width > 0 && width <= MACP_COLS);

    cropPadP->leftMargin = (MACP_COLS - width) / 2;

    if (width < cols)
        pm_message("%u of %u input columns will be output", width, cols);

    height = bottom - top + 1;
    assert(height > 0 && height <= MACP_ROWS);

    cropPadP->topMargin    = (MACP_ROWS - height) / 2;
    cropPadP->bottomMargin = cropPadP->topMargin + height - 1;

    if (height < rows)
        pm_message("%u out of %u input rows will be output", height, rows);

    cropPadP->imageWidth  = width;
    cropPadP->imageHeight = height;
}



static void
writeMacpHeader(FILE * const ofP) {

    char const ch = 0x00;    /* header contains nothing */

    unsigned int i;

    for (i = 0; i < MACP_HEAD_LEN; ++i)
        fputc(ch, ofP);
}



static void
writeMacpRowUnpacked(const bit  * const imageBits,
                     unsigned int const leftMarginCharCt,
                     unsigned int const imageColCharCt,
                     FILE *       const ofP) {
/*--------------------------------------------------------------------------
  Encode (without compression) and output one row.  The row comes divided into
  three parts: left margin, image, right margin.
----------------------------------------------------------------------------*/
    char const marginByte = 0x00;  /* White bits for margin */
    unsigned int const rightMarginCharCt =
        MACP_COLCHARS - leftMarginCharCt - imageColCharCt;
    
    unsigned int i;

    fputc(MACP_COLCHARS - 1, ofP);

    for (i = 0; i < leftMarginCharCt; ++i)
        fputc(marginByte, ofP);

    if (imageColCharCt > 0)
        fwrite(imageBits, 1, imageColCharCt, ofP);

    for (i = 0; i < rightMarginCharCt; ++i)
        fputc(marginByte, ofP);
}



static void
writeMacpRowPacked(const bit  * const packedBits,
                   unsigned int const leftMarginCharCt,
                   unsigned int const imageColCharCt,
                   unsigned int const rightMarginCharCt,
                   FILE *       const ofP) {
/*--------------------------------------------------------------------------
  Encode one row and write it to *ofP.

  As in the unpacked case, the row comes divided into three parts: left
  margin, image, right margin.  Unlike the unpacked case we need to know both
  the size of the packed data and the size of the right margin.
----------------------------------------------------------------------------*/
    char const marginByte = 0x00;  /* White bits for margin */

    if (leftMarginCharCt > 0) {
        fputc(257 - leftMarginCharCt, ofP);
        fputc(marginByte, ofP);
    }

    if (imageColCharCt > 0)
        fwrite(packedBits, 1, imageColCharCt, ofP);

    if (rightMarginCharCt > 0) {
        fputc(257 - rightMarginCharCt, ofP);
        fputc(marginByte, ofP);
    }
}



static void
writeMacpRow(bit        * const imageBits,
             unsigned int const leftMarginCharCt,
             unsigned int const imageColCharCt,
             bool         const norle,
             FILE *       const ofP) {
/*--------------------------------------------------------------------------
  Write the row 'imageBits' to Standard Output.

  Write it packed, unless packing would lead to unnecessary bloat or 'norle'
  is true.
----------------------------------------------------------------------------*/
    if (norle)
        writeMacpRowUnpacked(imageBits, leftMarginCharCt, imageColCharCt, ofP);
    else {
        unsigned int const rightMarginCharCt =
            MACP_COLCHARS - leftMarginCharCt - imageColCharCt;
        unsigned char packedBits[MACP_COLCHARS+1];
        size_t packedImageLength;

        if (pm_rlenc_maxbytes(MACP_COLCHARS, PM_RLE_PACKBITS)
            > MACP_COLCHARS + 1)
            pm_error("INTERNAL ERROR: RLE buffer too small");

        pm_rlenc_compressbyte(imageBits, packedBits, PM_RLE_PACKBITS,
                              imageColCharCt,  &packedImageLength);

        if (packedImageLength +
            (leftMarginCharCt  > 0 ? 1 : 0) * 2 +
            (rightMarginCharCt > 0 ? 1 : 0) * 2
            < MACP_COLCHARS) {
            /* It's smaller compressed, so do that */
            writeMacpRowPacked(packedBits, leftMarginCharCt,
                               packedImageLength, rightMarginCharCt, ofP);
        } else { /* Extremely rare */
            /* It's larger compressed, so do it uncompressed.  See note
               at top of file.
            */
            writeMacpRowUnpacked(imageBits, leftMarginCharCt, imageColCharCt,
                                 ofP);
        }
    }
}



static void
encodeRowsWithShift(bit *                    const bitrow,
                    FILE *                   const ifP,
                    int                      const inCols,
                    int                      const format,
                    bool                     const norle,
                    struct CropPadDimensions const cropPad,
                    FILE *                   const ofP) {
/*--------------------------------------------------------------------------
  Shift input rows to put only specified columns to output.  Add padding on
  left and right if necessary.

  No shift if the input image is the exact size (576 columns) of the Macpaint
  format.  If the input image is too wide and -left was not specified, extra
  content on the right is discarded.
----------------------------------------------------------------------------*/
    unsigned int const offset     =
        (cropPad.leftMargin + 8 - cropPad.leftCrop % 8) % 8;
    unsigned int const leftTrim   =
        cropPad.leftMargin % 8;
    unsigned int const rightTrim  =
        (8 - (leftTrim + cropPad.imageWidth) % 8 ) % 8;
    unsigned int const startChar  =
        (cropPad.leftCrop + offset) / 8;
    unsigned int const imageCharCt =
        pbm_packed_bytes(leftTrim + cropPad.imageWidth);
    unsigned int const leftMarginCharCt =
        cropPad.leftMargin / 8;

    unsigned int row;

    for (row = 0; row < cropPad.imageHeight; ++row) {
        pbm_readpbmrow_bitoffset(ifP, bitrow, inCols, format, offset);
        
        /* Trim off fractional margin portion in first byte of image data */
        if (leftTrim > 0) {
            bitrow[startChar] <<= leftTrim;
            bitrow[startChar] >>= leftTrim;
        }
        /* Do the same with bits in last byte of relevant image data */
        if (rightTrim > 0) {
            bitrow[startChar + imageCharCt - 1] >>= rightTrim;
            bitrow[startChar + imageCharCt - 1] <<= rightTrim;
        }

        writeMacpRow(&bitrow[startChar], leftMarginCharCt,
                     imageCharCt, norle, ofP);
    }
}



static void
writeMacp(unsigned int             const cols,
          unsigned int             const rows,
          int                      const format,
          FILE *                   const ifP,
          bool                     const norle,
          struct CropPadDimensions const cropPad,
          FILE *                   const ofP) {

    unsigned int row, skipRow;
    bit * bitrow;

    writeMacpHeader(ofP);

    /* Write top padding */
    for (row = 0; row < cropPad.topMargin; ++row)
        writeMacpRow(NULL, MACP_COLCHARS, 0, norle, ofP);

    /* Allocate PBM row with one extra byte for the shift case. */
    bitrow = pbm_allocrow_packed(cols + 8);

    for (skipRow = 0; skipRow < cropPad.topCrop; ++skipRow)
        pbm_readpbmrow_packed(ifP, bitrow, cols, format);

    encodeRowsWithShift(bitrow, ifP, cols, format, norle, cropPad, ofP);

    pbm_freerow_packed(bitrow);

    /* Add bottom padding */
    for (row = cropPad.bottomMargin + 1; row < MACP_ROWS; ++row)
        writeMacpRow(NULL, MACP_COLCHARS, 0, norle, ofP);
}



int
main(int argc, const char *argv[]) {

    FILE * ifP;
    int rows, cols;
    int format;
    struct CmdlineInfo cmdline;
    struct CropPadDimensions cropPad;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFileName);

    pbm_readpbminit(ifP, &cols, &rows, &format);

    calculateCropPad(cmdline, cols, rows, &cropPad);

    writeMacp(cols, rows, format, ifP, cmdline.norle, cropPad, stdout);

    pm_close(ifP);

    return 0;
}

