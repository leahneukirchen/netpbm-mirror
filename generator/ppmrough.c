/* ppmrough.c - create a PPM image containing two colors with a ragged
   border between them
**
** Copyright (C) 2002 by Eckard Specht.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.  */

#include <stdlib.h>
#include <math.h>
#include <sys/time.h>

#include "pm_c_util.h"
#include "mallocvar.h"
#include "rand.h"
#include "shhopt.h"
#include "ppm.h"

static pixval BG_RED, BG_GREEN, BG_BLUE;


struct CmdlineInfo {
  /* All the information the user supplied in the command line,
     in a form easy for the program to use.
  */
    unsigned int left, right, top, bottom;
    unsigned int width, height, var;
    const char * bg_rgb;
    const char * fg_rgb;
    unsigned int randomseed;
    unsigned int randomseedSpec;
    unsigned int verbose;
};



static void
parseCommandLine(int argc, const char ** argv,
                 struct CmdlineInfo * const cmdlineP) {

    optEntry * option_def;
        /* Instructions to OptParseOptions2 on how to parse our options.    */
    optStruct3 opt;

    unsigned int option_def_index;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENTRY */
    OPTENT3(0, "width",       OPT_UINT,   &cmdlineP->width,   NULL, 0);
    OPTENT3(0, "height",      OPT_UINT,   &cmdlineP->height,  NULL, 0);
    OPTENT3(0, "left",        OPT_UINT,   &cmdlineP->left,    NULL, 0);
    OPTENT3(0, "right",       OPT_UINT,   &cmdlineP->right,   NULL, 0);
    OPTENT3(0, "top",         OPT_UINT,   &cmdlineP->top,     NULL, 0);
    OPTENT3(0, "bottom",      OPT_UINT,   &cmdlineP->bottom,  NULL, 0);
    OPTENT3(0, "bg",          OPT_STRING, &cmdlineP->bg_rgb,  NULL, 0);
    OPTENT3(0, "fg",          OPT_STRING, &cmdlineP->fg_rgb,  NULL, 0);
    OPTENT3(0, "var",         OPT_UINT,   &cmdlineP->var,     NULL, 0);
    OPTENT3(0, "randomseed",  OPT_UINT,   &cmdlineP->randomseed,
            &cmdlineP->randomseedSpec, 0);
    OPTENT3(0, "init",        OPT_UINT,   &cmdlineP->randomseed,
            &cmdlineP->randomseedSpec, 0);
    OPTENT3(0, "verbose",     OPT_FLAG,   NULL, &cmdlineP->verbose, 0);

    /* Set the defaults */
    cmdlineP->width = 100;
    cmdlineP->height = 100;
    cmdlineP->left = cmdlineP->right = cmdlineP->top = cmdlineP->bottom = -1;
    cmdlineP->bg_rgb = NULL;
    cmdlineP->fg_rgb = NULL;
    cmdlineP->var = 10;

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We have no parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);

    if (argc-1 != 0)
        pm_error("There are no arguments.  You specified %d.", argc-1);

    free(option_def);
}



static void
makeAllForegroundColor(pixel **     const pixels,
                       unsigned int const rows,
                       unsigned int const cols,
                       pixval       const r,
                       pixval       const g,
                       pixval       const b) {

    unsigned int row;

    for (row = 0; row < rows; ++row) {
        unsigned int col;

        for (col = 0; col < cols; ++col)
            PPM_ASSIGN(pixels[row][col], r, g, b);
    }
}



static void
procLeft(pixel **           const pixels,
         int                const r1,
         int                const r2,
         int                const c1,
         int                const c2,
         unsigned int       const var,
         struct pm_randSt * const randStP) {

    if (r1 + 1 != r2) {
        int const rm = (r1 + r2) >> 1;
        int const cm = ((c1 + c2) >> 1) +
            (int)floor(((float)pm_rand(randStP) / RAND_MAX - 0.5) * var + 0.5);

        int c;

        for (c = 0; c < cm; c++)
            PPM_ASSIGN(pixels[rm][c], BG_RED, BG_GREEN, BG_BLUE);

        procLeft(pixels, r1, rm, c1, cm, var, randStP);
        procLeft(pixels, rm, r2, cm, c2, var, randStP);
    }
}



static void
procRight(pixel **           const pixels,
          int                const r1,
          int                const r2,
          int                const c1,
          int                const c2,
          unsigned int       const width,
          unsigned int       const var,
          struct pm_randSt * const randStP) {

    if (r1 + 1 != r2) {
        int const rm = (r1 + r2) >> 1;
        int const cm = ((c1 + c2) >> 1) +
            (int)floor(((float)pm_rand(randStP) / RAND_MAX - 0.5) * var + 0.5);

        int c;

        for (c = cm; c < width; c++)
            PPM_ASSIGN(pixels[rm][c], BG_RED, BG_GREEN, BG_BLUE);

        procRight(pixels, r1, rm, c1, cm, width, var, randStP);
        procRight(pixels, rm, r2, cm, c2, width, var, randStP);
    }
}



static void
procTop(pixel **           const pixels,
        int                const c1,
        int                const c2,
        int                const r1,
        int                const r2,
        unsigned int       const var,
        struct pm_randSt * const randStP) {

    if (c1 + 1 != c2) {
        int const cm = (c1 + c2) >> 1;
        int const rm = ((r1 + r2) >> 1) +
            (int)floor(((float)pm_rand(randStP) / RAND_MAX - 0.5) * var + 0.5);

        int r;

        for (r = 0; r < rm; r++)
            PPM_ASSIGN(pixels[r][cm], BG_RED, BG_GREEN, BG_BLUE);

        procTop(pixels, c1, cm, r1, rm, var, randStP);
        procTop(pixels, cm, c2, rm, r2, var, randStP);
    }
}



static void
procBottom(pixel **           const pixels,
           int                const c1,
           int                const c2,
           int                const r1,
           int                const r2,
           unsigned int       const height,
           unsigned int       const var,
           struct pm_randSt * const randStP) {

    if (c1 + 1 != c2) {
        int const cm = (c1 + c2) >> 1;
        int const rm = ((r1 + r2) >> 1) +
            (int)floor(((float)pm_rand(randStP) / RAND_MAX - 0.5) * var + 0.5);

        int r;

        for (r = rm; r < height; ++r)
            PPM_ASSIGN(pixels[r][cm], BG_RED, BG_GREEN, BG_BLUE);

        procBottom(pixels, c1, cm, r1, rm, height, var, randStP);
        procBottom(pixels, cm, c2, rm, r2, height, var, randStP);
    }
}



static void
makeRaggedLeftBorder(pixel **           const pixels,
                     unsigned int       const rows,
                     unsigned int       const cols,
                     unsigned int       const left,
                     unsigned int       const var,
                     struct pm_randSt * const randStP) {

    if (left >= 0) {
        int const leftC1 = left;
        int const leftC2 = left;
        int const leftR1 = 0;
        int const leftR2 = rows - 1;

        unsigned int col;

        for (col = 0; col < leftC1; ++col)
            PPM_ASSIGN(pixels[leftR1][col], BG_RED, BG_GREEN, BG_BLUE);
        for (col = 0; col < leftC2; ++col)
            PPM_ASSIGN(pixels[leftR2][col], BG_RED, BG_GREEN, BG_BLUE);

        procLeft(pixels, leftR1, leftR2, leftC1, leftC2, var, randStP);
    }
}



static void
makeRaggedRightBorder(pixel **           const pixels,
                      unsigned int       const rows,
                      unsigned int       const cols,
                      unsigned int       const right,
                      unsigned int       const width,
                      unsigned int       const var,
                      struct pm_randSt * const randStP) {

    if (right >= 0) {
        int const rightC1 = cols - right - 1;
        int const rightC2 = cols - right - 1;
        int const rightR1 = 0;
        int const rightR2 = rows - 1;

        unsigned int col;

        for (col = rightC1; col < cols; ++col)
            PPM_ASSIGN(pixels[rightR1][col], BG_RED, BG_GREEN, BG_BLUE);
        for (col = rightC2; col < cols; ++col)
            PPM_ASSIGN(pixels[rightR2][col], BG_RED, BG_GREEN, BG_BLUE);

        procRight(pixels, rightR1, rightR2, rightC1, rightC2, width, var,
                  randStP);
    }
}



static void
makeRaggedTopBorder(pixel **           const pixels,
                    unsigned int       const rows,
                    unsigned int       const cols,
                    unsigned int       const top,
                    unsigned int       const var,
                    struct pm_randSt * const randStP) {

    if (top >= 0) {
        unsigned int const topR1 = top;
        unsigned int const topR2 = top;
        unsigned int const topC1 = 0;
        unsigned int const topC2 = cols - 1;

        unsigned int row;

        for (row = 0; row < topR1; ++row)
            PPM_ASSIGN(pixels[row][topC1], BG_RED, BG_GREEN, BG_BLUE);
        for (row = 0; row < topR2; ++row)
            PPM_ASSIGN(pixels[row][topC2], BG_RED, BG_GREEN, BG_BLUE);

        procTop(pixels, topC1, topC2, topR1, topR2, var, randStP);
    }
}



static void
makeRaggedBottomBorder(pixel **            const pixels,
                       unsigned int        const rows,
                       unsigned int        const cols,
                       unsigned int        const bottom,
                       unsigned int        const height,
                       unsigned int        const var,
                       struct pm_randSt *  const randStP) {

    if (bottom >= 0) {
        unsigned int const bottomR1 = rows - bottom - 1;
        unsigned int const bottomR2 = rows - bottom - 1;
        unsigned int const bottomC1 = 0;
        unsigned int const bottomC2 = cols - 1;

        unsigned int row;

        for (row = bottomR1; row < rows; ++row)
            PPM_ASSIGN(pixels[row][bottomC1], BG_RED, BG_GREEN, BG_BLUE);
        for (row = bottomR2; row < rows; ++row)
            PPM_ASSIGN(pixels[row][bottomC2], BG_RED, BG_GREEN, BG_BLUE);

        procBottom(pixels, bottomC1, bottomC2, bottomR1, bottomR2,
                   height, var, randStP);
    }
}



int
main(int argc, const char * argv[]) {

    struct CmdlineInfo cmdline;
    pixel bgcolor, fgcolor;
    pixval fg_red, fg_green, fg_blue;
    int rows, cols;
    int left, right, top, bottom;
    struct pm_randSt randSt;
    static pixel** pixels;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    pm_randinit(&randSt);
    pm_srand(&randSt,
             cmdline.randomseedSpec ? cmdline.randomseed : pm_randseed());

    cols = cmdline.width;
    rows = cmdline.height;
    left = cmdline.left;
    right = cmdline.right;
    top = cmdline.top;
    bottom = cmdline.bottom;

    if (cmdline.bg_rgb)
        bgcolor = ppm_parsecolor(cmdline.bg_rgb, PPM_MAXMAXVAL);
    else
        PPM_ASSIGN(bgcolor, 0, 0, 0);
    BG_RED = PPM_GETR(bgcolor);
    BG_GREEN = PPM_GETG(bgcolor);
    BG_BLUE = PPM_GETB(bgcolor);

    if (cmdline.fg_rgb)
        fgcolor = ppm_parsecolor(cmdline.fg_rgb, PPM_MAXMAXVAL);
    else
        PPM_ASSIGN(fgcolor, PPM_MAXMAXVAL, PPM_MAXMAXVAL, PPM_MAXMAXVAL);
    fg_red = PPM_GETR(fgcolor);
    fg_green = PPM_GETG(fgcolor);
    fg_blue = PPM_GETB(fgcolor);

    if (cmdline.verbose) {
        pm_message("width is %d, height is %d, variance is %d.",
                   cols, rows, cmdline.var);
        if (left >= 0)
            pm_message("ragged left border is required");
        if (right >= 0)
            pm_message("ragged right border is required");
        if (top >= 0)
            pm_message("ragged top border is required");
        if (bottom >= 0)
            pm_message("ragged bottom border is required");
        pm_message("background is %s",
                   ppm_colorname(&bgcolor, PPM_MAXMAXVAL, 1));
        pm_message("foreground is %s",
                   ppm_colorname(&fgcolor, PPM_MAXMAXVAL, 1));
        if (cmdline.randomseedSpec)
            pm_message("pm_rand() initialized with seed %u",
                       cmdline.randomseed);
    }

    pixels = ppm_allocarray(cols, rows);

    makeAllForegroundColor(pixels, rows, cols, fg_red, fg_green, fg_blue);

    makeRaggedLeftBorder(pixels, rows, cols, left, cmdline.var, &randSt);

    makeRaggedRightBorder(pixels, rows, cols, right,
                          cmdline.width, cmdline.var, &randSt);

    makeRaggedTopBorder(pixels, rows, cols, top, cmdline.var, &randSt);

    makeRaggedBottomBorder(pixels, rows, cols, bottom,
                           cmdline.height, cmdline.var, &randSt);

    pm_randterm(&randSt);

    /* Write pixmap */
    ppm_writeppm(stdout, pixels, cols, rows, PPM_MAXMAXVAL, 0);

    ppm_freearray(pixels, rows);

    pm_close(stdout);

    return 0;
}


