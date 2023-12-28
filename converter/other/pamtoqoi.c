/*
  pamtoqoi -  Converts PAM to a QOI - The "Quite OK Image" format file

  This program is part of Netpbm.

  ---------------------------------------------------------------------

  QOI - The "Quite OK Image" format for fast, lossless image compression

  Encoder by Dominic Szablewski - https://phoboslab.org

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

  Modifications for Netpbm read routines by Akira F. Urushibata.

*/

#include <string.h>
#include <assert.h>
#include "pam.h"
#include "nstring.h"
#include "mallocvar.h"
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
encodeQoiHeader(qoi_Desc const qoiDesc) {

    assert (QOI_MAGIC_SIZE + 4 + 4 + 1 + 1 == QOI_HEADER_SIZE);

    fwrite (qoi_magic, QOI_MAGIC_SIZE, 1, stdout);
    pm_writebiglongu(stdout, qoiDesc.width);
    pm_writebiglongu(stdout, qoiDesc.height);
    putchar(qoiDesc.channelCt);
    putchar(qoiDesc.colorspace);

}



enum Tupletype {BW, BWAlpha, GRAY, GRAYAlpha, RGB, RGBAlpha,
                GRAY255, GRAY255Alpha, RGB255, RGB255Alpha};



static void
createSampleMap(sample    const oldMaxval,
                sample ** const sampleMapP) {

    unsigned int i;
    sample * sampleMap;
    sample   const newMaxval = 255;

    MALLOCARRAY_NOFAIL(sampleMap, oldMaxval+1);

    for (i = 0; i <= oldMaxval; ++i)
        sampleMap[i] = ROUNDDIV(i * newMaxval, oldMaxval);

    *sampleMapP = sampleMap;
}



static enum Tupletype
tupleTypeFmPam(const char * const pamTupleType,
               sample       const maxval) {

    enum Tupletype retval;

    if (streq(pamTupleType, PAM_PBM_TUPLETYPE)) {
        if (maxval !=1)
            pm_error("Invalid maxval (%lu) for tuple type '%s'.",
                     maxval, pamTupleType);
        else
            retval =  BW;
    } else if (streq(pamTupleType, PAM_PBM_ALPHA_TUPLETYPE)) {
      if (maxval !=1)
          pm_error("Invalid maxval (%lu) for tuple type '%s'.",
                    maxval, pamTupleType);
      else
          retval =  BWAlpha;
    } else if (maxval == 255) {
        if (streq(pamTupleType, PAM_PPM_TUPLETYPE))
            retval =  RGB255;
        else if(streq(pamTupleType, PAM_PPM_ALPHA_TUPLETYPE))
            retval =  RGB255Alpha;
        else if(streq(pamTupleType, PAM_PGM_TUPLETYPE))
            retval =  GRAY255;
        else if(streq(pamTupleType, PAM_PGM_ALPHA_TUPLETYPE))
            retval =  GRAY255Alpha;
        else
            pm_error("Don't know how to convert tuple type '%s'.",
                     pamTupleType);
    } else {
        if (streq(pamTupleType, PAM_PPM_TUPLETYPE))
            retval =  RGB;
        else if(streq(pamTupleType, PAM_PPM_ALPHA_TUPLETYPE))
            retval =  RGBAlpha;
        else if(streq(pamTupleType, PAM_PGM_TUPLETYPE))
            retval =  GRAY;
        else if(streq(pamTupleType, PAM_PGM_ALPHA_TUPLETYPE))
            retval =  GRAYAlpha;
        else
            pm_error("Don't know how to convert tuple type '%s'.",
                     pamTupleType);
    }

    return retval;
}



#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"

static unsigned int
channelCtFmTupleType(enum Tupletype const tupleType) {

    switch (tupleType) {
        case RGB:
          return 3;
        case RGB255:
          return 3;
        case RGBAlpha:
          return 4;
        case RGB255Alpha:
          return 4;
        case BW:
        case GRAY:
          return 3;
        case GRAY255:
          return 3;
        case BWAlpha:
        case GRAYAlpha:
          return 4;
        case GRAY255Alpha:
          return 4;
    }
}
#pragma GCC diagnostic pop



static qoi_Rgba
pxFmTuple(tuple          const tuple0,
          const sample * const sampleMap,
          enum Tupletype const tupleType) {
/*----------------------------------------------------------------------------
   Convert PAM tuple to qoi rgba pixel struct
-----------------------------------------------------------------------------*/
    qoi_Rgba px;

    switch (tupleType) {
    case RGB:
        px.rgba.r = sampleMap[tuple0[PAM_RED_PLANE]];
        px.rgba.g = sampleMap[tuple0[PAM_GRN_PLANE]];
        px.rgba.b = sampleMap[tuple0[PAM_BLU_PLANE]];
        px.rgba.a = 255;
        break;
    case RGB255:
        px.rgba.r = tuple0[PAM_RED_PLANE];
        px.rgba.g = tuple0[PAM_GRN_PLANE];
        px.rgba.b = tuple0[PAM_BLU_PLANE];
        px.rgba.a = 255;
        break;
    case RGBAlpha:
        px.rgba.r = sampleMap[tuple0[PAM_RED_PLANE]];
        px.rgba.g = sampleMap[tuple0[PAM_GRN_PLANE]];
        px.rgba.b = sampleMap[tuple0[PAM_BLU_PLANE]];
        px.rgba.a = sampleMap[tuple0[PAM_TRN_PLANE]];
        break;
    case RGB255Alpha:
        px.rgba.r = tuple0[PAM_RED_PLANE];
        px.rgba.g = tuple0[PAM_GRN_PLANE];
        px.rgba.b = tuple0[PAM_BLU_PLANE];
        px.rgba.a = tuple0[PAM_TRN_PLANE];
        break;
    case BW:
    case GRAY : {
        unsigned char const qoiSample = sampleMap[tuple0[0]];
        px.rgba.r = qoiSample;
        px.rgba.g = qoiSample;
        px.rgba.b = qoiSample;
        px.rgba.a = 255;
    } break;
    case GRAY255: {
        unsigned char const qoiSample = tuple0[0];
        px.rgba.r = qoiSample;
        px.rgba.g = qoiSample;
        px.rgba.b = qoiSample;
        px.rgba.a = 255;
    } break;
    case BWAlpha:
    case GRAYAlpha: {
        unsigned char const qoiSample = sampleMap[tuple0[0]];
        px.rgba.r = qoiSample;
        px.rgba.g = qoiSample;
        px.rgba.b = qoiSample;
        px.rgba.a = sampleMap[tuple0[PAM_GRAY_TRN_PLANE]];
    } break;
    case GRAY255Alpha: {
        unsigned char const qoiSample = tuple0[0];
        px.rgba.r = qoiSample;
        px.rgba.g = qoiSample;
        px.rgba.b = qoiSample;
        px.rgba.a = tuple0[PAM_GRAY_TRN_PLANE];
    } break;
    }

    return px;
}



static void
encodeNewPixel(qoi_Rgba const px,
               qoi_Rgba const pxPrev) {

    if (px.rgba.a == pxPrev.rgba.a) {
        signed char const vr = px.rgba.r - pxPrev.rgba.r;
        signed char const vg = px.rgba.g - pxPrev.rgba.g;
        signed char const vb = px.rgba.b - pxPrev.rgba.b;

        signed char const vgR = vr - vg;
        signed char const vgB = vb - vg;

        if (
            vr > -3 && vr < 2 &&
            vg > -3 && vg < 2 &&
            vb > -3 && vb < 2
            ) {
            putchar(QOI_OP_DIFF |
                    (vr + 2) << 4 | (vg + 2) << 2 | (vb + 2));
        } else if (
            vgR >  -9 && vgR <  8 &&
            vg  > -33 && vg  < 32 &&
            vgB >  -9 && vgB <  8
            ) {
            putchar(QOI_OP_LUMA    | (vg   + 32));
            putchar((vgR + 8) << 4 | (vgB +  8));
        } else {
            putchar(QOI_OP_RGB);
            putchar(px.rgba.r);
            putchar(px.rgba.g);
            putchar(px.rgba.b);
        }
    } else {
        putchar(QOI_OP_RGBA);
        putchar(px.rgba.r);
        putchar(px.rgba.g);
        putchar(px.rgba.b);
        putchar(px.rgba.a);
    }
}



static void
qoiEncode(FILE           * const ifP,
          struct pam     * const inpamP) {

    tuple * tuplerow;
    unsigned int row;

    qoi_Rgba index[QOI_INDEX_SIZE];
    qoi_Rgba pxPrev;
    unsigned int run;
    sample * sampleMap;
    qoi_Desc qoiDesc;

    enum Tupletype const tupleType =
      tupleTypeFmPam(inpamP->tuple_type, inpamP->maxval);

    if (inpamP->height > QOI_PIXELS_MAX / inpamP->width)
        pm_error("Too many pixels for QOI: %u x %u (max is %u)",
                 inpamP->height, inpamP->width, QOI_PIXELS_MAX);

    qoiDesc.colorspace = QOI_SRGB;
    qoiDesc.width      = inpamP->width;
    qoiDesc.height     = inpamP->height;
    qoiDesc.channelCt  = channelCtFmTupleType(tupleType);

    encodeQoiHeader(qoiDesc);

    tuplerow = pnm_allocpamrow(inpamP);

    if (inpamP->maxval != 255)
        createSampleMap(inpamP->maxval, &sampleMap);

    qoi_clearQoiIndex(index);

    pxPrev.rgba.r = 0;
    pxPrev.rgba.g = 0;
    pxPrev.rgba.b = 0;
    pxPrev.rgba.a = 255;

    /* Read and convert rows. */
    for (row = 0, run = 0; row < inpamP->height; ++row) {
        unsigned int col;

        pnm_readpamrow(inpamP, tuplerow);

        for (col = 0; col < inpamP->width; ++col) {
            qoi_Rgba const px = pxFmTuple(tuplerow[col], sampleMap, tupleType);

            if (px.v == pxPrev.v) {
                ++run;
                if (run == 62) {
                    putchar(QOI_OP_RUN | (run - 1));
                    run = 0;
                }
            } else {
                unsigned int const indexPos = qoi_colorHash(px);

                if (run > 0) {
                    putchar(QOI_OP_RUN | (run - 1));
                    run = 0;
                }

                if (index[indexPos].v == px.v) {
                    putchar(QOI_OP_INDEX | indexPos);

                } else {
                    index[indexPos] = px;
                    encodeNewPixel(px, pxPrev);
                }
            }
            pxPrev = px;
        }
    }

    if (run > 0)
        putchar(QOI_OP_RUN | (run - 1));

    fwrite(qoi_padding, sizeof(qoi_padding), 1, stdout);

    if (inpamP->maxval != 255)
        free(sampleMap);
    pnm_freepamrow(tuplerow);
}



int
main(int argc, const char **argv) {

    struct CmdlineInfo cmdline;
    struct pam inpam;
    FILE * ifP;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFileName);

    pnm_readpaminit(ifP, &inpam, PAM_STRUCT_SIZE(tuple_type));

    qoiEncode(ifP, &inpam);

    return 0;
}



