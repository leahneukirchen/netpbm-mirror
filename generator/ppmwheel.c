/* ppmwheel.c - create a color circle of a specified size
**
** This was adapted by Bryan Henderson in January 2003 from ppmcirc.c by
** Peter Kirchgessner:
**
** Copyright (C) 1995 by Peter Kirchgessner.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

#include <string.h>
#include <math.h>

#include "pm_c_util.h"
#include "mallocvar.h"
#include "shhopt.h"
#include "ppm.h"

#ifndef PI
#define PI  3.14159265358979323846
#endif



typedef enum {WT_HUE_VAL, WT_HUE_SAT, WT_PPMCIRC} WheelType;


struct CmdlineInfo {
    unsigned int diameter;
    WheelType    wheelType;
    pixval       maxval;
};



static void
parseCommandLine(int argc, const char **argv,
                 struct CmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
  Convert program invocation arguments (argc,argv) into a format the
  program can use easily, struct CmdlineInfo.  Validate arguments along
  the way and exit program with message if invalid.

  Note that some string information we return as *cmdlineP is in the storage
  argv[] points to.
-----------------------------------------------------------------------------*/
    optEntry * option_def;
        /* Instructions to OptParseOptions3 on how to parse our options.
         */
    optStruct3 opt;

    unsigned int maxvalSpec, huevalueSpec, huesaturationSpec;
    unsigned int option_def_index;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;
    OPTENT3(0, "maxval",         OPT_UINT,
            &cmdlineP->maxval, &maxvalSpec,        0);
    OPTENT3(0, "huevalue",       OPT_FLAG,
            NULL,              &huevalueSpec,      0);
    OPTENT3(0, "huesaturation",  OPT_FLAG,
            NULL,              &huesaturationSpec, 0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We have no parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (!maxvalSpec)
        cmdlineP->maxval = PPM_MAXMAXVAL;
    else {
        if (cmdlineP->maxval > PPM_OVERALLMAXVAL)
            pm_error("The value you specified for -maxval (%u) is too big.  "
                     "Max allowed is %u", cmdlineP->maxval,
                     PPM_OVERALLMAXVAL);

        if (cmdlineP->maxval < 1)
            pm_error("You cannot specify 0 for -maxval");
    }

    if (huevalueSpec + huesaturationSpec > 1)
        pm_error("You may specify at most one of "
                 "-huevalue and -huesaturation");

    cmdlineP->wheelType =
        huevalueSpec      ? WT_HUE_VAL :
        huesaturationSpec ? WT_HUE_SAT :
        WT_PPMCIRC;

    if (argc-1 != 1) {
        pm_error("Need 1 argument diameter of the wheel in pixels");
    } else {
        const char * const diameterArg = argv[1];

        if (strlen(diameterArg) == 0)
            pm_error("Diameter argument is a null string");
        else {
            long argNumber;
            char * tailptr;
            argNumber = strtol(diameterArg, &tailptr, 10);

            if (*tailptr != '\0')
                pm_error("You specified an invalid number as diameter: '%s'",
                         diameterArg);
            if (argNumber <= 0)
                pm_error("Diameter must be positive.  You specified %ld.",
                         argNumber);
            if (argNumber < 4)
                pm_error("Diameter must be at least 4.  You specified %ld",
                         argNumber);

            cmdlineP->diameter = argNumber;
        }
    }
    free(option_def);
}







static pixel
ppmcircColor(pixel  const normalColor,
             pixval const maxval,
             double const d) {
/*----------------------------------------------------------------------------
   The color that Ppmcirc (by Peter Kirchgessner, not part of Netpbm) puts at
   'd' units from the center where the normal color in a hue-value color wheel
   is 'normalColor'.

   We have no idea what the point of this is.
-----------------------------------------------------------------------------*/
    pixel retval;

    if (d >= 0.5) {
        double const scale = sqrt(2.0 - 2.0 * d);

        PPM_ASSIGN(retval,
                   maxval - scale * (maxval - normalColor.r/d),
                   maxval - scale * (maxval - normalColor.g/d),
                   maxval - scale * (maxval - normalColor.b/d));
    } else if (d == 0.0) {
        PPM_ASSIGN(retval, 0, 0, 0);
    } else {
        double const scale = sqrt(sqrt(sqrt(2.0 * d)))/d;
        PPM_ASSIGN(retval,
                   normalColor.r * scale,
                   normalColor.g * scale,
                   normalColor.b * scale);
    }
    return retval;
}



static pixel
wheelColor(WheelType const wheelType,
           double    const dx,
           double    const dy,
           double    const radius,
           pixval    const maxval) {

    double const dist = sqrt(SQR(dx) + SQR(dy));

    pixel retval;

    if (dist > radius) {
        retval = ppm_whitepixel(maxval);
    } else {
        double const hue90 = atan2(dx, dy) / PI * 180.0;
        struct hsv hsv;

        hsv.h = hue90 < 0.0 ? 360.0 + hue90 : hue90;

        switch (wheelType) {
        case WT_HUE_SAT:
            hsv.v = 1.0;
            hsv.s = dist / radius;
            retval = ppm_color_from_hsv(hsv, maxval);
            break;
        case WT_HUE_VAL:
            hsv.s = 1.0;
            hsv.v = dist / radius;
            retval = ppm_color_from_hsv(hsv, maxval);
            break;
        case WT_PPMCIRC:
            hsv.s = 1.0;
            hsv.v = dist / radius;
            {
                pixel const hvColor = ppm_color_from_hsv(hsv, maxval);
                retval = ppmcircColor(hvColor, maxval, dist/radius);
            }
            break;
        }
    }
    return retval;
}



static void
ppmwheel(WheelType    const wheelType,
         unsigned int const diameter,
         pixval       const maxval,
         FILE *       const ofP) {

    unsigned int const cols   = diameter;
    unsigned int const rows   = diameter;
    unsigned int const radius = diameter/2 - 1;
    unsigned int const xcenter = cols / 2;
    unsigned int const ycenter = rows / 2;

    unsigned int row;
    pixel * orow;

    orow = ppm_allocrow(cols);

    ppm_writeppminit(ofP, cols, rows, maxval, 0);

    for (row = 0; row < rows; ++row) {
        unsigned int col;
        for (col = 0; col < cols; ++col) {
            double const dx = (int)col - (int)xcenter;
            double const dy = (int)row - (int)ycenter;

            orow[col] = wheelColor(wheelType, dx, dy, radius, maxval);
        }
        ppm_writeppmrow(ofP, orow, cols, maxval, 0);
    }
    ppm_freerow(orow);
}



int
main(int argc, const char ** argv) {

    struct CmdlineInfo cmdline;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ppmwheel(cmdline.wheelType, cmdline.diameter, cmdline.maxval, stdout);

    pm_close(stdout);
    return 0;
}


