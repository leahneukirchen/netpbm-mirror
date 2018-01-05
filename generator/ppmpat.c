/* ppmpat.c - make a pixmap
**
** Copyright (C) 1989, 1991 by Jef Poskanzer.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

#define _DEFAULT_SOURCE /* New name for SVID & BSD source defines */
#define _XOPEN_SOURCE 500  /* Make sure strdup() is in string.h */
                           /* get M_PI in math.h */
#define _BSD_SOURCE  /* Make sure strdup() is in <string.h> */
#define SPIROGRAPHS 0   /* Spirograph to be added soon */

#include <assert.h>
#include <math.h>
#include <limits.h>
#include <string.h>

#include "pm_c_util.h"
#include "mallocvar.h"
#include "shhopt.h"
#include "nstring.h"
#include "ppm.h"
#include "ppmdraw.h"


typedef enum {
    PAT_GINGHAM2,
    PAT_GINGHAM3,
    PAT_MADRAS,
    PAT_TARTAN,
    PAT_ARGYLE1,
    PAT_ARGYLE2,
    PAT_POLES,
    PAT_SQUIG,
    PAT_CAMO,
    PAT_ANTICAMO,
    PAT_SPIRO1,
    PAT_SPIRO2,
    PAT_SPIRO3
} Pattern;

typedef struct {
/*----------------------------------------------------------------------------
   An ordered list of colors with a cursor.
-----------------------------------------------------------------------------*/
    unsigned int count;
    unsigned int index;
        /* Current position in the list */
    pixel *      color;
        /* Malloced array 'count' in size. */
} ColorTable;

struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    Pattern      basePattern;
    unsigned int width;
    unsigned int height;
    unsigned int colorSpec;
    ColorTable   colorTable;
    unsigned int randomseed;
    unsigned int randomseedSpec;
};


static void
validateColorCount(Pattern      const basePattern,
                   unsigned int const colorCount) {

    if (colorCount == 0)
        pm_error("-color: no colors specified");

    switch (basePattern) {
    case PAT_GINGHAM2:
    case PAT_ARGYLE1:
    case PAT_SPIRO1:
        if (colorCount != 2)
            pm_error("Wrong number of colors: %u. "
                     "2 colors are required for the specified pattern.",
                     colorCount);
        break;
    case PAT_GINGHAM3:
    case PAT_MADRAS:
    case PAT_TARTAN:
    case PAT_ARGYLE2:
        if (colorCount != 3)
            pm_error("Wrong number of colors: %u. "
                     "3 colors are required for the specified pattern.",
                     colorCount);
        break;
    case PAT_POLES:
        if (colorCount < 2)
            pm_error("Too few colors: %u. "
                     "At least 2 colors are required "
                     "for the specified pattern.",
                     colorCount);
        break;
    case PAT_SQUIG:
    case PAT_CAMO:
    case PAT_ANTICAMO:
        if (colorCount < 3)
            pm_error("Wrong number of colors: %u. "
                     "At least 3 colors are required "
                     "for the specified pattern.",
                     colorCount);
        break;

    case PAT_SPIRO2:
    case PAT_SPIRO3:
    default:
        pm_error("INTERNAL ERROR.");
    }
}



static void
parseColorOpt(const char ** const colorText,
              ColorTable  * const colorTableP,
              Pattern       const basePattern) {
/*----------------------------------------------------------------------------
    String-list argument to -color is a comma-separated array of
    color names or values, e.g.:
    "-color=red,white,blue"
    "-color=rgb:ff/ff/ff,rgb:00/00/00,rgb:80/80/ff"

    Input:
      Color name/value string-list: colorText[]

    Output values:
      Color array: colorTableP->color[]
      Number of colors found: colorTableP->colors
----------------------------------------------------------------------------*/
    unsigned int colorCount;
    unsigned int i;
    pixel * inColor;

    for (colorCount = 0; colorText[colorCount] != NULL; ++colorCount)
        ;

    MALLOCARRAY(inColor, colorCount);

    if (!inColor)
        pm_error("Failed to allocate table space for %u colors "
                 "specified by -color", colorCount);

    for (i = 0; i < colorCount; ++i)
        inColor[i] = ppm_parsecolor(colorText[i], PPM_MAXMAXVAL);

    validateColorCount(basePattern, colorCount);

    colorTableP->count = colorCount;
    colorTableP->index = 0;  /* initial value */
    colorTableP->color = inColor;
}



static void
parseCommandLine(int argc, const char ** argv,
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
    const char ** colorText;
    unsigned int basePatternCount;
    unsigned int gingham2;
    unsigned int gingham3;
    unsigned int madras;
    unsigned int tartan;
    unsigned int argyle1;
    unsigned int argyle2;
    unsigned int poles;
    unsigned int squig;
    unsigned int camo;
    unsigned int anticamo;
    unsigned int spiro1;
    unsigned int spiro2;
    unsigned int spiro3;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENTRY */
    OPTENT3(0, "gingham2",      OPT_FLAG,   NULL,
            &gingham2,   0);
    OPTENT3(0, "g2",            OPT_FLAG,   NULL,
            &gingham2,   0);
    OPTENT3(0, "gingham3",      OPT_FLAG,   NULL,
            &gingham3,   0);
    OPTENT3(0, "g3",            OPT_FLAG,   NULL,
            &gingham3,   0);
    OPTENT3(0, "madras",        OPT_FLAG,   NULL,
            &madras,     0);
    OPTENT3(0, "tartan",        OPT_FLAG,   NULL,
            &tartan,     0);
    OPTENT3(0, "argyle1",       OPT_FLAG,   NULL,
            &argyle1,     0);
    OPTENT3(0, "argyle2",       OPT_FLAG,   NULL,
            &argyle2,     0);
    OPTENT3(0, "poles",         OPT_FLAG,   NULL,
            &poles,      0);
    OPTENT3(0, "squig",         OPT_FLAG,   NULL,
            &squig,      0);
    OPTENT3(0, "camo",          OPT_FLAG,   NULL,
            &camo,       0);
    OPTENT3(0, "anticamo",      OPT_FLAG,   NULL,
            &anticamo,   0);
#if SPIROGRAPHS != 0
    OPTENT3(0, "spiro1",        OPT_FLAG,   NULL,
            &spiro1,     0);
    OPTENT3(0, "spiro2",        OPT_FLAG,   NULL,
            &spiro1,     0);
    OPTENT3(0, "spiro3",        OPT_FLAG,   NULL,
            &spiro1,     0);
#else
    spiro1 = spiro2 = spiro3 = 0;
#endif
    OPTENT3(0, "color",         OPT_STRINGLIST, &colorText,
            &cmdlineP->colorSpec,           0);
    OPTENT3(0, "randomseed",    OPT_UINT,       &cmdlineP->randomseed,
            &cmdlineP->randomseedSpec,      0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We have no parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */
    free(option_def);

    basePatternCount =
        gingham2 + gingham3 + madras + tartan + argyle1 + argyle2 +
        poles +
        squig +
        camo + anticamo +
        spiro1 + spiro2 + spiro3;

    if (basePatternCount < 1)
        pm_error("You must specify a base pattern option such as -gingham2");
    else if (basePatternCount > 1)
        pm_error("You may not specify more than one base pattern option.  "
                 "You specified %u", basePatternCount);
    else {
        if (gingham2)
            cmdlineP->basePattern = PAT_GINGHAM2;
        else if (gingham3)
            cmdlineP->basePattern = PAT_GINGHAM3;
        else if (madras)
            cmdlineP->basePattern = PAT_MADRAS;
        else if (tartan)
            cmdlineP->basePattern = PAT_TARTAN;
        else if (argyle1)
            cmdlineP->basePattern = PAT_ARGYLE1;
        else if (argyle2)
            cmdlineP->basePattern = PAT_ARGYLE2;
        else if (poles)
            cmdlineP->basePattern = PAT_POLES;
        else if (squig)
            cmdlineP->basePattern = PAT_SQUIG;
        else if (camo)
            cmdlineP->basePattern = PAT_CAMO;
        else if (anticamo)
            cmdlineP->basePattern = PAT_ANTICAMO;
        else if (spiro1)
            cmdlineP->basePattern = PAT_SPIRO1;
        else if (spiro2)
            cmdlineP->basePattern = PAT_SPIRO2;
        else if (spiro3)
            cmdlineP->basePattern = PAT_SPIRO3;
        else
            assert(false);  /* Every possibility is accounted for */
    }

    if (cmdlineP->colorSpec) {
        parseColorOpt(colorText, &cmdlineP->colorTable, cmdlineP->basePattern);
        free(colorText);
    } else
        cmdlineP->colorTable.count = 0;

    if (argc-1 != 2)
        pm_error("You must specify 2 non-option arguments: width and height "
                 "in pixels.  You specified %u", argc-1);
    else {
        cmdlineP->width  = atoi(argv[1]);
        cmdlineP->height = atoi(argv[2]);

        if (cmdlineP->width < 1)
            pm_error("Width must be at least 1 pixel");
        if (cmdlineP->height < 1)
            pm_error("Height must be at least 1 pixel");
    }
}



static void
freeCmdline(struct CmdlineInfo const cmdline) {

    if (cmdline.colorSpec)
        free(cmdline.colorTable.color);
}



static void
validateComputableDimensions(unsigned int const cols,
                             unsigned int const rows) {

    /*
      Notes on width and height limits:

      cols * 3, rows * 3 appear in madras, tartan
      cols*rows appears in poles
      cols+rows appears in squig

      PPMD functions use signed integers for pixel positions
      (because they allow you to specify points off the canvas).
    */

    if (cols > INT_MAX/4 || rows > INT_MAX/4 || rows > INT_MAX/cols)
        pm_error("Width and/or height are way too large: %u x %u",
                 cols, rows);
}



static pixel
randomColor(pixval const maxval) {

    pixel p;

    PPM_ASSIGN(p,
               rand() % (maxval + 1),
               rand() % (maxval + 1),
               rand() % (maxval + 1)
        );

    return p;
}



#define DARK_THRESH 0.25

static pixel
randomBrightColor(pixval const maxval) {

    pixel p;

    do {
        p = randomColor(maxval);
    } while (PPM_LUMIN(p) <= maxval * DARK_THRESH);

    return p;
}



static pixel
randomDarkColor(pixval const maxval) {

    pixel p;

    do {
        p = randomColor(maxval);
    } while (PPM_LUMIN(p) > maxval * DARK_THRESH);

    return p;
}



static pixel
averageTwoColors(pixel const p1,
                 pixel const p2) {

    pixel p;

    PPM_ASSIGN(p,
               (PPM_GETR(p1) + PPM_GETR(p2)) / 2,
               (PPM_GETG(p1) + PPM_GETG(p2)) / 2,
               (PPM_GETB(p1) + PPM_GETB(p2)) / 2);

    return p;
}



static ppmd_drawproc average_drawproc;

static void
average_drawproc(pixel **     const pixels,
                 int          const cols,
                 int          const rows,
                 pixval       const maxval,
                 int          const col,
                 int          const row,
                 const void * const clientdata) {

    if (col >= 0 && col < cols && row >= 0 && row < rows)
        pixels[row][col] =
            averageTwoColors(pixels[row][col], *((const pixel*) clientdata));
}



static void
nextColor(ColorTable * const colorTableP) {
/*----------------------------------------------------------------------------
  Increment index, return it to 0 if we have used all the colors
-----------------------------------------------------------------------------*/
    colorTableP->index = (colorTableP->index + 1) % colorTableP->count;
}



static void
nextColorBg(ColorTable * const colorTableP) {
/*----------------------------------------------------------------------------
  Increment index, return it to 1 if we have used all the colors (color[0] is
  the background color, it's outside the cycle)
-----------------------------------------------------------------------------*/
    colorTableP->index = colorTableP->index % (colorTableP->count - 1) + 1;
        /* Works when index == 0, but no callers rely on this. */

}



/*----------------------------------------------------------------------------
   Camouflage stuff
-----------------------------------------------------------------------------*/



static pixel
randomAnticamoColor(pixval const maxval) {

    int v1, v2, v3;
    pixel p;

    v1 = (maxval + 1) / 4;
    v2 = (maxval + 1) / 2;
    v3 = 3 * v1;

    switch (rand() % 15) {
    case 0: case 1:
        PPM_ASSIGN(p, rand() % v1 + v3, rand() % v2, rand() % v2);
        break;

    case 2:
    case 3:
        PPM_ASSIGN(p, rand() % v2, rand() % v1 + v3, rand() % v2);
        break;

    case 4:
    case 5:
        PPM_ASSIGN(p, rand() % v2, rand() % v2, rand() % v1 + v3);
        break;

    case 6:
    case 7:
    case 8:
        PPM_ASSIGN(p, rand() % v2, rand() % v1 + v3, rand() % v1 + v3);
        break;

    case 9:
    case 10:
    case 11:
        PPM_ASSIGN(p, rand() % v1 + v3, rand() % v2, rand() % v1 + v3);
        break;

    case 12:
    case 13:
    case 14:
        PPM_ASSIGN(p, rand() % v1 + v3, rand() % v1 + v3, rand() % v2);
        break;
    }

    return p;
}



static pixel
randomCamoColor(pixval const maxval) {

    int const v1 = (maxval + 1 ) / 8;
    int const v2 = (maxval + 1 ) / 4;
    int const v3 = (maxval + 1 ) / 2;

    pixel p;

    switch (rand() % 10) {
    case 0:
    case 1:
    case 2:
        /* light brown */
        PPM_ASSIGN(p, rand() % v3 + v3, rand() % v3 + v2, rand() % v3 + v2);
        break;

    case 3:
    case 4:
    case 5:
        /* dark green */
        PPM_ASSIGN(p, rand() % v2, rand() % v2 + 3 * v1, rand() % v2);
        break;

    case 6:
    case 7:
        /* brown */
        PPM_ASSIGN(p, rand() % v2 + v2, rand() % v2, rand() % v2);
        break;

    case 8:
    case 9:
        /* dark brown */
        PPM_ASSIGN(p, rand() % v1 + v1, rand() % v1, rand() % v1);
        break;
    }

    return p;
}



static float
rnduni(void) {
    return rand() % 32767 / 32767.0;
}



static void
clearBackgroundCamo(pixel **     const pixels,
                    unsigned int const cols,
                    unsigned int const rows,
                    pixval       const maxval,
                    ColorTable * const colorTableP,
                    bool         const antiflag) {

    pixel color;

    if (colorTableP->count > 0) {
        color = colorTableP->color[0];
    } else if (antiflag)
        color = randomAnticamoColor(maxval);
    else
        color = randomCamoColor(maxval);

    ppmd_filledrectangle(
        pixels, cols, rows, maxval, 0, 0, cols, rows, PPMD_NULLDRAWPROC,
        &color);
}



static void
camoFill(pixel **         const pixels,
         unsigned int     const cols,
         unsigned int     const rows,
         pixval           const maxval,
         struct fillobj * const fh,
         ColorTable     * const colorTableP,
         bool             const antiflag) {

    pixel color;

    if (colorTableP->count > 0) {
        assert(colorTableP->index < colorTableP->count);
        color = colorTableP->color[colorTableP->index];
        nextColorBg(colorTableP);
    } else if (antiflag)
        color = randomAnticamoColor(maxval);
    else
        color = randomCamoColor(maxval);

    ppmd_fill(pixels, cols, rows, maxval, fh, PPMD_NULLDRAWPROC, &color);
}



#define BLOBRAD 50

#define MIN_POINTS 7
#define MAX_POINTS 13

#define MIN_ELLIPSE_FACTOR 0.5
#define MAX_ELLIPSE_FACTOR 2.0

#define MIN_POINT_FACTOR 0.5
#define MAX_POINT_FACTOR 2.0



static void
computeXsYs(int *        const xs,
            int *        const ys,
            unsigned int const cols,
            unsigned int const rows,
            unsigned int const pointCt) {

    unsigned int const cx = rand() % cols;
    unsigned int const cy = rand() % rows;
    double const a = rnduni() * (MAX_ELLIPSE_FACTOR - MIN_ELLIPSE_FACTOR) +
        MIN_ELLIPSE_FACTOR;
    double const b = rnduni() * (MAX_ELLIPSE_FACTOR - MIN_ELLIPSE_FACTOR) +
        MIN_ELLIPSE_FACTOR;
    double const theta = rnduni() * 2.0 * M_PI;

    unsigned int p;

    for (p = 0; p < pointCt; ++p) {
        double const c = rnduni() * (MAX_POINT_FACTOR - MIN_POINT_FACTOR) +
            MIN_POINT_FACTOR;
        double const tx   = a * sin(p * 2.0 * M_PI / pointCt);
        double const ty   = b * cos(p * 2.0 * M_PI / pointCt);
        double const tang = atan2(ty, tx) + theta;
        xs[p] = MAX(0, MIN(cols-1, cx + BLOBRAD * c * sin(tang)));
        ys[p] = MAX(0, MIN(rows-1, cy + BLOBRAD * c * cos(tang)));
    }
}



static void
camo(pixel **     const pixels,
     unsigned int const cols,
     unsigned int const rows,
     ColorTable * const colorTableP,
     pixval       const maxval,
     bool         const antiflag) {

    unsigned int const n = (rows * cols) / SQR(BLOBRAD) * 5;

    unsigned int i;

    clearBackgroundCamo(pixels, cols, rows, maxval, colorTableP, antiflag);

    if (colorTableP->count > 0) {
        assert(colorTableP->count > 1);
        colorTableP->index = 1;  /* Foreground colors start at 1 */
    }

    for (i = 0; i < n; ++i) {
        unsigned int const pointCt =
            rand() % (MAX_POINTS - MIN_POINTS + 1) + MIN_POINTS;

        int xs[MAX_POINTS], ys[MAX_POINTS];
        int x0, y0;
        struct fillobj * fh;

        computeXsYs(xs, ys, cols, rows, pointCt);

        x0 = (xs[0] + xs[pointCt - 1]) / 2;
        y0 = (ys[0] + ys[pointCt - 1]) / 2;

        fh = ppmd_fill_create();

        ppmd_polyspline(
            pixels, cols, rows, maxval, x0, y0, pointCt, xs, ys, x0, y0,
            ppmd_fill_drawproc, fh);

        camoFill(pixels, cols, rows, maxval, fh, colorTableP, antiflag);

        ppmd_fill_destroy(fh);
    }
}



/*----------------------------------------------------------------------------
   Plaid patterns
-----------------------------------------------------------------------------*/

static void
gingham2(pixel **     const pixels,
         unsigned int const cols,
         unsigned int const rows,
         ColorTable   const colorTable,
         pixval       const maxval) {

    bool  const colorSpec = (colorTable.count > 0);
    pixel const backcolor = colorSpec ?
                            colorTable.color[0] : randomDarkColor(maxval);
    pixel const forecolor = colorSpec ?
                            colorTable.color[1] : randomBrightColor(maxval);
    unsigned int const colso2 = cols / 2;
    unsigned int const rowso2 = rows / 2;

    /* Warp. */
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, 0, 0, colso2, rows, PPMD_NULLDRAWPROC,
        &backcolor);
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, colso2, 0, cols - colso2, rows,
        PPMD_NULLDRAWPROC, &forecolor);

    /* Woof. */
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, 0, 0, cols, rowso2, average_drawproc,
        &backcolor);
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, 0, rowso2, cols, rows - rowso2,
        average_drawproc, &forecolor);
}



static void
gingham3(pixel **     const pixels,
         unsigned int const cols,
         unsigned int const rows,
         ColorTable   const colorTable,
         pixval       const maxval) {

    bool  const colorSpec = (colorTable.count > 0);
    pixel const backcolor = colorSpec ?
                            colorTable.color[0] : randomDarkColor(maxval);
    pixel const fore1color = colorSpec ?
                            colorTable.color[1] : randomBrightColor(maxval);
    pixel const fore2color = colorSpec ?
                            colorTable.color[2] : randomBrightColor(maxval);
    unsigned int const colso4 = cols / 4;
    unsigned int const rowso4 = rows / 4;

    /* Warp. */
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, 0, 0, colso4, rows, PPMD_NULLDRAWPROC,
        &backcolor);
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, colso4, 0, colso4, rows, PPMD_NULLDRAWPROC,
        &fore1color);
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, 2 * colso4, 0, colso4, rows,
        PPMD_NULLDRAWPROC, &fore2color);
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, 3 * colso4, 0, cols - colso4, rows,
        PPMD_NULLDRAWPROC, &fore1color);

    /* Woof. */
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, 0, 0, cols, rowso4, average_drawproc,
        &backcolor);
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, 0, rowso4, cols, rowso4, average_drawproc,
        &fore1color);
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, 0, 2 * rowso4, cols, rowso4,
        average_drawproc, &fore2color);
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, 0, 3 * rowso4, cols, rows - rowso4,
        average_drawproc, &fore1color);
}



static void
madras(pixel **     const pixels,
       unsigned int const cols,
       unsigned int const rows,
       ColorTable   const colorTable,
       pixval       const maxval) {

    bool  const colorSpec = (colorTable.count > 0);
    pixel const backcolor = colorSpec ?
                            colorTable.color[0] : randomDarkColor(maxval);
    pixel const fore1color = colorSpec ?
                            colorTable.color[1] : randomBrightColor(maxval);
    pixel const fore2color = colorSpec ?
                            colorTable.color[2] : randomBrightColor(maxval);
    unsigned int const cols2  = cols * 2 / 44;
    unsigned int const rows2  = rows * 2 / 44;
    unsigned int const cols3  = cols * 3 / 44;
    unsigned int const rows3  = rows * 3 / 44;
    unsigned int const cols12 = cols - 10 * cols2 - 4 * cols3;
    unsigned int const rows12 = rows - 10 * rows2 - 4 * rows3;
    unsigned int const cols6a = cols12 / 2;
    unsigned int const rows6a = rows12 / 2;
    unsigned int const cols6b = cols12 - cols6a;
    unsigned int const rows6b = rows12 - rows6a;

    /* Warp. */
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, 0, 0, cols2, rows, PPMD_NULLDRAWPROC,
        &backcolor);
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, cols2, 0, cols3, rows, PPMD_NULLDRAWPROC,
        &fore1color);
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, cols2 + cols3, 0, cols2, rows,
        PPMD_NULLDRAWPROC, &backcolor);
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, 2 * cols2 + cols3, 0, cols2, rows,
        PPMD_NULLDRAWPROC, &fore2color);
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, 3 * cols2 + cols3, 0, cols2, rows,
        PPMD_NULLDRAWPROC, &backcolor);
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, 4 * cols2 + cols3, 0, cols6a, rows,
        PPMD_NULLDRAWPROC, &fore1color);
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, 4 * cols2 + cols3 + cols6a, 0, cols2, rows,
        PPMD_NULLDRAWPROC, &backcolor);
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, 5 * cols2 + cols3 + cols6a, 0, cols3, rows,
        PPMD_NULLDRAWPROC, &fore2color);
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, 5 * cols2 + 2 * cols3 + cols6a, 0, cols2,
        rows, PPMD_NULLDRAWPROC, &backcolor);
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, 6 * cols2 + 2 * cols3 + cols6a, 0, cols3,
        rows, PPMD_NULLDRAWPROC, &fore2color);
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, 6 * cols2 + 3 * cols3 + cols6a, 0, cols2,
        rows, PPMD_NULLDRAWPROC, &backcolor);
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, 7 * cols2 + 3 * cols3 + cols6a, 0, cols6b,
        rows, PPMD_NULLDRAWPROC, &fore1color);
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, 7 * cols2 + 3 * cols3 + cols6a + cols6b, 0,
        cols2, rows, PPMD_NULLDRAWPROC, &backcolor);
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, 8 * cols2 + 3 * cols3 + cols6a + cols6b, 0,
        cols2, rows, PPMD_NULLDRAWPROC, &fore2color);
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, 9 * cols2 + 3 * cols3 + cols6a + cols6b, 0,
        cols2, rows, PPMD_NULLDRAWPROC, &backcolor);
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, 10 * cols2 + 3 * cols3 + cols6a + cols6b,
        0, cols3, rows, PPMD_NULLDRAWPROC, &fore1color);

    /* Woof. */
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, 0, 0, cols, rows2, average_drawproc,
        &backcolor);
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, 0, rows2, cols, rows3, average_drawproc,
        &fore2color);
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, 0, rows2 + rows3, cols, rows2,
        average_drawproc, &backcolor);
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, 0, 2 * rows2 + rows3, cols, rows2,
        average_drawproc, &fore1color);
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, 0, 3 * rows2 + rows3, cols, rows2,
        average_drawproc, &backcolor);
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, 0, 4 * rows2 + rows3, cols, rows6a,
        average_drawproc, &fore2color);
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, 0, 4 * rows2 + rows3 + rows6a, cols, rows2,
        average_drawproc, &backcolor);
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, 0, 5 * rows2 + rows3 + rows6a, cols, rows3,
        average_drawproc, &fore1color);
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, 0, 5 * rows2 + 2 * rows3 + rows6a, cols,
        rows2, average_drawproc, &backcolor);
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, 0, 6 * rows2 + 2 * rows3 + rows6a, cols,
        rows3, average_drawproc, &fore1color);
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, 0, 6 * rows2 + 3 * rows3 + rows6a, cols,
        rows2, average_drawproc, &backcolor);
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, 0, 7 * rows2 + 3 * rows3 + rows6a, cols,
        rows6b, average_drawproc, &fore2color);
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, 0, 7 * rows2 + 3 * rows3 + rows6a + rows6b,
        cols, rows2, average_drawproc, &backcolor);
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, 0, 8 * rows2 + 3 * rows3 + rows6a + rows6b,
        cols, rows2, average_drawproc, &fore1color);
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, 0, 9 * rows2 + 3 * rows3 + rows6a + rows6b,
        cols, rows2, average_drawproc, &backcolor);
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, 0,
        10 * rows2 + 3 * rows3 + rows6a + rows6b,
        cols, rows3, average_drawproc, &fore2color);
}



static void
tartan(pixel **     const pixels,
       unsigned int const cols,
       unsigned int const rows,
       ColorTable   const colorTable,
       pixval       const maxval) {

    bool  const colorSpec = (colorTable.count > 0);
    pixel const backcolor = colorSpec ?
                            colorTable.color[0] : randomDarkColor(maxval);
    pixel const fore1color = colorSpec ?
                            colorTable.color[1] : randomBrightColor(maxval);
    pixel const fore2color = colorSpec ?
                            colorTable.color[2] : randomBrightColor(maxval);
    unsigned int const cols1  = cols / 22;
    unsigned int const rows1  = rows / 22;
    unsigned int const cols3  = cols * 3 / 22;
    unsigned int const rows3  = rows * 3 / 22;
    unsigned int const cols10 = cols - 3 * cols1 - 3 * cols3;
    unsigned int const rows10 = rows - 3 * rows1 - 3 * rows3;
    unsigned int const cols5a = cols10 / 2;
    unsigned int const rows5a = rows10 / 2;
    unsigned int const cols5b = cols10 - cols5a;
    unsigned int const rows5b = rows10 - rows5a;

    /* Warp. */
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, 0, 0, cols5a, rows, PPMD_NULLDRAWPROC,
        &backcolor);
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, cols5a, 0, cols1, rows, PPMD_NULLDRAWPROC,
        &fore1color);
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, cols5a + cols1, 0, cols5b, rows,
        PPMD_NULLDRAWPROC, &backcolor );
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, cols10 + cols1, 0, cols3, rows,
        PPMD_NULLDRAWPROC, &fore2color);
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, cols10 + cols1 + cols3, 0, cols1, rows,
        PPMD_NULLDRAWPROC, &backcolor);
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, cols10 + 2 * cols1 + cols3, 0, cols3, rows,
        PPMD_NULLDRAWPROC, &fore2color);
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, cols10 + 2 * cols1 + 2 * cols3, 0, cols1,
        rows, PPMD_NULLDRAWPROC, &backcolor);
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, cols10 + 3 * cols1 + 2 * cols3, 0, cols3,
        rows, PPMD_NULLDRAWPROC, &fore2color);

    /* Woof. */
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, 0, 0, cols, rows5a, average_drawproc,
        &backcolor);
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, 0, rows5a, cols, rows1, average_drawproc,
        &fore1color);
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, 0, rows5a + rows1, cols, rows5b,
        average_drawproc, &backcolor);
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, 0, rows10 + rows1, cols, rows3,
        average_drawproc, &fore2color);
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, 0, rows10 + rows1 + rows3, cols, rows1,
        average_drawproc, &backcolor);
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, 0, rows10 + 2 * rows1 + rows3, cols, rows3,
        average_drawproc, &fore2color);
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, 0, rows10 + 2 * rows1 + 2 * rows3, cols,
        rows1, average_drawproc, &backcolor);
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, 0, rows10 + 3 * rows1 + 2 * rows3, cols,
        rows3, average_drawproc, &fore2color);
}



static void
drawAndFillDiamond(pixel **     const pixels,
                   unsigned int const cols,
                   unsigned int const rows,
                   pixval       const maxval,
                   pixel        const forecolor) {

    unsigned int const colso2 = cols / 2;
    unsigned int const rowso2 = rows / 2;

    ppmd_pathbuilder * const pathBuilderP = ppmd_pathbuilder_create();

    ppmd_pathbuilder_setBegPoint(pathBuilderP,
                 ppmd_makePoint (colso2, 0));

    ppmd_pathbuilder_addLineLeg(pathBuilderP,
                 ppmd_makeLineLeg(ppmd_makePoint(cols-1, rowso2)));
    ppmd_pathbuilder_addLineLeg(pathBuilderP,
                 ppmd_makeLineLeg(ppmd_makePoint(colso2, rows-1)));
    ppmd_pathbuilder_addLineLeg(pathBuilderP,
                 ppmd_makeLineLeg(ppmd_makePoint(0,      rowso2)));
    ppmd_pathbuilder_addLineLeg(pathBuilderP,
                 ppmd_makeLineLeg(ppmd_makePoint(colso2, 0)));

    ppmd_fill_path(pixels, cols, rows, maxval,
                   ppmd_pathbuilder_pathP(pathBuilderP), forecolor);
}



static void
argyle(pixel **     const pixels,
       unsigned int const cols,
       unsigned int const rows,
       ColorTable   const colorTable,
       pixval       const maxval,
       bool         const stripes) {

    bool  const colorSpec = (colorTable.count > 0);
    pixel const backcolor = colorSpec ?
        colorTable.color[0] : randomDarkColor(maxval);
    pixel const forecolor = colorSpec ?
        colorTable.color[1] : randomBrightColor(maxval);

    /* Fill canvas with background to start */
    ppmd_filledrectangle(
        pixels, cols, rows, maxval, 0, 0, cols, rows, PPMD_NULLDRAWPROC,
        &backcolor);

    drawAndFillDiamond(pixels, cols, rows, maxval, forecolor);

    if (stripes) {
         /* Connect corners with thin stripes */
         pixel const stripecolor =
             colorSpec ? colorTable.color[2] : randomBrightColor(maxval);

         ppmd_line(pixels, cols, rows, maxval, 0, 0, cols-1, rows-1,
              PPMD_NULLDRAWPROC, (char *) &stripecolor);
         ppmd_line(pixels, cols, rows, maxval, cols-1, 0, 0, rows-1,
              PPMD_NULLDRAWPROC, (char *) &stripecolor);
    }
}



/*----------------------------------------------------------------------------
   Poles stuff
-----------------------------------------------------------------------------*/



#define MAXPOLES 500



static void
placeAndColorPolesRandomly(int *        const xs,
                           int *        const ys,
                           pixel *      const colors,
                           unsigned int const cols,
                           unsigned int const rows,
                           pixval       const maxval,
                           ColorTable * const colorTableP,
                           unsigned int const poleCt) {

    unsigned int i;

    for (i = 0; i < poleCt; ++i) {

        xs[i] = rand() % cols;
        ys[i] = rand() % rows;

        if (colorTableP->count > 0) {
            colors[i] = colorTableP->color[colorTableP->index];
            nextColor(colorTableP);
        } else
            colors[i] = randomBrightColor(maxval);
    }
}



static void
assignInterpolatedColor(pixel * const resultP,
                        pixel   const color1,
                        double  const dist1,
                        pixel   const color2,
                        double  const dist2) {

    if (dist1 == 0)
        /* pixel is a pole */
        *resultP = color1;
    else {
        double const sum = dist1 + dist2;

        pixval const r = (PPM_GETR(color1)*dist2 + PPM_GETR(color2)*dist1)/sum;
        pixval const g = (PPM_GETG(color1)*dist2 + PPM_GETG(color2)*dist1)/sum;
        pixval const b = (PPM_GETB(color1)*dist2 + PPM_GETB(color2)*dist1)/sum;

        PPM_ASSIGN(*resultP, r, g, b);
    }
}



static void
poles(pixel **     const pixels,
      unsigned int const cols,
      unsigned int const rows,
      ColorTable * const colorTableP,
      pixval       const maxval) {

    unsigned int const poleCt = MAX(2, MIN(MAXPOLES, cols * rows / 30000));

    int xs[MAXPOLES], ys[MAXPOLES];
    pixel colors[MAXPOLES];
    unsigned int row;

    placeAndColorPolesRandomly(xs, ys, colors, cols, rows, maxval,
                               colorTableP, poleCt);

    /* Interpolate points */

    for (row = 0; row < rows; ++row) {
        unsigned int col;
        for (col = 0; col < cols; ++col) {
            double dist1, dist2;
            pixel color1, color2;
            unsigned int i;

            /* Find two closest poles. */
            dist1 = dist2 = (SQR((double)cols) + SQR((double)rows));
            for (i = 0; i < poleCt; ++i) {
                double const newdist =
                    (double)((int)col - xs[i]) * ((int)col - xs[i]) +
                    (double)((int)row - ys[i]) * ((int)row - ys[i]);
                if (newdist < dist1) {
                    dist2  = dist1;
                    color2 = color1;
                    dist1  = newdist;
                    color1 = colors[i];
                } else if (newdist < dist2) {
                    dist2  = newdist;
                    color2 = colors[i];
                }
            }
            assignInterpolatedColor(&pixels[row][col],
                                    color1, dist1, color2, dist2);
        }
    }
}



/*----------------------------------------------------------------------------
   Squig stuff
-----------------------------------------------------------------------------*/

#define SQUIGS 5
#define SQ_POINTS 7
#define SQ_MAXCIRCLE_POINTS 5000

struct Squig {
    unsigned int circleCt;
    pixel        color[SQ_MAXCIRCLE_POINTS];
    ppmd_point   off[SQ_MAXCIRCLE_POINTS];
};

typedef struct {
    struct Squig * squigP;
} SqClientData;

static void
validateSquigAspect(unsigned int const cols,
                    unsigned int const rows) {

    if (cols / rows >= 25 || rows / cols >= 25)
        pm_error("Image too narrow.  Aspect ratio: %u/%u=%f "
                 "is outside accepted range: 0.04 - 25.0",
                 cols, rows, (float)cols/rows );

}



static ppmd_point
vectorSum(ppmd_point const a,
          ppmd_point const b) {

    return ppmd_makePoint(a.x + b.x, a.y + b.y);
}



static ppmd_drawprocp sqMeasureCircleDrawproc;

static void
sqMeasureCircleDrawproc(pixel**      const pixels,
                        unsigned int const cols,
                        unsigned int const rows,
                        pixval       const maxval,
                        ppmd_point   const p,
                        const void * const clientdata) {

    const SqClientData * const sqClientDataP = clientdata;

    struct Squig * const squigP = sqClientDataP->squigP;

    squigP->off[squigP->circleCt++] = p;
}



static ppmd_drawprocp sqRainbowCircleDrawproc;

static void
sqRainbowCircleDrawproc(pixel **     const pixels,
                        unsigned int const cols,
                        unsigned int const rows,
                        pixval       const maxval,
                        ppmd_point   const p,
                        const void * const clientdata) {

    const SqClientData * const sqClientDataP = clientdata;

    struct Squig * const squigP = sqClientDataP->squigP;

    unsigned int i;

    for (i = 0; i < squigP->circleCt; ++i)
        ppmd_point_drawprocp(
            pixels, cols, rows, maxval, vectorSum(p, squigP->off[i]),
            &squigP->color[i]);
}



static void
chooseSqPoleColors(ColorTable * const colorTableP,
                   pixval       const maxval,
                   pixel *      const color1P,
                   pixel *      const color2P,
                   pixel *      const color3P) {

    if (colorTableP->count > 0) {
        *color1P = colorTableP->color[colorTableP->index];
        nextColor(colorTableP);
        *color2P = colorTableP->color[colorTableP->index];
        nextColor(colorTableP);
        *color3P = colorTableP->color[colorTableP->index];
        nextColor(colorTableP);
    } else {
        *color1P = randomBrightColor(maxval);
        *color2P = randomBrightColor(maxval);
        *color3P = randomBrightColor(maxval);
    }
}



static void
sqAssignColors(unsigned int const circlecount,
               pixval       const maxval,
               ColorTable * const colorTableP,
               pixel *      const colors) {

    float const cco3 = (circlecount - 1) / 3.0;

    pixel rc1;
    pixel rc2;
    pixel rc3;
    unsigned int i;

    chooseSqPoleColors(colorTableP, maxval, &rc1, &rc2, &rc3);

    for (i = 0; i < circlecount; ++i) {
        if (i < cco3) {
            float const frac = (float)i/cco3;
            PPM_ASSIGN(colors[i],
                       (float) PPM_GETR(rc1) +
                       ((float) PPM_GETR(rc2) - (float) PPM_GETR(rc1)) * frac,
                       (float) PPM_GETG(rc1) +
                       ((float) PPM_GETG(rc2) - (float) PPM_GETG(rc1)) * frac,
                       (float) PPM_GETB(rc1) +
                       ((float) PPM_GETB(rc2) - (float) PPM_GETB(rc1)) * frac
                );
        } else if (i < 2.0 * cco3) {
            float const frac = (float)i/cco3 - 1.0;
            PPM_ASSIGN(colors[i],
                       (float) PPM_GETR(rc2) +
                       ((float) PPM_GETR(rc3) - (float) PPM_GETR(rc2)) * frac,
                       (float) PPM_GETG(rc2) +
                       ((float) PPM_GETG(rc3) - (float) PPM_GETG(rc2)) * frac,
                       (float) PPM_GETB(rc2) +
                       ((float) PPM_GETB(rc3) - (float) PPM_GETB(rc2)) * frac
                       );
        } else {
            float const frac = (float)i/cco3 - 2.0;
            PPM_ASSIGN(colors[i],
                       (float) PPM_GETR(rc3) +
                       ((float) PPM_GETR(rc1) - (float) PPM_GETR(rc3)) * frac,
                       (float) PPM_GETG(rc3) +
                       ((float) PPM_GETG(rc1) - (float) PPM_GETG(rc3)) * frac,
                       (float) PPM_GETB(rc3) +
                       ((float) PPM_GETB(rc1) - (float) PPM_GETB(rc3)) * frac
                );
        }
    }
}



static void
clearBackgroundSquig(pixel **     const pixels,
                     unsigned int const cols,
                     unsigned int const rows,
                     ColorTable * const colorTableP,
                     pixval       const maxval) {

    pixel color;

    if (colorTableP->count > 0) {
        color = colorTableP->color[0];
        colorTableP->index = 1;
    } else
        PPM_ASSIGN(color, 0, 0, 0);

    ppmd_filledrectangle(
        pixels, cols, rows, maxval, 0, 0, cols, rows, PPMD_NULLDRAWPROC,
        &color);
}



static void
chooseWrapAroundPoint(unsigned int const cols,
                      unsigned int const rows,
                      ppmd_point * const pFirstP,
                      ppmd_point * const pLastP,
                      ppmd_point * const p0P,
                      ppmd_point * const p1P,
                      ppmd_point * const p2P,
                      ppmd_point * const p3P) {

    switch (rand() % 4) {
    case 0:
        p1P->x = rand() % cols;
        p1P->y = 0;
        if (p1P->x < cols / 2)
            pFirstP->x = rand() % (p1P->x * 2 + 1);
        else
            pFirstP->x = cols - 1 - rand() % ((cols - p1P->x) * 2);
        pFirstP->y = rand() % rows;
        p2P->x = p1P->x;
        p2P->y = rows - 1;
        pLastP->x = 2 * p2P->x - pFirstP->x;
        pLastP->y = p2P->y - pFirstP->y;
        p0P->x = pLastP->x;
        p0P->y = pLastP->y - rows;
        p3P->x = pFirstP->x;
        p3P->y = pFirstP->y + rows;
        break;

    case 1:
        p2P->x = rand() % cols;
        p2P->y = 0;
        if (p2P->x < cols / 2)
            pLastP->x = rand() % (p2P->x * 2 + 1);
        else
            pLastP->x = cols - 1 - rand() % ((cols - p2P->x) * 2);
        pLastP->y = rand() % rows;
        p1P->x = p2P->x;
        p1P->y = rows - 1;
        pFirstP->x = 2 * p1P->x - pLastP->x;
        pFirstP->y = p1P->y - pLastP->y;
        p0P->x = pLastP->x;
        p0P->y = pLastP->y + rows;
        p3P->x = pFirstP->x;
        p3P->y = pFirstP->y - rows;
        break;

    case 2:
        p1P->x = 0;
        p1P->y = rand() % rows;
        pFirstP->x = rand() % cols;
        if (p1P->y < rows / 2)
            pFirstP->y = rand() % (p1P->y * 2 + 1);
        else
            pFirstP->y = rows - 1 - rand() % ((rows - p1P->y) * 2);
        p2P->x = cols - 1;
        p2P->y = p1P->y;
        pLastP->x = p2P->x - pFirstP->x;
        pLastP->y = 2 * p2P->y - pFirstP->y;
        p0P->x = pLastP->x - cols;
        p0P->y = pLastP->y;
        p3P->x = pFirstP->x + cols;
        p3P->y = pFirstP->y;
        break;

    case 3:
        p2P->x = 0;
        p2P->y = rand() % rows;
        pLastP->x = rand() % cols;
        if (p2P->y < rows / 2)
            pLastP->y = rand() % (p2P->y * 2 + 1);
        else
            pLastP->y = rows - 1 - rand() % ((rows - p2P->y) * 2);
        p1P->x = cols - 1;
        p1P->y = p2P->y;
        pFirstP->x = p1P->x - pLastP->x;
        pFirstP->y = 2 * p1P->y - pLastP->y;
        p0P->x = pLastP->x + cols;
        p0P->y = pLastP->y;
        p3P->x = pFirstP->x - cols;
        p3P->y = pFirstP->y;
        break;
    }
}



static void
squig(pixel **     const pixels,
      unsigned int const cols,
      unsigned int const rows,
      ColorTable * const colorTableP,
      pixval       const maxval) {

    int i;

    validateSquigAspect(cols, rows);

    clearBackgroundSquig(pixels, cols, rows, colorTableP, maxval);

    /* Draw the squigs. */
    ppmd_setlinetype(PPMD_LINETYPE_NODIAGS);
    ppmd_setlineclip(0);

    for (i = SQUIGS; i > 0; --i) {
        unsigned int const radius = (cols + rows) / 2 / (25 + i * 2);

        struct Squig squig;

        SqClientData sqClientData;

        ppmd_point c[SQ_POINTS];
        ppmd_point p0, p1, p2, p3;

        squig.circleCt = 0;

        sqClientData.squigP = &squig;

        ppmd_circlep(pixels, cols, rows, maxval,
                     ppmd_makePoint(0, 0), radius,
                     sqMeasureCircleDrawproc, &sqClientData);
        sqAssignColors(squig.circleCt, maxval, colorTableP, squig.color);

        chooseWrapAroundPoint(cols, rows, &c[0], &c[SQ_POINTS-1],
                              &p0, &p1, &p2, &p3);

        {
            /* Do the middle points */
            unsigned int j;

            for (j = 1; j < SQ_POINTS - 1; ++j) {
              /* validateSquigAspect() assures that
                 cols - 2 * radius, rows -2 * radius are positive
              */
                c[j].x = (rand() % (cols - 2 * radius)) + radius;
                c[j].y = (rand() % (rows - 2 * radius)) + radius;
            }
        }

        ppmd_linep(
            pixels, cols, rows, maxval, p0, p1,
            sqRainbowCircleDrawproc, &sqClientData);
        ppmd_polysplinep(
            pixels, cols, rows, maxval, p1, SQ_POINTS, c, p2,
            sqRainbowCircleDrawproc, &sqClientData);
        ppmd_linep(
            pixels, cols, rows, maxval, p2, p3,
            sqRainbowCircleDrawproc, &sqClientData);
    }
}



int
main(int argc, const char ** argv) {

    struct CmdlineInfo cmdline;
    pixel ** pixels;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    validateComputableDimensions(cmdline.width, cmdline.height);

    srand(cmdline.randomseedSpec ? cmdline.randomseed : pm_randseed());

    pixels = ppm_allocarray(cmdline.width, cmdline.height);

    switch (cmdline.basePattern) {
    case PAT_GINGHAM2:
        gingham2(pixels, cmdline.width, cmdline.height,
                 cmdline.colorTable, PPM_MAXMAXVAL);
        break;

    case PAT_GINGHAM3:
        gingham3(pixels, cmdline.width, cmdline.height,
                 cmdline.colorTable, PPM_MAXMAXVAL);
        break;

    case PAT_MADRAS:
        madras(pixels, cmdline.width, cmdline.height,
               cmdline.colorTable, PPM_MAXMAXVAL);
        break;

    case PAT_TARTAN:
        tartan(pixels, cmdline.width, cmdline.height,
               cmdline.colorTable, PPM_MAXMAXVAL);
        break;

    case PAT_ARGYLE1:
        argyle(pixels, cmdline.width, cmdline.height,
               cmdline.colorTable, PPM_MAXMAXVAL, FALSE);
        break;

    case PAT_ARGYLE2:
        argyle(pixels, cmdline.width, cmdline.height,
               cmdline.colorTable, PPM_MAXMAXVAL, TRUE);
        break;

    case PAT_POLES:
        poles(pixels, cmdline.width, cmdline.height,
              &cmdline.colorTable, PPM_MAXMAXVAL);
        break;

    case PAT_SQUIG:
        squig(pixels, cmdline.width, cmdline.height,
              &cmdline.colorTable, PPM_MAXMAXVAL);
        break;

    case PAT_CAMO:
        camo(pixels, cmdline.width, cmdline.height,
             &cmdline.colorTable, PPM_MAXMAXVAL, 0);
        break;

    case PAT_ANTICAMO:
        camo(pixels, cmdline.width, cmdline.height,
             &cmdline.colorTable, PPM_MAXMAXVAL, 1);
        break;

    default:
        pm_error("can't happen!");
    }

    ppm_writeppm(stdout, pixels, cmdline.width, cmdline.height,
                 PPM_MAXMAXVAL, 0);

    ppm_freearray(pixels, cmdline.height);

    freeCmdline(cmdline);

    return 0;
}



