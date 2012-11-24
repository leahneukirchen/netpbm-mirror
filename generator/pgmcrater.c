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

/* Modifications by Arjen Bax, 2001-06-21: Remove black vertical line at right
 * edge. Make craters wrap around the image (enables tiling of image).
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
    OPTENT3(0,   "gamma",    OPT_FLOAT,    &cmdlineP->gamma,
            &gammaSpec,       0);
    OPTENT3(0,   "randomseed",   OPT_UINT,    &cmdlineP->randomseed,
            &cmdlineP->randomseedSpec,      0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We may have parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (argc-1 > 0)
        pm_error("There are no non-option arguments.  You specified %u",
                 argc-1);

    if (!numberSpec)
        cmdlineP->number = 50000;

    if (cmdlineP->number == 0)
        pm_error("-number must be positive");

    if (!heightSpec)
        cmdlineP->height = 256;

    if (cmdlineP->height == 0)
        pm_error("-height must be positive");

    if (!widthSpec)
        cmdlineP->width = 256;

    if (cmdlineP->width == 0)
        pm_error("-width must be positive");

    if (!gammaSpec)
        cmdlineP->gamma = 1.0;

    if (cmdlineP->gamma <= 0.0)
        pm_error("gamma correction must be greater than 0");

    free(option_def);
}



/* Definitions for obtaining random numbers. */

/*  Display parameters  */

#define SCRX    screenxsize       /* Screen width */
#define SCRY    screenysize       /* Screen height */
#define SCRGAMMA 1.0              /* Display gamma */

#define RGBQuant    255


static double const ImageGamma = 0.5;     /* Inherent gamma of mapped image */

static double const arand = 32767.0;      /* Random number parameters */

static double const CdepthPower = 1.5;    /* Crater depth power factor */

static double DepthBias;  /* sqrt(.5) */

static int const slopemin = -52;
static int const slopemax = 52;


static double const
Cast(double const low,
     double const high) {

    return low + (high - low) * ((rand() & 0x7FFF) / arand);
}



static int
modulo(int const t,
       int const n) {

    int m;

    assert(n > 0);

    m = t % n;

    while (m < 0) {
        m += n;
    }

    return m;
}



#define Auxadr(x, y) &aux[modulo(y, screenysize)*screenxsize+modulo(x, screenxsize)]



static void
generateScreenImage(const unsigned short * const aux,
                    unsigned int           const screenxsize,
                    unsigned int           const screenysize,
                    unsigned char *        const slopemap) {

    unsigned int row;
    gray * pixrow;

    pgm_writepgminit(stdout, screenxsize, screenysize, RGBQuant, FALSE);
    pixrow = pgm_allocrow(screenxsize);

    for (row = 0; row < screenysize; ++row) {
        unsigned int col;

        for (col = 0; col < screenxsize; ++col) {
            int j;
            j = *Auxadr(col+1, row) - *Auxadr(col, row);
            j = MIN(MAX(slopemin, j), slopemax);
            pixrow[col] = slopemap[j - slopemin];
        }
        pgm_writepgmrow(stdout, pixrow, screenxsize, RGBQuant, FALSE);
    }
    pm_close(stdout);
    pgm_freerow(pixrow);

}



static void
gencraters(struct CmdlineInfo const cmdline) {
/*----------------------------------------------------------------------------
   Generate cratered terrain
-----------------------------------------------------------------------------*/
    unsigned int const screenxsize = cmdline.width;  /* screen X size */
    unsigned int const screenysize = cmdline.height; /* screen Y size */
    double       const dgamma =      cmdline.gamma;  /* display gamma */
    unsigned int const ncraters =    cmdline.number; /* num craters to gen */

    int i, j;
    unsigned int l;
    unsigned short * aux;
    unsigned char * slopemap;   /* Slope to pixel map */

    /* Acquire the elevation array and initialize it to mean
       surface elevation. */

    MALLOCARRAY(aux, SCRX * SCRY);
    if (aux == NULL) 
        pm_error("out of memory allocating elevation array");

    /* Acquire the elevation buffer and initialize to mean
       initial elevation. */

    for (i = 0; i < SCRY; i++) {
        unsigned short *zax = aux + (((long) SCRX) * i);

        for (j = 0; j < SCRX; j++) {
            *zax++ = 32767;
        }
    }

    /* Every time we go around this loop we plop another crater
       on the surface.  */

    for (l = 0; l < ncraters; l++) {
        double g;
        int cx = Cast(0.0, ((double) SCRX - 1)),
            cy = Cast(0.0, ((double) SCRY - 1)),
            gx, gy, x, y;
        unsigned int amptot = 0, axelev;
        unsigned int npatch = 0;


        /* Phase 1.  Compute the mean elevation of the impact
           area.  We assume the impact area is a
           fraction of the total crater size. */

        /* Thanks, Rudy, for this equation  that maps the uniformly
           distributed  numbers  from   Cast   into   an   area-law
           distribution as observed on cratered bodies. */

        g = sqrt(1 / (M_PI * (1 - Cast(0, 0.9999))));

        /* If the crater is tiny, handle it specially. */

        if (g < 3) {

            /* Set pixel to the average of its Moore neighbourhood. */

            for (y = cy - 1; y <= cy + 1; y++) {
                for (x = cx - 1; x <= cx + 1; x++) {
                    amptot += *Auxadr(x, y);
                    npatch++;
                }
            }
            axelev = amptot / npatch;

            /* Perturb the mean elevation by a small random factor. */

            x = (g >= 1) ? ((rand() >> 8) & 3) - 1 : 0;
            *Auxadr(cx, cy) = axelev + x;

            /* Jam repaint sizes to correct patch. */

            gx = 1;
            gy = 0;

        } else {

            /* Regular crater.  Generate an impact feature of the
               correct size and shape. */

            /* Determine mean elevation around the impact area. */

            gx = MAX(2, (g / 3));
            gy = MAX(2, g / 3);

            for (y = cy - gy; y <= cy + gy; y++) {
                for (x = cx-gx; x <= cx + gx; x++) {
                    amptot += *Auxadr(x,y);
                    npatch++;
                }
            }
            axelev = amptot / npatch;

            gy = MAX(2, g);
            g = gy;
            gx = MAX(2, g);

            for (y = cy - gy; y <= cy + gy; y++) {
                double dy = (cy - y) / (double) gy,
                    dysq = dy * dy;

                for (x = cx - gx; x <= cx + gx; x++) {
                    double dx = ((cx - x) / (double) gx),
                        cd = (dx * dx) + dysq,
                        cd2 = cd * 2.25,
                        tcz = DepthBias - sqrt(fabs(1 - cd2)),
                        cz = MAX((cd2 > 1) ? 0.0 : -10, tcz),
                        roll, iroll;
                    unsigned short av;

                    cz *= pow(g, CdepthPower);
                    if (dy == 0 && dx == 0 && ((int) cz) == 0) {
                        cz = cz < 0 ? -1 : 1;
                    }

#define         rollmin 0.9
                    roll = (((1 / (1 - MIN(rollmin, cd))) /
                             (1 / (1 - rollmin))) - (1 - rollmin)) / rollmin;
                    iroll = 1 - roll;

                    av = (axelev + cz) * iroll + (*Auxadr(x,y) + cz) * roll;
                    av = MAX(1000, MIN(64000, av));
                    *Auxadr(x,y) = av;
                }
            }
        }
        if ((l % 5000) == 4999) {
            pm_message( "%u craters generated of %u (%u%% done)",
                        l + 1, ncraters, ((l + 1) * 100) / ncraters);
        }
    }

    i = MAX((slopemax - slopemin) + 1, 1);
    MALLOCARRAY(slopemap, i);
    if (slopemap == NULL)
        pm_error("out of memory allocating slope map");

    for (i = slopemin; i <= slopemax; i++) {

        /* Confused?   OK,  we're using the  left-to-right slope to
           calculate a shade based on the sine of  the  angle  with
           respect  to the vertical (light incident from the left).
           Then, with one exponentiation, we account for  both  the
           inherent   gamma   of   the   image  (ad-hoc),  and  the
           user-specified display gamma, using the identity:

           (x^y)^z = (x^(y*z))             */

        slopemap[i - slopemin] = i > 0 ?
            (128 + 127.0 *
             pow(sin((M_PI / 2) * i / slopemax),
                 dgamma * ImageGamma)) :
            (128 - 127.0 *
             pow(sin((M_PI / 2) * i / slopemin),
                 dgamma * ImageGamma));
    }

    generateScreenImage(aux, screenxsize, screenysize, slopemap);

    free((char *) slopemap);
    free((char *) aux);
}



int
main(int argc, const char ** argv) {

    struct CmdlineInfo cmdline;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    srand(cmdline.randomseedSpec ? cmdline.randomseed : pm_randseed());

    DepthBias = sqrt(0.5);

    gencraters(cmdline);

    exit(0);
}



