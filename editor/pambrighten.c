/*=============================================================================
                                  pambrighten
===============================================================================
  Change Value and Saturation of Netpbm image.
=============================================================================*/

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <math.h>
#include "pm_c_util.h"
#include "mallocvar.h"
#include "shhopt.h"
#include "pam.h"



struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use. */
    const char * inputFileName;  /* '-' if stdin */
    float        valchange;
    float        satchange;
};



static void
parseCommandLine(int                        argc,
                 const char **              argv,
                 struct CmdlineInfo * const cmdlineP ) {
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

    unsigned int valueSpec;
    int          valueOpt;
    unsigned int saturationSpec;
    int          saturationOpt;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0, "value",       OPT_INT,    &valueOpt,
            &valueSpec,           0 );
    OPTENT3(0, "saturation",  OPT_INT,    &saturationOpt,
            &saturationSpec,      0 );

    opt.opt_table = option_def;
    opt.short_allowed = false;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = false;    /* No negative arguments */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (valueSpec) {
        if (valueOpt < -100)
            pm_error("Value reduction cannot be more than 100%%.  "
                     "You specified %d", valueOpt);
        else
            cmdlineP->valchange = 1.0 + (float)valueOpt / 100;
    } else
        cmdlineP->valchange = 1.0;

    if (saturationSpec) {
        if (saturationOpt < -100)
            pm_error("Saturation reduction cannot be more than 100%%.  "
                     "You specified %d", saturationOpt);
        else
            cmdlineP->satchange = 1.0 + (float)saturationOpt / 100;
    } else
        cmdlineP->satchange = 1.0;

    if (argc-1 < 1)
        cmdlineP->inputFileName = "-";
    else if (argc-1 == 1)
        cmdlineP->inputFileName = argv[1];
    else
        pm_error("Program takes at most one argument:  file specification");
}



static void
changeColorPix(tuple  const tupleval,
               float  const valchange,
               float  const satchange,
               sample const maxval) {

    pixel oldRgb, newRgb;
    struct hsv oldHsv, newHsv;

    PPM_PUTR(oldRgb, tupleval[PAM_RED_PLANE]);
    PPM_PUTG(oldRgb, tupleval[PAM_GRN_PLANE]);
    PPM_PUTB(oldRgb, tupleval[PAM_BLU_PLANE]);
    oldHsv = ppm_hsv_from_color(oldRgb, maxval);

    newHsv.h = oldHsv.h;

    newHsv.s = MIN(1.0, MAX(0.0, oldHsv.s * satchange));

    newHsv.v = MIN(1.0, MAX(0.0, oldHsv.v * valchange));

    newRgb = ppm_color_from_hsv(newHsv, maxval);

    tupleval[PAM_RED_PLANE] = PPM_GETR(newRgb);
    tupleval[PAM_GRN_PLANE] = PPM_GETG(newRgb);
    tupleval[PAM_BLU_PLANE] = PPM_GETB(newRgb);
}



static void
changeGrayPix(tuple  const tupleval,
              float  const valchange,
              sample const maxval) {

    double const oldGray = (double) tupleval[0] / maxval;

    double newGray;

    newGray = MIN(1.0, MAX(0.0, oldGray * valchange));

    tupleval[0] = ROUNDU(newGray * maxval);
}



typedef enum {COLORTYPE_COLOR, COLORTYPE_GRAY, COLORTYPE_BW} ColorType;



static ColorType
colorTypeOfImage(struct pam * const pamP) {
/*----------------------------------------------------------------------------
   The basic type of color represented in the image described by *pamP: full
   color, grayscale, or black and white

   Note that we're talking about the format of the image, not the reality of
   the pixels.  A color image is still a color image even if all the colors in
   it happen to be gray.

   For a PAM image, as is customary in Netpbm, we do not consider the tuple
   type, but rather infer the color type from the depth and maxval.  This
   gives us more flexibility for future tuple types.
-----------------------------------------------------------------------------*/
    ColorType retval;

    if (pamP->format == PPM_FORMAT ||
        pamP->format == RPPM_FORMAT ||
        (pamP->format == PAM_FORMAT && pamP->depth >= 3)) {

        retval = COLORTYPE_COLOR;

    } else if (pamP->format == PGM_FORMAT ||
               pamP->format == RPGM_FORMAT ||
               (pamP->format == PAM_FORMAT &&
                pamP->depth >= 1 &&
                pamP->maxval > 1)) {

        retval = COLORTYPE_GRAY;

    } else {

        retval = COLORTYPE_BW;

    }
    return retval;
}



static void
pambrighten(struct CmdlineInfo const cmdline,
            FILE *             const ifP,
            struct pam *       const inpamP,
            struct pam *       const outpamP) {

    ColorType const colorType = colorTypeOfImage(inpamP);

    tuple * tuplerow;
    unsigned int row;

    pnm_writepaminit(outpamP);

    tuplerow = pnm_allocpamrow(inpamP);

    for (row = 0; row < inpamP->height; ++row) {
        unsigned int col;

        pnm_readpamrow(inpamP, tuplerow);

        for (col = 0; col < inpamP->width; ++col)  {
            switch (colorType) {
            case COLORTYPE_COLOR:
                changeColorPix(tuplerow[col],
                               cmdline.valchange, cmdline.satchange,
                               inpamP->maxval);
                break;
            case COLORTYPE_GRAY:
                changeGrayPix(tuplerow[col],
                              cmdline.valchange,
                              inpamP->maxval);
            case COLORTYPE_BW:
                /* Nothing to change. */
                break;
            }
        }
        pnm_writepamrow(outpamP, tuplerow);
    }
    pnm_freepamrow(tuplerow);
}



int
main(int argc, const char *argv[]) {

    struct CmdlineInfo cmdline;
    FILE * ifP;
    struct pam inpam, outpam;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFileName);
    pnm_readpaminit(ifP, &inpam, PAM_STRUCT_SIZE(tuple_type));

    outpam = inpam;
    outpam.file = stdout;

    pambrighten(cmdline, ifP, &inpam, &outpam);

    pm_close(ifP);
    pm_close(stdout);

    return 0;
}



/*
   This was derived from ppmbrighten code written by Jef Poskanzer and 
   Brian Moffet. Updated by Willem van Schaik to support PAM.

   Copyright (C) 1989 by Jef Poskanzer.
   Copyright (C) 1990 by Brian Moffet.
   Copyright (C) 2019 by Willem van Schaik (willem@schaik.com)

   Permission to use, copy, modify, and distribute this software and its
   documentation for any purpose and without fee is hereby granted, provided
   that the above copyright notice appear in all copies and that both that
   copyright notice and this permission notice appear in supporting
   documentation.  This software is provided "as is" without express or
   implied warranty.

   Bryan Henderson contributes his work to the public domain.
*/
