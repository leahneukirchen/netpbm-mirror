/* ----------------------------------------------------------------------
 *
 * Bleed colors from non-background colors into the background
 *
 * By Scott Pakin <scott+pbm@pakin.org>
 *
 * ----------------------------------------------------------------------
 *
 * Copyright (C) 2010 Scott Pakin <scott+pbm@pakin.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see http://www.gnu.org/licenses/.
 *
 * ----------------------------------------------------------------------
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <alloca.h>
#include <time.h>

#include "mallocvar.h"
#include "nstring.h"
#include "shhopt.h"
#include "pam.h"
#include "pammap.h"


static time_t const timeUpdateDelta = 30;
    /* Seconds between progress updates */
static int const    minUpdates = 4;
    /* Minimum number of progress updates to output */


struct cmdlineInfo {
    /* This structure represents all of the information the user
       supplied in the command line but in a form that's easy for the
       program to use.
    */
    const char * inputFilename;  /* '-' if stdin */
    const char * bgcolor;
    unsigned int wrap;
    unsigned int all;
    float        power;
    unsigned int downsample;
};

struct coords {
    /* This structure represents an (x,y) coordinate within an image. */
    unsigned int x;
    unsigned int y;
};

typedef double distFunc_t(struct coords const p0,
                          struct coords const p1,
                          unsigned int  const width,
                          unsigned int  const height);
    /* Distance function */



static void
parseCommandLine(int argc, const char ** const argv,
                 struct cmdlineInfo * const cmdlineP ) {

    optEntry     * option_def;
        /* Instructions to OptParseOptions3 on how to parse our options */
    optStruct3     opt;
    unsigned int   option_def_index;
    unsigned int bgcolorSpec, powerSpec,downsampleSpec;

    MALLOCARRAY_NOFAIL(option_def, 100);
    option_def_index = 0;          /* Incremented by OPTENTRY */

    OPTENT3(0, "bgcolor",    OPT_STRING, &cmdlineP->bgcolor,    
            &bgcolorSpec, 0);
    OPTENT3(0, "wrap",       OPT_FLAG,   NULL,
            &cmdlineP->wrap,       0);
    OPTENT3(0, "all",        OPT_FLAG,   NULL,
            &cmdlineP->all,        0);
    OPTENT3(0, "power",      OPT_FLOAT,  &cmdlineP->power,      
            &powerSpec, 0);
    OPTENT3(0, "downsample", OPT_UINT,   &cmdlineP->downsample, 
            &downsampleSpec, 0);

    opt.opt_table = option_def;
    opt.short_allowed = 0;
    opt.allowNegNum = 1;

    optParseOptions3( &argc, (char **)argv, opt, sizeof(opt), 0 );

    if (!bgcolorSpec)
        cmdlineP->bgcolor = NULL;

    if (!powerSpec)
        cmdlineP->power = -2.0;

    if (!downsampleSpec)
        cmdlineP->downsample = 0;

    if (argc-1 < 1)
        cmdlineP->inputFilename = "-";
    else {
        cmdlineP->inputFilename = argv[1];
        if (argc-1 > 1)
            pm_error("Too many arguments: %u.  The only argument is the "
                     "optional input file name", argc-1);
    }
}



static void
locatePaintSources(struct pam *     const pamP,
                   tuple **         const tuples,
                   tuple            const bgColor,
                   unsigned int     const downsample,
                   struct coords ** const paintSourcesP,
                   unsigned int *   const numPaintSourcesP) {
/*--------------------------------------------------------------------
  Construct a list of all pixel coordinates in the input image that
  represent a non-background color.
  ----------------------------------------------------------------------*/
    struct coords * paintSources;
        /* List of paint-source indexes into inImage */
    unsigned int numPaintSources;  /* Number of entries in the above */
    unsigned int numAlloced;    /* Number of allocated coordinates. */
    unsigned int row;

    paintSources = NULL;
    numAlloced = 0;
    numPaintSources = 0;

    for (row = 0; row < pamP->height; ++row) {
        unsigned int col;
        for (col = 0; col < pamP->width; ++col) {
            if (!pnm_tupleequal(pamP, tuples[row][col], bgColor)) {
                /* Add (row, col) to the list of paint sources. */
                if (numPaintSources == numAlloced) {
                    numAlloced += pamP->width;
                    REALLOCARRAY(paintSources, numAlloced);
                    if (!paintSources)
                        pm_error("Out of memory");
                }
                paintSources[numPaintSources].x = col;
                paintSources[numPaintSources].y = row;
                ++numPaintSources;
            }
        }
    }

    pm_message("Image contains %u background + %u non-background pixels",
               pamP->width * pamP->height - numPaintSources,
               numPaintSources);
    
    /* Reduce the number of paint sources to reduce execution time. */
    if (downsample > 0 && downsample < numPaintSources) {
        unsigned int i;

        srandom(time(NULL));

        for (i = 0; i < downsample; ++i) {
            unsigned int const swapIdx = i + random() % (numPaintSources - i);
            struct coords const swapVal = paintSources[i];

            paintSources[i] = paintSources[swapIdx];
            paintSources[swapIdx] = swapVal;
        }
        numPaintSources = downsample;
    }
    *paintSourcesP    = paintSources;
    *numPaintSourcesP = numPaintSources;
}



static distFunc_t euclideanDistanceSqr;

static double
euclideanDistanceSqr(struct coords const p0,
                     struct coords const p1,
                     unsigned int  const width,
                     unsigned int  const height) {
/*----------------------------------------------------------------------------
   Return the square of the Euclidian distance between p0 and p1.
-----------------------------------------------------------------------------*/
    double const deltax = (double) (p1.x - p0.x);
    double const deltay = (double) (p1.y - p0.y);

    return SQR(deltax) + SQR(deltay);
}



static distFunc_t euclideanDistanceTorusSqr;

static double
euclideanDistanceTorusSqr(struct coords const p0,
                          struct coords const p1,
                          unsigned int  const width,
                          unsigned int  const height) {
/*----------------------------------------------------------------------------
   Return the square of the Euclidian distance between p0 and p1, assuming
   it's a toroidal surface on which the top row curves around to meet the
   bottom and the left column to the right.
-----------------------------------------------------------------------------*/
    struct coords p0Adj, p1Adj;

    if (p1.x >= p0.x + width / 2) {
        p0Adj.x = p0.x + width;
        p1Adj.x = p1.x;
    } else if (p0.x >= p1.x + width / 2) {
        p0Adj.x = p0.x;
        p1Adj.x = p1.x + width;
    } else {
        p0Adj.x = p0.x;
        p1Adj.x = p1.x;
    }
    if (p1.y >= p0.y + height / 2) {
        p0Adj.y = p0.y + height;
        p1Adj.y = p1.y;
    } else if (p0.y >= p1.y + height / 2) {
        p0Adj.y = p0.y;
        p1Adj.y = p1.y + height;
    } else {
        p0Adj.y = p0.y;
        p1Adj.y = p1.y;
    }

    return euclideanDistanceSqr(p0Adj, p1Adj, 0, 0);
}



static void
reportProgress(unsigned int const rowsComplete,
               unsigned int const height) {

    static time_t prevOutputTime = 0;
    time_t        now;                  /* Current time in seconds */

    if (prevOutputTime == 0)
        prevOutputTime = time(NULL);

    /* Output our progress only every timeUpdateDelta seconds. */
    now = time(NULL);
    if (prevOutputTime) {
        if (now - prevOutputTime >= timeUpdateDelta
            || rowsComplete % (height/minUpdates) == 0) {
            pm_message("%.1f%% complete",
                       rowsComplete * 100.0 / height);
            prevOutputTime = now;
        }
    } else
        prevOutputTime = now;
}



static void
produceOutputImage(struct pam *          const pamP,
                   tuple **              const tuples,
                   tuple                 const bgColor,
                   const struct coords * const paintSources,
                   unsigned int          const numPaintSources,
                   distFunc_t *          const distFunc,
                   double                const distPower,
                   bool                  const all) {
/*--------------------------------------------------------------------
  Color each background pixel (or, if allPixels is 1, all pixels)
  using a fraction of each paint source as determined by its distance
  to the background pixel.
----------------------------------------------------------------------*/
    struct coords target;

    for (target.y = 0; target.y < pamP->height; ++target.y) {
        double * newColor;
        
        MALLOCARRAY(newColor, pamP->depth);

        for (target.x = 0; target.x < pamP->width; ++target.x) {
            if (all ||
                pnm_tupleequal(pamP, tuples[target.y][target.x], bgColor)) {

                unsigned int plane;
                unsigned int ps;
                double       totalWeight;

                for (plane = 0; plane < pamP->depth; ++plane)
                    newColor[plane] = 0.0;
                totalWeight = 0.0;
                for (ps = 0; ps < numPaintSources; ++ps) {
                    struct coords const source = paintSources[ps];
                    tuple const paintColor = tuples[source.y][source.x];
                    double const distSqr =
                        (*distFunc)(target, source,
                                    pamP->width, pamP->height);

                    if (distSqr > 0.0) {
                        // We do special cases for some common cases with code
                        // that is much faster than pow().
                        double const weight =
                            distPower == -2.0 ? 1.0 / distSqr :
                            distPower == -1.0 ? 1.0 / sqrt(distSqr):
                            pow(distSqr, distPower/2);

                        unsigned int plane;

                        for (plane = 0; plane < pamP->depth; ++plane)
                            newColor[plane] += weight * paintColor[plane];

                        totalWeight += weight;
                    }
                }
                for (plane = 0; plane < pamP->depth; ++plane)
                    tuples[target.y][target.x][plane] =
                        (sample) (newColor[plane] / totalWeight);
            }
        }
        reportProgress(target.y, pamP->height);

        free(newColor);
    }
}



int
main(int argc, const char *argv[]) {
    FILE *             ifP;
    struct cmdlineInfo cmdline;          /* Command-line parameters */
    tuple              bgColor;          /* Input image's background color */
    struct coords *    paintSources;
        /* List of paint-source indexes into inImage */
    unsigned int       numPaintSources;  /* Number of entries in the above */
    distFunc_t *       distFunc;         /* The distance function */
    struct pam inpam;
    struct pam outPam;
    tuple ** tuples;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFilename);

    tuples = pnm_readpam(ifP, &inpam, PAM_STRUCT_SIZE(allocation_depth));

    pm_close(ifP);

    distFunc = cmdline.wrap ? euclideanDistanceTorusSqr : euclideanDistanceSqr;

    if (cmdline.bgcolor)
        bgColor = pnm_parsecolor(cmdline.bgcolor, inpam.maxval) ;
    else
        bgColor = pnm_backgroundtuple(&inpam, tuples);

    pm_message("Treating %s as the background color",
               pnm_colorname(&inpam, bgColor, PAM_COLORNAME_HEXOK));

    locatePaintSources(&inpam, tuples, bgColor, cmdline.downsample,
                       &paintSources, &numPaintSources);

    produceOutputImage(&inpam, tuples,
                       bgColor, paintSources, numPaintSources, distFunc,
                       cmdline.power, cmdline.all);


    outPam = inpam;
    outPam.file = stdout;
    pnm_writepam(&outPam, tuples);

    return 0;
}
