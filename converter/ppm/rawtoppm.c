/* rawtoppm.c - convert raw RGB bytes into a PBM image
**
** Copyright (C) 1991 by Jef Poskanzer.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

#include <stdbool.h>

#include "pm.h"
#include "mallocvar.h"
#include "shhopt.h"
#include "ppm.h"

enum Order { ORD_RGB, ORD_RBG, ORD_GRB, ORD_GBR, ORD_BRG, ORD_BGR };

enum Interleave { INT_PIX, INT_ROW };



struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char *    inputFileName;  /* File name of input file */
    unsigned int    cols;
    unsigned int    rows;
    unsigned int    headerskip;
    unsigned int    rowskip;
    enum Order      order;
    enum Interleave interleave;
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

    unsigned int interpixel, interrow;
    unsigned int rgb, rbg, grb, gbr, brg, bgr;
    unsigned int headerskipSpec, rowskipSpec;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0,   "headerskip",  OPT_UINT,   &cmdlineP->headerskip,
            &headerskipSpec,                0);
    OPTENT3(0,   "rowskip",     OPT_UINT,   &cmdlineP->rowskip,
            &rowskipSpec,                   0);
    OPTENT3(0,   "interpixel",  OPT_FLAG,   NULL,
            &interpixel,          0);
    OPTENT3(0,   "interrow",    OPT_FLAG,   NULL,
            &interrow,            0);
    OPTENT3(0,   "rgb",         OPT_FLAG,   NULL,
            &rgb,                 0);
    OPTENT3(0,   "rbg",         OPT_FLAG,   NULL,
            &rbg,                 0);
    OPTENT3(0,   "grb",         OPT_FLAG,   NULL,
            &grb,                 0);
    OPTENT3(0,   "gbr",         OPT_FLAG,   NULL,
            &gbr,                 0);
    OPTENT3(0,   "brg",         OPT_FLAG,   NULL,
            &brg,                 0);
    OPTENT3(0,   "bgr",         OPT_FLAG,   NULL,
            &bgr,                 0);

    opt.opt_table = option_def;
    opt.short_allowed = false;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = false;  /* We have no parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (!headerskipSpec)
        cmdlineP->headerskip = 0;

    if (!rowskipSpec)
        cmdlineP->rowskip = 0;

    if (rgb + rbg + grb + gbr + brg + bgr > 1) {
        pm_error("You cannot specify more than one of "
                 "-rgb, -rbg, -grb, -gbr, -brg, and -bgr");
    } else {
        if (rgb)
            cmdlineP->order = ORD_RGB;
        else if (rbg)
            cmdlineP->order = ORD_RBG;
        else if (grb)
            cmdlineP->order = ORD_GRB;
        else if (gbr)
            cmdlineP->order = ORD_GBR;
        else if (brg)
            cmdlineP->order = ORD_BRG;
        else if (bgr)
            cmdlineP->order = ORD_BGR;
        else
            cmdlineP->order = ORD_RGB;
    }

    if (interpixel && interrow)
        pm_error("You cannot specify both -interpixel and -interrow");
    else {
        if (interpixel)
            cmdlineP->interleave = INT_PIX;
        else if (interrow)
            cmdlineP->interleave = INT_ROW;
        else
            cmdlineP->interleave = INT_PIX;
    }

    if (argc-1 < 2)
        pm_error("You must specify width and height in pixels as arguments");
    else {
        cmdlineP->cols = pm_parse_width(argv[1]);
        cmdlineP->rows = pm_parse_height(argv[2]);

        if (argc-1 < 3)
            cmdlineP->inputFileName = "-";
        else {
            cmdlineP->inputFileName = argv[3];

            if (argc-1 > 3) {
                pm_error("Too many arguments (%u).  "
                         "The only possible non-option arguments are "
                         "width, height, and input file name", argc-1);
            }
        }
    }
    free(option_def);
}



static void
skipHeader(FILE *       const ifP,
           unsigned int const headerskip) {

    unsigned int i;

    for (i = 0; i < headerskip; ++i) {
        int const val = getc(ifP);
        if (val == EOF) {
            pm_error("EOF or read error skipping byte %u of header "
                     "per -headerskip", i);
        }
    }
}



static void
dorowskip(FILE *       const ifP,
          unsigned int const rowskip) {

    unsigned int i;

    for (i = 0; i < rowskip; ++i) {
        int const val = getc(ifP);

        if (val == EOF)
            pm_error("EOF / read error");
    }
}



static void
doRowPixInterleave(pixel *      const pixrow,
                   unsigned int const cols,
                   FILE *       const ifP,
                   enum Order   const order,
                   unsigned int const rowskip) {

    unsigned int col;

    for (col = 0; col < cols; ++col) {
        int const val1 = getc(ifP);
        int const val2 = getc(ifP);
        int const val3 = getc(ifP);

        if (val1 == EOF)
            pm_error("EOF / read error");
        if (val2 == EOF)
            pm_error("EOF / read error");
        if (val3 == EOF)
            pm_error("EOF / read error");

        switch (order) {
        case ORD_RGB:
            PPM_ASSIGN(pixrow[col], val1, val2, val3);
            break;
        case ORD_RBG:
            PPM_ASSIGN(pixrow[col], val1, val3, val2);
            break;
        case ORD_GRB:
            PPM_ASSIGN(pixrow[col], val2, val1, val3);
            break;
        case ORD_GBR:
            PPM_ASSIGN(pixrow[col], val3, val1, val2);
            break;
        case ORD_BRG:
            PPM_ASSIGN(pixrow[col], val2, val3, val1);
            break;
        case ORD_BGR:
            PPM_ASSIGN(pixrow[col], val3, val2, val1);
            break;
        }
    }
    dorowskip(ifP, rowskip);
}



static void
doRowRowInterleave(pixel *      const pixrow,
                   unsigned int const cols,
                   FILE *       const ifP,
                   enum Order   const order,
                   unsigned int const rowskip,
                   pixval *     const grow1,
                   pixval *     const grow2,
                   pixval *     const grow3) {

    unsigned int col;

    for (col = 0; col < cols; ++col) {
        int const val1 = getc(ifP);
        if (val1 == EOF)
            pm_error("EOF / read error");
        grow1[col] = val1;
    }
    dorowskip(ifP, rowskip);
    for (col = 0; col < cols; ++col) {
        int const val2 = getc(ifP);
        if (val2 == EOF)
            pm_error( "EOF / read error" );
        grow2[col] = val2;
    }
    dorowskip(ifP, rowskip);
    for (col = 0; col < cols; ++col) {
        int const val3 = getc(ifP);
        if (val3 == EOF)
            pm_error( "EOF / read error" );
        grow3[col] = val3;
    }
    dorowskip(ifP, rowskip);

    for (col = 0; col < cols; ++col) {
        switch (order) {
        case ORD_RGB:
            PPM_ASSIGN(pixrow[col], grow1[col], grow2[col], grow3[col]);
            break;
        case ORD_RBG:
            PPM_ASSIGN(pixrow[col], grow1[col], grow3[col], grow2[col]);
            break;
        case ORD_GRB:
            PPM_ASSIGN(pixrow[col], grow2[col], grow1[col], grow3[col]);
            break;
        case ORD_GBR:
            PPM_ASSIGN(pixrow[col], grow3[col], grow1[col], grow2[col]);
            break;
        case ORD_BRG:
            PPM_ASSIGN(pixrow[col], grow2[col], grow3[col], grow1[col]);
            break;
        case ORD_BGR:
            PPM_ASSIGN(pixrow[col], grow3[col], grow2[col], grow1[col]);
            break;
        }
    }
}



int
main(int argc, const char ** argv) {

    pixval const maxval = 255;

    struct CmdlineInfo cmdline;
    FILE * ifP;
    pixel * pixrow;
    gray * grow1;
    gray * grow2;
    gray * grow3;
    unsigned int row;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFileName);

    ppm_writeppminit(stdout, cmdline.cols, cmdline.rows, maxval, 0);

    pixrow = ppm_allocrow(cmdline.cols);

    if (cmdline.interleave == INT_ROW) {
        grow1 = pgm_allocrow(cmdline.cols);
        grow2 = pgm_allocrow(cmdline.cols);
        grow3 = pgm_allocrow(cmdline.cols);
    }

    skipHeader(ifP, cmdline.headerskip);

    for (row = 0; row < cmdline.rows; ++row) {
        switch (cmdline.interleave) {
        case INT_PIX:
            doRowPixInterleave(pixrow, cmdline.cols, ifP, cmdline.order,
                               cmdline.rowskip);
            break;

        case INT_ROW:
            doRowRowInterleave(pixrow, cmdline.cols, ifP, cmdline.order,
                               cmdline.rowskip,
                               grow1, grow2, grow3);
            break;
        }
        ppm_writeppmrow(stdout, pixrow, cmdline.cols, maxval, 0);
    }

    if (cmdline.interleave == INT_ROW) {
        pgm_freerow(grow1);
        pgm_freerow(grow2);
        pgm_freerow(grow3);
    }
    ppm_freerow(pixrow);

    pm_close(ifP);
    pm_close(stdout);

    return 0;
}



