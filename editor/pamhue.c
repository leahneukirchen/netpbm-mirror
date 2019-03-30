/*=============================================================================
                                  pamhue
===============================================================================
  Change the hue, the Hue-Saturation-Value model, every pixel in an image
  by a specified angle.
=============================================================================*/
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include "mallocvar.h"
#include "shhopt.h"
#include "pam.h"



struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFileName;  /* '-' if stdin */
    float        huechange;
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

    unsigned int huechangeSpec;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0,  "huechange",          OPT_FLOAT,
            &cmdlineP->huechange,           &huechangeSpec,             0);

    opt.opt_table = option_def;
    opt.short_allowed = false;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = false;    /* No negative arguments */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (!huechangeSpec)
        pm_error("You must specify -huechange");

    if (argc-1 < 1)
        cmdlineP->inputFileName = "-";
    else if (argc-1 == 1)
        cmdlineP->inputFileName = argv[1];
    else
        pm_error("Program takes at most one argument:  file specification");
}



static float
positiveMod(float const arg,
            float const modulus) {
/*----------------------------------------------------------------------------
   'arg' mod 'modulus', but positive (i.e. in the range 0.0 - 'modulus').
-----------------------------------------------------------------------------*/
    float const mod = fmodf(arg, modulus);

    return mod >= 0.0 ? mod : 360 + mod;
}



static void
changeHue(tuple   const tupleval,
          float   const huechange,
          sample  const maxval) {

    pixel oldRgb, newRgb;
    struct hsv oldHsv, newHsv;

    PPM_PUTR(oldRgb, tupleval[PAM_RED_PLANE]);
    PPM_PUTG(oldRgb, tupleval[PAM_GRN_PLANE]);
    PPM_PUTB(oldRgb, tupleval[PAM_BLU_PLANE]);

    oldHsv = ppm_hsv_from_color(oldRgb, maxval);

    newHsv.h = positiveMod(oldHsv.h + huechange, 360.0);
    newHsv.s = oldHsv.s;
    newHsv.v = oldHsv.v;

    newRgb = ppm_color_from_hsv(newHsv, maxval);

    tupleval[PAM_RED_PLANE] = PPM_GETR(newRgb);
    tupleval[PAM_GRN_PLANE] = PPM_GETG(newRgb);
    tupleval[PAM_BLU_PLANE] = PPM_GETB(newRgb);
}



static void
convertRow(tuple *            const tuplerow,
           float              const huechange,
           const struct pam * const pamP) {

    unsigned int col;

    for (col = 0; col < pamP->width; ++col)  {
        if ((pamP->format == PPM_FORMAT) || (pamP->format == RPPM_FORMAT) ||
                 ((pamP->format == PAM_FORMAT) && (pamP->depth >= 3))) {
            /* It's a color image, so there is a hue to change */

            changeHue(tuplerow[col], huechange, pamP->maxval);
        } else {
            /* It's black and white or grayscale, which means fully
               desaturated, so hue is meaningless.  Nothing to change.
            */
        }
    }
}



static void
pamhue(struct CmdlineInfo const cmdline,
       FILE *             const ifP,
       FILE *             const ofP) {

    struct pam inpam, outpam;
    tuple * tuplerow;
    unsigned int row;

    pnm_readpaminit(ifP, &inpam, PAM_STRUCT_SIZE(tuple_type));

    outpam = inpam;
    outpam.file = ofP;

    pnm_writepaminit(&outpam);

    tuplerow = pnm_allocpamrow(&inpam);

    for (row = 0; row < inpam.height; ++row) {
        pnm_readpamrow(&inpam, tuplerow);

        convertRow(tuplerow, cmdline.huechange, &inpam);

        pnm_writepamrow(&outpam, tuplerow);
    }

    pnm_freepamrow(tuplerow);
}



int
main(int argc, const char *argv[]) {

    struct CmdlineInfo cmdline;
    FILE * ifP;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFileName);

    pamhue(cmdline, ifP, stdout);

    pm_close(ifP);
    pm_close(stdout);

    return 0;
}



/* This was derived by Bryan Henderson from code by Willem van Schaik
   (willem@schaik.com), which was derived from Ppmbrighten code written by Jef
   Poskanzer and Brian Moffet.

   Copyright (C) 1989 by Jef Poskanzer.
   Copyright (C) 1990 by Brian Moffet.
   Copyright (C) 2019 by Willem van Schaik (willem@schaik.com)

   Permission to use, copy, modify, and distribute this software and its
   documentation for any purpose and without fee is hereby granted, provided
   that the above copyright notice appear in all copies and that both that
   copyright notice and this permission notice appear in supporting
   documentation.  This software is provided "as is" without express or
   implied warranty.

   Bryan contributes his work to the public domain.
*/
