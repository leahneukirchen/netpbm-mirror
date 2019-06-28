/*=============================================================================
                              ppmbrighten
===============================================================================
  Change Value and Saturation of PPM image.
=============================================================================*/

#include "pm_c_util.h"
#include "ppm.h"
#include "shhopt.h"
#include "mallocvar.h"

struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFileName;  /* '-' if stdin */
    float saturation;
    float value;
    unsigned int normalize;
};



static void
parseCommandLine(int argc, const char ** argv,
                 struct CmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   parse program command line described in Unix standard form by argc
   and argv.  Return the information in the options as *cmdlineP.

   If command line is internally inconsistent (invalid options, etc.),
   issue error message to stderr and abort program.

   Note that the strings we return are stored in the storage that
   was passed to us as the argv array.  We also trash *argv.
-----------------------------------------------------------------------------*/
    optEntry *option_def;
        /* Instructions to pm_optParseOptions3 on how to parse our options.
         */
    optStruct3 opt;

    unsigned int option_def_index;

    unsigned int saturationSpec, valueSpec;
    int saturationOpt, valueOpt;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0, "saturation",  OPT_INT,    &saturationOpt,
            &saturationSpec,      0 );
    OPTENT3(0, "value",       OPT_INT,    &valueOpt,
            &valueSpec,           0 );
    OPTENT3(0, "normalize",   OPT_FLAG,   NULL,
            &cmdlineP->normalize, 0 );

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We have no parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (saturationSpec) {
        if (saturationOpt < -100)
            pm_error("Saturation reduction cannot be more than 100%%.  "
                     "You specified %d", saturationOpt);
        else
            cmdlineP->saturation = 1.0 + (float)saturationOpt / 100;
    } else
        cmdlineP->saturation = 1.0;

    if (valueSpec) {
        if (valueOpt < -100)
            pm_error("Value reduction cannot be more than 100%%.  "
                     "You specified %d", valueOpt);
        else
            cmdlineP->value = 1.0 + (float)valueOpt / 100;
    } else
        cmdlineP->value = 1.0;

    if (argc-1 < 1)
        cmdlineP->inputFileName = "-";
    else if (argc-1 == 1)
        cmdlineP->inputFileName = argv[1];
    else
        pm_error("Program takes at most one argument:  file specification");
}



static void
getMinMax(FILE *       const ifP,
          unsigned int const cols,
          unsigned int const rows,
          pixval       const maxval,
          int          const format,
          double *     const minValueP,
          double *     const maxValueP) {

    pixel * pixelrow;
    double minValue, maxValue;
    unsigned int row;

    pixelrow = ppm_allocrow(cols);

    for (row = 0, minValue = 65536.0, maxValue = 0.0; row < rows; ++row) {
        unsigned int col;

        ppm_readppmrow(ifP, pixelrow, cols, maxval, format);

        for (col = 0; col < cols; ++col) {
            struct hsv const pixhsv =
                ppm_hsv_from_color(pixelrow[col], maxval);

            maxValue = MAX(maxValue, pixhsv.v);
            minValue = MIN(minValue, pixhsv.v);
        }
    }
    ppm_freerow(pixelrow);

    *minValueP = minValue;
    *maxValueP = maxValue;
}



int
main(int argc, const char ** argv) {

    double const EPSILON = 1.0e-5;
    struct CmdlineInfo cmdline;
    FILE * ifP;
    pixel * pixelrow;
    pixval maxval;
    int rows, cols, format, row;
    double minValue, maxValue;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    if (cmdline.normalize)
        ifP = pm_openr_seekable(cmdline.inputFileName);
    else
        ifP = pm_openr(cmdline.inputFileName);

    ppm_readppminit(ifP, &cols, &rows, &maxval, &format);

    if (cmdline.normalize) {
        pm_filepos rasterPos;
        pm_tell2(ifP, &rasterPos, sizeof(rasterPos));
        getMinMax(ifP, cols, rows, maxval, format, &minValue, &maxValue);
        pm_seek2(ifP, &rasterPos, sizeof(rasterPos));
        if (maxValue - minValue > EPSILON) {
            pm_message("Minimum value %.0f%% of full intensity "
                       "being remapped to zero.",
                       (minValue * 100.0));
            pm_message("Maximum value %.0f%% of full intensity "
                       "being remapped to full.",
                       (maxValue * 100.0));
        } else
            pm_message("Sole value of %.0f%% of full intensity "
                       "not being remapped",
                       (maxValue * 100.0));
    }

    pixelrow = ppm_allocrow(cols);

    ppm_writeppminit(stdout, cols, rows, maxval, 0);

    for (row = 0; row < rows; ++row) {
        unsigned int col;

        ppm_readppmrow(ifP, pixelrow, cols, maxval, format);

        for (col = 0; col < cols; ++col) {
            struct hsv pixhsv;

            pixhsv = ppm_hsv_from_color(pixelrow[col], maxval);
                /* initial value */

            if (cmdline.normalize) {
                if (maxValue - minValue > EPSILON)
                    pixhsv.v = (pixhsv.v - minValue) / (maxValue - minValue);
            }
            pixhsv.s = pixhsv.s * cmdline.saturation;
            pixhsv.s = MAX(0.0, MIN(1.0, pixhsv.s));
            pixhsv.v = pixhsv.v * cmdline.value;
            pixhsv.v = MAX(0.0, MIN(1.0, pixhsv.v));
            pixelrow[col] = ppm_color_from_hsv(pixhsv, maxval);
        }
        ppm_writeppmrow(stdout, pixelrow, cols, maxval, 0);
    }
    ppm_freerow(pixelrow);

    pm_close(ifP);

    /* If the program failed, it previously aborted with nonzero exit status
       via various function calls.
    */
    return 0;
}



/**
** Copyright (C) 1989 by Jef Poskanzer.
** Copyright (C) 1990 by Brian Moffet.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

