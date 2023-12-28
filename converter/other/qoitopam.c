/*
  qoitopam -  Converts from a QOI - The "Quite OK Image" format file to PAM

  This program is part of Netpbm.

  ---------------------------------------------------------------------


  QOI - The "Quite OK Image" format for fast, lossless image compression

  Decoder by Dominic Szablewski - https://phoboslab.org

  -- LICENSE: The MIT License(MIT)

  Copyright(c) 2021 Dominic Szablewski

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files(the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions :

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

  For more information on the format visit: https//qoiformat.org/ .

  Modifications for Netpbm & PAM write routines by Akira F. Urushibata.
*/

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "pm.h"
#include "pam.h"
#include "mallocvar.h"
#include "nstring.h"
#include "shhopt.h"

#include "qoi.h"



struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFileName;  /* '-' if stdin */
};



static void
parseCommandLine(int                  argc,
                 const char **        argv,
                 struct CmdlineInfo * cmdlineP ) {
/*----------------------------------------------------------------------------
   Parse program command line described in Unix standard form by argc
   and argv.  Return the information in the options as *cmdlineP.

   If command line is internally inconsistent (invalid options, etc.),
   issue error message to stderr and abort program.

   Note that the strings we return are stored in the storage that
   was passed to us as the argv array.  We also trash *argv.
-----------------------------------------------------------------------------*/
    optEntry * option_def;
        /* Instructions to pm_optParseOptions3 on how to parse our options.
         */
    optStruct3 opt;

    unsigned int option_def_index;

    MALLOCARRAY(option_def, 100);

    OPTENTINIT;

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We have no parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (argc-1 < 1)
        cmdlineP->inputFileName = "-";
    else if (argc-1 == 1)
        cmdlineP->inputFileName = argv[1];
    else
        pm_error("Program takes at most one argument: input file name.  "
            "you specified %d", argc-1);
}



static void
readAndValidateMagic(FILE * const ifP){

    char magicBuff[QOI_MAGIC_SIZE];
    size_t charsReadCt;

    charsReadCt = fread(magicBuff, 1, QOI_MAGIC_SIZE, ifP);

    if (charsReadCt == 0)
        pm_error("Input file is empty.");
    else if (charsReadCt < QOI_MAGIC_SIZE || !MEMSEQ(&magicBuff, &qoi_magic)) {
        assert(QOI_MAGIC_SIZE == 4);
        pm_error("Invalid QOI image: does not start with magic number "
                 "'%c%c%c%c'",
                 qoi_magic[0], qoi_magic[1], qoi_magic[2], qoi_magic[3]);
    }
}



/* The following two functions are from lib/pmfileio.c */

static void
abortWithReadError(FILE * const ifP) {

    if (feof(ifP))
        pm_error("Unexpected end of input file");
    else
        pm_error("Error (not EOF) reading file.");
}



static unsigned char
getcNofail(FILE * const ifP) {

    int c;

    c = getc(ifP);

    if (c == EOF)
        abortWithReadError(ifP);

    return (unsigned char) c;
}



static void
decodeQoiHeader(FILE *     const ifP,
                qoi_Desc * const qoiDescP) {

    unsigned long int width, height;

    readAndValidateMagic(ifP);

    pm_readbiglongu(ifP, &width);
    if (width == 0)
        pm_error("Invalid QOI image: width is zero");
    else
        qoiDescP->width = width;

    pm_readbiglongu(ifP, &height);
    if (height == 0)
        pm_error("Invalid QOI image: height is zero");
    else if (height > QOI_PIXELS_MAX / width)
        pm_error ("Invalid QOI image: %u x %u is more than %u pixels",
                  (unsigned int) width, (unsigned int) height, QOI_PIXELS_MAX);
    else
        qoiDescP->height = height;

    qoiDescP->channelCt = getcNofail(ifP);
    if (qoiDescP->channelCt != 3 && qoiDescP->channelCt != 4)
        pm_error("Invalid QOI image: channel count is %u.  "
                 "Only 3 and 4 are valid", qoiDescP->channelCt);

    qoiDescP->colorspace = getcNofail(ifP);
    if (qoiDescP->colorspace != QOI_SRGB && qoiDescP->colorspace != QOI_LINEAR)
        pm_error("Invalid QOI image: colorspace code is %u.  "
                 "Only %u (SRGB) and %u (LINEAR) are valid",
                 qoiDescP->colorspace, QOI_SRGB, QOI_LINEAR);
}



static void
qoiDecode(FILE *       const ifP,
          qoi_Desc *   const qoiDescP,
          struct pam * const outpamP) {

    qoi_Rgba index[QOI_INDEX_SIZE];
    unsigned int row;
    qoi_Rgba px;
    unsigned int run;
    tuple * tuplerow;

    assert(qoiDescP);
    tuplerow = pnm_allocpamrow(outpamP);

    qoi_clearQoiIndex(index);
    px.rgba.r = px.rgba.g = px.rgba.b = 0;
    px.rgba.a = 255;

    for (row = 0, run = 0; row < outpamP->height; ++row) {
        unsigned int col;

        for (col = 0; col < outpamP->width; ++col) {
            if (run > 0) {
                 --run;
            } else {
                unsigned char const b1 = getcNofail(ifP);

                if (b1 == QOI_OP_RGB) {
                    px.rgba.r = getcNofail(ifP);
                    px.rgba.g = getcNofail(ifP);
                    px.rgba.b = getcNofail(ifP);
                } else if (b1 == QOI_OP_RGBA) {
                    px.rgba.r = getcNofail(ifP);
                    px.rgba.g = getcNofail(ifP);
                    px.rgba.b = getcNofail(ifP);
                    px.rgba.a = getcNofail(ifP);
                } else if ((b1 & QOI_MASK_2) == QOI_OP_INDEX) {
                  /* Official spec says 2 or more consecutive instances of
                     QOI_OP_INDEX are not allowed, but we don't check */
                    px = index[b1];
                } else if ((b1 & QOI_MASK_2) == QOI_OP_DIFF) {
                    px.rgba.r += ((b1 >> 4) & 0x03) - 2;
                    px.rgba.g += ((b1 >> 2) & 0x03) - 2;
                    px.rgba.b += ( b1       & 0x03) - 2;
                } else if ((b1 & QOI_MASK_2) == QOI_OP_LUMA) {
                    unsigned char const b2 = getcNofail(ifP);
                    unsigned char const vg = (b1 & 0x3f) - 32;
                    px.rgba.r += vg - 8 + ((b2 >> 4) & 0x0f);
                    px.rgba.g += vg;
                    px.rgba.b += vg - 8 +  (b2       & 0x0f);
                } else if ((b1 & QOI_MASK_2) == QOI_OP_RUN) {
                    run = (b1 & 0x3f);
                }
                /* register pixel in hash lookup array */
                index[qoi_colorHash(px)] = px;
            }
            tuplerow[col][PAM_RED_PLANE] = px.rgba.r;
            tuplerow[col][PAM_GRN_PLANE] = px.rgba.g;
            tuplerow[col][PAM_BLU_PLANE] = px.rgba.b;
            if (qoiDescP->channelCt == 4)
                tuplerow[col][PAM_TRN_PLANE] = px.rgba.a;
        }
        pnm_writepamrow(outpamP, tuplerow);
    }
    if (run > 0)
        pm_error("Invalid QOI image: %u (or more) extra pixels "
                 "beyond end of image.", run);

    pnm_freepamrow(tuplerow);
}



static void
readAndValidatePadding(FILE * const ifP){

    unsigned char padBuff[QOI_PADDING_SIZE];
    size_t charsReadCt;

    charsReadCt = fread(padBuff, 1, QOI_PADDING_SIZE, ifP);

    if(charsReadCt < QOI_PADDING_SIZE) {
        pm_error("Invalid QOI image.  Error reading final 8-byte padding.  "
                 "Premature end of file.");
    } else if (!MEMSEQ(&padBuff, &qoi_padding))
        pm_error("Invalid QOI image.  Final 8-byte padding incorrect.");
    else if (fgetc(ifP) != EOF)
        pm_error("Invalid QOI image.  "
                 "Extraneous bytes after final 8-byte padding.");
}



int
main(int argc, const char **argv) {

    struct CmdlineInfo cmdline;
    qoi_Desc qoiDesc;
    struct pam outpam;
    FILE * ifP;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFileName);

    outpam.size        = sizeof(struct pam);
    outpam.len         = PAM_STRUCT_SIZE(tuple_type);
    outpam.maxval      = QOI_MAXVAL;
    outpam.plainformat = 0;

    decodeQoiHeader(ifP, &qoiDesc);

    outpam.depth  = qoiDesc.channelCt == 3 ? 3 : 4;
    outpam.width  = qoiDesc.width;
    outpam.height = qoiDesc.height;
    outpam.format = PAM_FORMAT;
    outpam.file   = stdout;

    if (qoiDesc.channelCt == 3)
        strcpy(outpam.tuple_type, PAM_PPM_TUPLETYPE);
    else
        strcpy(outpam.tuple_type, PAM_PPM_ALPHA_TUPLETYPE);

    pnm_writepaminit(&outpam);
    qoiDecode(ifP, &qoiDesc, &outpam);

    readAndValidatePadding(ifP);

    return 0;
}



