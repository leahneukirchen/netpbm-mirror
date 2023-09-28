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



struct CmdlineInfo {
  /* All the information the user supplied in the command line,
     in a form easy for the program to use.
  */
    unsigned int left, right, top, bottom;
    unsigned int leftSpec, rightSpec, topSpec, bottomSpec;
    unsigned int width;
    unsigned int height;
    unsigned int var;
    const char * bg;  /* Null if not specified */
    const char * fg;  /* Null if not specified */
    unsigned int randomseed;
    unsigned int randomseedSpec;
    unsigned int verbose;
};



static void
parseCommandLine(int argc, const char ** argv,
                 struct CmdlineInfo * const cmdlineP) {

    unsigned int widthSpec, heightSpec, bgSpec, fgSpec, varSpec;

    optEntry * option_def;
        /* Instructions to OptParseOptions2 on how to parse our options.    */
    optStruct3 opt;

    unsigned int option_def_index;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENTRY */
    OPTENT3(0, "width",       OPT_UINT,   &cmdlineP->width,
            &widthSpec, 0);
    OPTENT3(0, "height",      OPT_UINT,   &cmdlineP->height,
            &heightSpec, 0);
    OPTENT3(0, "left",        OPT_UINT,   &cmdlineP->left,
            &cmdlineP->leftSpec, 0);
    OPTENT3(0, "right",       OPT_UINT,   &cmdlineP->right,
            &cmdlineP->rightSpec, 0);
    OPTENT3(0, "top",         OPT_UINT,   &cmdlineP->top,
            &cmdlineP->topSpec, 0);
    OPTENT3(0, "bottom",      OPT_UINT,   &cmdlineP->bottom,
            &cmdlineP->bottomSpec, 0);
    OPTENT3(0, "bg",          OPT_STRING, &cmdlineP->bg,
            &bgSpec, 0);
    OPTENT3(0, "fg",          OPT_STRING, &cmdlineP->fg,
            &fgSpec, 0);
    OPTENT3(0, "var",         OPT_UINT,   &cmdlineP->var,
            &varSpec, 0);
    OPTENT3(0, "randomseed",  OPT_UINT,   &cmdlineP->randomseed,
            &cmdlineP->randomseedSpec, 0);
    OPTENT3(0, "init",        OPT_UINT,   &cmdlineP->randomseed,
            &cmdlineP->randomseedSpec, 0);
    OPTENT3(0, "verbose",     OPT_FLAG,   NULL,
            &cmdlineP->verbose, 0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We have no parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);

    if (!widthSpec)
        cmdlineP->width = 100;
    if (!heightSpec)
        cmdlineP->height = 100;
    if (!bgSpec)
        cmdlineP->bg = NULL;
    if (!fgSpec)
        cmdlineP->fg = NULL;
    if (!varSpec)
        cmdlineP->var = 10;

    if (cmdlineP->topSpec && cmdlineP->top > cmdlineP->height)
        pm_error("-top value too large.  Max is %u", cmdlineP->height);
    if (cmdlineP->bottomSpec && cmdlineP->bottom > cmdlineP->height)
        pm_error("-bottom value too large.  Max is %u", cmdlineP->height);
    if (cmdlineP->leftSpec && cmdlineP->left > cmdlineP->width)
        pm_error("-left value too large.  Max is %u", cmdlineP->width);
    if (cmdlineP->rightSpec && cmdlineP->right > cmdlineP->width)
        pm_error("-right value too large.  Max is %u", cmdlineP->width);

    if (argc-1 != 0)
        pm_error("There are no arguments.  You specified %d.", argc-1);

    free(option_def);
}



static int
mean(int const a,
     int const b) {

    return (a + b) / 2;
}



static void
reportParameters(struct CmdlineInfo const cmdline,
                 pixel              const bgcolor,
                 pixel              const fgcolor) {

    pm_message("width is %d, height is %d, variance is %d.",
               cmdline.width, cmdline.height, cmdline.var);
    if (cmdline.leftSpec)
        pm_message("ragged left border is required");
    if (cmdline.rightSpec)
        pm_message("ragged right border is required");
    if (cmdline.topSpec)
        pm_message("ragged top border is required");
    if (cmdline.bottomSpec)
        pm_message("ragged bottom border is required");
    pm_message("background is %s",
               ppm_colorname(&bgcolor, PPM_MAXMAXVAL, 1));
    pm_message("foreground is %s",
               ppm_colorname(&fgcolor, PPM_MAXMAXVAL, 1));
    if (cmdline.randomseedSpec)
        pm_message("pm_rand() initialized with seed %u",
                   cmdline.randomseed);
}



static void
makeAllForegroundColor(pixel **     const pixels,
                       unsigned int const rows,
                       unsigned int const cols,
                       pixel        const fgcolor) {

    pixval const r = PPM_GETR(fgcolor);
    pixval const g = PPM_GETG(fgcolor);
    pixval const b = PPM_GETB(fgcolor);

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
         unsigned int       const width,
         unsigned int       const var,
         pixel              const bgcolor,
         struct pm_randSt * const randStP) {

    if (r1 + 1 != r2) {
        int const rm = mean(r1, r2);
        int const cm = mean(c1, c2) +
            (int)floor(((float)pm_drand(randStP) - 0.5) * var + 0.5);

        unsigned int c;

        for (c = 0; c < MIN(width, MAX(0, cm)); ++c)
            pixels[rm][c] = bgcolor;

        procLeft(pixels, r1, rm, c1, cm, width, var, bgcolor, randStP);
        procLeft(pixels, rm, r2, cm, c2, width, var, bgcolor, randStP);
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
          pixel              const bgcolor,
          struct pm_randSt * const randStP) {

    if (r1 + 1 != r2) {
        int const rm = mean(r1, r2);
        int const cm = mean(c1, c2) +
            (int)floor(((float)pm_drand(randStP) - 0.5) * var + 0.5);

        unsigned int c;

        for (c = MAX(0, cm); c < width; ++c)
            pixels[rm][c] = bgcolor;

        procRight(pixels, r1, rm, c1, cm, width, var, bgcolor, randStP);
        procRight(pixels, rm, r2, cm, c2, width, var, bgcolor, randStP);
    }
}



static void
procTop(pixel **           const pixels,
        int                const c1,
        int                const c2,
        int                const r1,
        int                const r2,
        unsigned int       const height,
        unsigned int       const var,
        pixel              const bgcolor,
        struct pm_randSt * const randStP) {

    if (c1 + 1 != c2) {
        int const cm = mean(c1, c2);
        int const rm = mean(r1, r2) +
            (int)floor(((float)pm_drand(randStP) - 0.5) * var + 0.5);

        unsigned int r;

        for (r = 0; r < MIN(height, MAX(0, rm)); ++r)
            pixels[r][cm] = bgcolor;

        procTop(pixels, c1, cm, r1, rm, height, var, bgcolor, randStP);
        procTop(pixels, cm, c2, rm, r2, height, var, bgcolor, randStP);
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
           pixel              const bgcolor,
           struct pm_randSt * const randStP) {

    if (c1 + 1 != c2) {
        int const cm = mean(c1, c2);
        int const rm = mean(r1, r2) +
            (int)floor(((float)pm_drand(randStP) - 0.5) * var + 0.5);

        unsigned int r;

        for (r = MAX(0, rm); r < height; ++r)
            pixels[r][cm] = bgcolor;

        procBottom(pixels, c1, cm, r1, rm, height, var, bgcolor, randStP);
        procBottom(pixels, cm, c2, rm, r2, height, var, bgcolor, randStP);
    }
}



static void
makeRaggedLeftBorder(pixel **           const pixels,
                     unsigned int       const rows,
                     unsigned int       const cols,
                     bool               const leftSpec,
                     unsigned int       const left,
                     unsigned int       const var,
                     pixel              const bgcolor,
                     struct pm_randSt * const randStP) {

    if (leftSpec) {
        int const leftC1 = left;
        int const leftC2 = left;
        int const leftR1 = 0;
        int const leftR2 = rows - 1;

        unsigned int col;

        for (col = 0; col < leftC1; ++col)
            pixels[leftR1][col] = bgcolor;
        for (col = 0; col < leftC2; ++col)
            pixels[leftR2][col] = bgcolor;

        procLeft(pixels, leftR1, leftR2, leftC1, leftC2, cols, var,
                 bgcolor, randStP);
    }
}



static void
makeRaggedRightBorder(pixel **           const pixels,
                      unsigned int       const rows,
                      unsigned int       const cols,
                      bool               const rightSpec,
                      unsigned int       const right,
                      unsigned int       const width,
                      unsigned int       const var,
                      pixel              const bgcolor,
                      struct pm_randSt * const randStP) {

    if (rightSpec) {
        int const rightC1 = cols - right - 1;
        int const rightC2 = cols - right - 1;
        int const rightR1 = 0;
        int const rightR2 = rows - 1;

        unsigned int col;

        for (col = rightC1; col < cols; ++col)
            pixels[rightR1][col] = bgcolor;
        for (col = rightC2; col < cols; ++col)
            pixels[rightR2][col] = bgcolor;

        procRight(pixels, rightR1, rightR2, rightC1, rightC2, width, var,
                  bgcolor, randStP);
    }
}



static void
makeRaggedTopBorder(pixel **           const pixels,
                    unsigned int       const rows,
                    unsigned int       const cols,
                    bool               const topSpec,
                    unsigned int       const top,
                    unsigned int       const var,
                    pixel              const bgcolor,
                    struct pm_randSt * const randStP) {

    if (topSpec) {
        unsigned int const topR1 = top;
        unsigned int const topR2 = top;
        unsigned int const topC1 = 0;
        unsigned int const topC2 = cols - 1;

        unsigned int row;

        for (row = 0; row < topR1; ++row)
            pixels[row][topC1] = bgcolor;
        for (row = 0; row < topR2; ++row)
            pixels[row][topC2] = bgcolor;

        procTop(pixels, topC1, topC2, topR1, topR2, rows,
                var, bgcolor, randStP);
    }
}



static void
makeRaggedBottomBorder(pixel **            const pixels,
                       unsigned int        const rows,
                       unsigned int        const cols,
                       bool                const bottomSpec,
                       unsigned int        const bottom,
                       unsigned int        const height,
                       unsigned int        const var,
                       pixel               const bgcolor,
                       struct pm_randSt *  const randStP) {

    if (bottomSpec) {
        unsigned int const bottomR1 = rows - bottom - 1;
        unsigned int const bottomR2 = rows - bottom - 1;
        unsigned int const bottomC1 = 0;
        unsigned int const bottomC2 = cols - 1;

        unsigned int row;

        for (row = bottomR1; row < rows; ++row)
            pixels[row][bottomC1] = bgcolor;
        for (row = bottomR2; row < rows; ++row)
            pixels[row][bottomC2] = bgcolor;

        procBottom(pixels, bottomC1, bottomC2, bottomR1, bottomR2,
                   height, var, bgcolor, randStP);
    }
}



int
main(int argc, const char ** const argv) {

    struct CmdlineInfo cmdline;
    pixel bgcolor, fgcolor;
    struct pm_randSt randSt;
    static pixel** pixels;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    pm_randinit(&randSt);
    pm_srand(&randSt,
             cmdline.randomseedSpec ? cmdline.randomseed : pm_randseed());

    if (cmdline.bg)
        bgcolor = ppm_parsecolor(cmdline.bg, PPM_MAXMAXVAL);
    else
        PPM_ASSIGN(bgcolor, 0, 0, 0);

    if (cmdline.fg)
        fgcolor = ppm_parsecolor(cmdline.fg, PPM_MAXMAXVAL);
    else
        PPM_ASSIGN(fgcolor, PPM_MAXMAXVAL, PPM_MAXMAXVAL, PPM_MAXMAXVAL);

    if (cmdline.verbose)
        reportParameters(cmdline, bgcolor, fgcolor);

    pixels = ppm_allocarray(cmdline.width, cmdline.height);

    makeAllForegroundColor(pixels, cmdline.height, cmdline.width, fgcolor);

    makeRaggedLeftBorder(pixels, cmdline.height, cmdline.width,
                         cmdline.leftSpec, cmdline.left,
                         cmdline.var, bgcolor, &randSt);

    makeRaggedRightBorder(pixels, cmdline.height, cmdline.width,
                          cmdline.rightSpec, cmdline.right,
                          cmdline.width, cmdline.var, bgcolor, &randSt);

    makeRaggedTopBorder(pixels, cmdline.height, cmdline.width,
                        cmdline.topSpec, cmdline.top,
                        cmdline.var, bgcolor, &randSt);

    makeRaggedBottomBorder(pixels, cmdline.height, cmdline.width,
                           cmdline.bottomSpec, cmdline.bottom,
                           cmdline.height, cmdline.var, bgcolor, &randSt);

    pm_randterm(&randSt);

    /* Write pixmap */
    ppm_writeppm(stdout, pixels, cmdline.width, cmdline.height,
                 PPM_MAXMAXVAL, 0);

    ppm_freearray(pixels, cmdline.height);

    pm_close(stdout);

    return 0;
}


