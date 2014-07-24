/*

              Fractal cratering

       Designed and implemented in November of 1989 by:

        John Walker
        Autodesk SA
        Avenue des Champs-Montants 14b
        CH-2074 MARIN
        Switzerland
        Usenet: kelvin@Autodesk.com
        Fax:    038/33 88 15
        Voice:  038/33 76 33

    The  algorithm  used  to  determine crater size is as described on
    pages 31 and 32 of:

    Peitgen, H.-O., and Saupe, D. eds., The Science Of Fractal
        Images, New York: Springer Verlag, 1988.

    The  mathematical  technique  used  to calculate crater radii that
    obey the proper area law distribution from a uniformly distributed
    pseudorandom sequence was developed by Rudy Rucker.

    Permission  to  use, copy, modify, and distribute this software and
    its documentation  for  any  purpose  and  without  fee  is  hereby
    granted,  without any conditions or restrictions.  This software is
    provided "as is" without express or implied warranty.

                PLUGWARE!

    If you like this kind of stuff, you may also enjoy "James  Gleick's
    Chaos--The  Software"  for  MS-DOS,  available for $59.95 from your
    local software store or directly from Autodesk, Inc., Attn: Science
    Series,  2320  Marinship Way, Sausalito, CA 94965, USA.  Telephone:
    (800) 688-2344 toll-free or, outside the  U.S. (415)  332-2344  Ext
    4886.   Fax: (415) 289-4718.  "Chaos--The Software" includes a more
    comprehensive   fractal    forgery    generator    which    creates
    three-dimensional  landscapes  as  well as clouds and planets, plus
    five more modules which explore other aspects of Chaos.   The  user
    guide  of  more  than  200  pages includes an introduction by James
    Gleick and detailed explanations by Rudy Rucker of the  mathematics
    and algorithms used by each program.

*/

/* Modifications by Arjen Bax, 2001-06-21: Remove black vertical line at
   right edge. Make craters wrap around the image (enables tiling of image).
 */

#define _XOPEN_SOURCE   /* get M_PI in math.h */

#include <assert.h>
#include <math.h>

#include "pm_c_util.h"
#include "pgm.h"
#include "mallocvar.h"
#include "shhopt.h"


struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    unsigned int number;
    unsigned int height;
    unsigned int width;
    float        gamma;
    unsigned int randomseed;
    unsigned int randomseedSpec;
    unsigned int test;
    unsigned int terrain;
    unsigned int radius;

};



static void
parseCommandLine(int argc, const char ** const argv,
                 struct CmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   Note that the file spec array we return is stored in the storage that
   was passed to us as the argv array.
-----------------------------------------------------------------------------*/
    optEntry * option_def;
        /* Instructions to OptParseOptions3 on how to parse our options.
         */
    optStruct3 opt;
    unsigned int option_def_index;

    unsigned int numberSpec, heightSpec, widthSpec, gammaSpec;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0,   "number",   OPT_UINT,    &cmdlineP->number,
            &numberSpec,      0);
    OPTENT3(0,   "height",   OPT_UINT,    &cmdlineP->height,
            &heightSpec,      0);
    OPTENT3(0,   "ysize",    OPT_UINT,    &cmdlineP->height,
            &heightSpec,      0);
    OPTENT3(0,   "width",    OPT_UINT,    &cmdlineP->width,
            &widthSpec,       0);
    OPTENT3(0,   "xsize",    OPT_UINT,    &cmdlineP->width,
            &widthSpec,       0);
    OPTENT3(0,   "gamma",    OPT_FLOAT,   &cmdlineP->gamma,
            &gammaSpec,       0);
    OPTENT3(0,   "randomseed",  OPT_UINT, &cmdlineP->randomseed,
            &cmdlineP->randomseedSpec,   0);
    OPTENT3(0,   "test",     OPT_UINT,   &cmdlineP->radius,
            &cmdlineP->test,      0);
    OPTENT3(0,   "terrain",  OPT_FLAG,    NULL,
            &cmdlineP->terrain,   0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We may have parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (argc-1 > 0)
        pm_error("There are no non-option arguments.  You specified %u",
                 argc-1);

    if (!heightSpec)
        cmdlineP->height = 256;

    if (cmdlineP->height == 0)
        pm_error("-height must be positive");

    if (!widthSpec)
        cmdlineP->width = 256;

    if (cmdlineP->width == 0)
        pm_error("-width must be positive");

    if (cmdlineP->test) {
        if (numberSpec || cmdlineP->randomseedSpec)
            pm_message("Test mode.  Only one fixed crater will be created.  "
                       "-number and/or -randomseed ignored.");

        if(MAX(cmdlineP->height, cmdlineP->width) * 2 < cmdlineP->radius)
            pm_error("Radius (%u) too large", cmdlineP->radius);
    } else {
        if (!numberSpec)
            cmdlineP->number = 50000;

        if (cmdlineP->number == 0)
            pm_error("-number must be positive");
    }

    if (cmdlineP->terrain) {
        if (gammaSpec)
            pm_message("Terrain elevation chart will be output.  "
                       "-gamma argument (%f) ignored.", cmdlineP->gamma);
    } else {
        if (!gammaSpec)
            cmdlineP->gamma = 1.0;

        if (cmdlineP->gamma <= 0.0)
            pm_error("gamma correction must be greater than 0");
    }

    free(option_def);
}


/* Definitions for obtaining random numbers. */

/*  Display parameters  */

static double const ImageGamma = 0.5;     /* Inherent gamma of mapped image */
static double const arand = 32767.0;      /* Random number parameters */
static double const CdepthPower = 1.5;    /* Crater depth power factor */
static double DepthBias2 = 0.5;           /* Square of depth bias */
static int const slopemin = -52;
static int const slopemax = 52;

static double const
Cast(double const high) {

    return high * ((rand() & 0x7FFF) / arand);
}



static unsigned int
mod(int const t,
    unsigned int const n) {

    /* This is used to transform coordinates beyond bounds into ones
       within: craters "wrap around" the edges.  This enables tiling
       of the image.

       Produces strange effects when crater radius is very large compared
       to image size.
    */

    int m;
    m = t % (int) n;
    if (m < 0)
        m += n;
    return m;
}


static void
generateSlopeMap(gray * const slopemap,
                 double const dgamma) {

    /* Prepare an array which maps the difference in altitude between two
       adjacent points (slope) to shades of gray.  Used for output in
       default (non-terrain) mode.   Uphill slopes are bright; downhill
       slopes are dark.
    */ 

    int i;
    double const gamma = dgamma * ImageGamma;

    for (i = slopemin; i <= 0; i++) {   /* Negative, downhill, dark */
        slopemap[i - slopemin] =
            128 - 127.0 * pow(sin((M_PI / 2) * i / slopemin), gamma);
    }
    for (i = 0; i <= slopemax; i++) {   /* Positive, uphill, bright */
        slopemap[i - slopemin] =
            128 + 127.0 * pow(sin((M_PI / 2) * i / slopemax), gamma);
    }

    /* Confused?   OK,  we're using the  left-to-right slope to
       calculate a shade based on the sine of  the  angle  with
       respect  to the vertical (light incident from the left).
       Then, with one exponentiation, we account for  both  the
       inherent   gamma   of   the   image  (ad-hoc),  and  the
       user-specified display gamma, using the identity:
       (x^y)^z = (x^(y*z))             */
}


static gray
slopeToGrayval(int    const slope,
               gray * const slopemap) {

    return( slopemap[ MIN (MAX (slopemin, slope), slopemax) - slopemin] );

}



static void
generateScreenImage(gray **       const terrain,
                    unsigned int  const width,
                    unsigned int  const height,
                    double        const dgamma ) {

    /* Convert a terrain elevation chart into a shaded image and output */

    unsigned int row;
    gray * const pixrow   = pgm_allocrow(width);  /* output row */
    gray * const slopemap = pgm_allocrow(slopemax - slopemin +1);
                            /* Slope to pixel grayval map */
    gray const maxval = 255;

    pgm_writepgminit(stdout, width, height, maxval, FALSE);

    generateSlopeMap(slopemap, dgamma);

    for (row = 0; row < height; ++row) {
        unsigned int col;

        for (col = 0; col < width -1; ++col) {
            int const slope = terrain[row][col+1] - terrain[row][col];
            pixrow[col] = slopeToGrayval(slope, slopemap);
        }
        /* Wrap around to determine shade of pixel on right edge */
        pixrow[width -1] =
            slopeToGrayval(terrain[row][0] - terrain[row][width-1], slopemap);

        pgm_writepgmrow(stdout, pixrow, width, maxval, FALSE);
    }

    pgm_freerow(slopemap);
    pgm_freerow(pixrow);

}


static void
smallCrater(gray **      const terrain,
            unsigned int const width,
            unsigned int const height,
            int          const cx,
            int          const cy,
            double       const g) {

    /* If the crater is tiny, handle it specially. */

    int x, y;
    unsigned int amptot = 0, axelev;
    unsigned int npatch = 0;

    /* Set pixel to the average of its Moore neighbourhood. */

    for (y = cy - 1; y <= cy + 1; y++) {
        for (x = cx - 1; x <= cx + 1; x++) {
            amptot += terrain[mod(y, height)][mod(x, width)];
            npatch++;
        }
    }
    axelev = amptot / npatch;

    /* Perturb the mean elevation by a small random factor. */

    x = (g >= 1) ? ((rand() >> 8) & 3) - 1 : 0;
    terrain[mod(cy, height)][mod(cx, width)] = axelev + x;

}



static void
normalCrater(gray **     const terrain,
            unsigned int const width,
            unsigned int const height,
            int          const cx,
            int          const cy,
            double       const radius) {

     /* Regular crater.  Generate an impact feature of the
               correct size and shape. */

    int x, y;
    unsigned int amptot = 0, axelev;
    unsigned int npatch = 0;
    int const impactRadius = (int) MAX(2, (radius / 3));
    int const craterRadius = (int) radius;
    double const rollmin = 0.9;

    /* Determine mean elevation around the impact area.
       We assume the impact area is a fraction of the total crater size. */

    for (y = cy - impactRadius; y <= cy + impactRadius; y++) {
        for (x = cx - impactRadius; x <= cx + impactRadius; x++) {
             amptot += terrain[mod(y, height)][mod(x, width)];
             npatch++;
        }
    }
    axelev = amptot / npatch;

    for (y = cy - craterRadius; y <= cy + craterRadius; y++) {
        int const dysq = (cy - y) * (cy - y);

        for (x = cx - craterRadius; x <= cx + craterRadius; x++) {
            int  const dxsq = (cx - x) * (cx - x);
            double const cd = (dxsq + dysq) /
                              (double) (craterRadius * craterRadius);
            double const cd2 = cd * 2.25;
            double const tcz = sqrt(DepthBias2) - sqrt(fabs(1 - cd2));
            double cz = MAX((cd2 > 1) ? 0.0 : -10, tcz);  /* Initial value */
            double roll;
            unsigned int av;

            cz *= pow(craterRadius, CdepthPower);
            if (dysq == 0 && dxsq == 0 && ((int) cz) == 0) {
                cz = cz < 0 ? -1 : 1;
                }

             roll = (((1 / (1 - MIN(rollmin, cd))) /
                     (1 / (1 - rollmin))) - (1 - rollmin)) / rollmin;

             av = (axelev + cz) * (1 - roll) +
                  (terrain[mod(y, height)][mod(x, width)] + cz) * roll;
             av = MAX(1000, MIN(64000, av));

             terrain[mod(y, height)][mod(x, width)] = av;
        }
    }
}


/* Todo:  We should also have largeCrater() */


static void
plopCrater(gray **         const terrain,
              unsigned int const width,
              unsigned int const height,
              int          const cx,
              int          const cy,
              double       const radius) {

        if (radius < 3)
          smallCrater (terrain, width, height, cx, cy, radius);
        else
          normalCrater(terrain, width, height, cx, cy, radius);
}



static void
genCraters(struct CmdlineInfo const cmdline) {
/*----------------------------------------------------------------------------
   Generate cratered terrain
-----------------------------------------------------------------------------*/
    unsigned int const width  = cmdline.width;  /* screen X size */
    unsigned int const height = cmdline.height; /* screen Y size */
    double       const dgamma = cmdline.gamma;  /* display gamma */
    gray         const tmaxval = 65535; /* maxval of elevation array */

    gray ** terrain;    /* elevation array */
    unsigned int row, col;

    /* Acquire the elevation array and initialize it to mean
       surface elevation. */

    terrain = pgm_allocarray(width, height);

    for (row = 0; row < height; ++row) {
        for (col = 0; col < width; ++col)
            terrain[row][col] = tmaxval/2;
    }

    if ( cmdline.test )
        plopCrater(terrain, width, height,
                   width/2, height/2, (double) cmdline.radius);

    else {
        unsigned int const ncraters = cmdline.number; /* num of craters */
        unsigned int l;

        for (l = 0; l < ncraters; l++) {
            int const cx = Cast((double) width  - 1);
            int const cy = Cast((double) height - 1);

            /* Thanks, Rudy, for this equation  that maps the uniformly
               distributed  numbers  from   Cast   into   an   area-law
               distribution as observed on cratered bodies.

               Produces values within the interval:
               0.56419 <= radius <= 56.419 */

            double const radius = sqrt(1 / (M_PI * (1 - Cast(0.9999))));

            plopCrater(terrain, width, height, cx, cy, radius);

            if (((l + 1) % 5000) == 0)
                pm_message("%u craters generated of %u (%u%% done)",
                           l + 1, ncraters, ((l + 1) * 100) / ncraters);
        }
    }

    if (cmdline.terrain)
        pgm_writepgm(stdout, terrain, width, height, tmaxval, FALSE);
    else
        generateScreenImage(terrain, width, height, dgamma);

    pgm_freearray(terrain, height);
    pm_close(stdout);
}



int
main(int argc, const char ** argv) {

    struct CmdlineInfo cmdline;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    srand(cmdline.randomseedSpec ? cmdline.randomseed : pm_randseed());
    genCraters(cmdline);

    exit(0);
}
