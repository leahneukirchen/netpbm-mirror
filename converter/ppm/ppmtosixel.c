/* ppmtosix.c - read a PPM and produce a color sixel file
**
** Copyright (C) 1991 by Rick Vinci.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
**
** "-7bit" option added August 2023 by Scott Pakin <scott+pbm@pakin.org>.
*/

#include "pm_c_util.h"
#include "mallocvar.h"
#include "shhopt.h"
#include "ppm.h"

static unsigned int const SIXEL_MAXVAL = 100;
static unsigned int const MAXCOLORCT = 256;

static const char * const sixel = "@ACGO_";

/* Named escape sequences */
typedef struct {
  const char * DCS;   /* Device Control String */
  const char * ST;    /* String Terminator */
  const char * CSI;   /* Control String Introducer */
  const char * ESC;   /* Escape character */
} EscapeSequenceSet;



enum Charwidth {CHARWIDTH_8BIT, CHARWIDTH_7BIT};

struct CmdlineInfo {
    /* All the information the user supplied in the command line, in a form
       easy for the program to use.
    */
    const char *   inputFileName;
    unsigned int   raw;
    unsigned int   margin;
    enum Charwidth charWidth;
};



static EscapeSequenceSet
escapeSequenceSet(enum Charwidth const charWidth) {

    EscapeSequenceSet eseqs;

    switch (charWidth) {
    case CHARWIDTH_8BIT: {
        eseqs.DCS = "\220";
        eseqs.ST  = "\234";
        eseqs.CSI = "\233";
        eseqs.ESC = "\033";
    } break;
    case CHARWIDTH_7BIT: {
        eseqs.DCS = "\033P";
        eseqs.ST  = "\033\\";
        eseqs.CSI = "\033[";
        eseqs.ESC = "\033";
    } break;
    }
    return eseqs;
}



static void
parseCommandLine(int argc, const char ** argv,
                 struct CmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   Parse the program arguments (given by argc and argv) into a form
   the program can deal with more easily -- a cmdline_info structure.
   If the syntax is invalid, issue a message and exit the program via
   pm_error().

   Note that the file spec array we return is stored in the storage that
   was passed to us as the argv array.
-----------------------------------------------------------------------------*/
    optEntry * option_def;  /* malloc'ed */
    optStruct3 opt;  /* set by OPTENT3 */
    unsigned int option_def_index;

    unsigned int opt7Bit;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0,   "raw",      OPT_FLAG,
            NULL,                       &cmdlineP->raw, 0);
    OPTENT3(0,   "margin",   OPT_FLAG,
            NULL,                       &cmdlineP->margin, 0);
    OPTENT3(0,   "7bit",     OPT_FLAG,
            NULL,                       &opt7Bit, 0);

    opt.opt_table = option_def;
    opt.short_allowed = false; /* We have no short (old-fashioned) options */
    opt.allowNegNum = false;   /* We have no parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    cmdlineP->charWidth = opt7Bit ? CHARWIDTH_7BIT : CHARWIDTH_8BIT;

    if (argc-1 == 0)
        cmdlineP->inputFileName = "-";
    else if (argc-1 != 1)
        pm_error("Program takes zero or one argument (filename).  You "
                 "specified %d", argc-1);
    else
        cmdlineP->inputFileName = argv[1];

    free(option_def);
}



static void
writePackedImage(pixel **        const pixels,
                 unsigned int    const rows,
                 unsigned int    const cols,
                 colorhash_table const cht) {

    unsigned int row;

    for (row = 0; row < rows; ++row) {

        unsigned int const b = row % 6;

        unsigned int repeatCt;
        unsigned int col;

        repeatCt = 1;

        for (col = 0; col < cols; ++col) {
            const pixel * const thisRow = pixels[row];

            unsigned int const thisColorIdx =
                ppm_lookupcolor(cht, &thisRow[col]);

            if (col == cols - 1)   /* last pixel in row */
                if (repeatCt == 1)
                    printf("#%d%c", thisColorIdx, sixel[b]);
                else
                    printf("#%d!%d%c", thisColorIdx, repeatCt, sixel[b]);
            else {  /* not last pixel in row */
                unsigned int const nextColorIdx =
                    ppm_lookupcolor(cht, &thisRow[col+1]);
                if (thisColorIdx == nextColorIdx)
                    ++repeatCt;
                else {
                    if (repeatCt == 1)
                        printf("#%d%c", thisColorIdx, sixel[b]);
                    else {
                        printf("#%d!%d%c", thisColorIdx, repeatCt, sixel[b]);
                        repeatCt = 1;
                    }
                }
            }
        }
        printf( "$\n" );   /* Carriage Return */

        if (b == 5)
            printf("-\n");   /* Line Feed (one sixel height) */
    }
}



static void
writeHeader(bool              const wantMargin,
            EscapeSequenceSet const eseqs) {

    if (wantMargin)
        printf("%s%d;%ds", eseqs.CSI, 14, 72);

    printf("%s", eseqs.DCS);  /* start with Device Control String */

    printf("0;0;8q");   /* Horizontal Grid Size at 1/90" and graphics On */

    printf("\"1;1\n");  /* set aspect ratio 1:1 */
}



static void
writeColorMap(colorhist_vector const chv,
              unsigned int     const colorCt,
              pixval           const maxval) {

    unsigned int colorIdx;

    for (colorIdx = 0; colorIdx < colorCt ; ++colorIdx) {
        pixel const p = chv[colorIdx].color;

        pixel scaledColor;

        if (maxval == SIXEL_MAXVAL)
            scaledColor = p;
        else
            PPM_DEPTH(scaledColor, p, maxval, SIXEL_MAXVAL);

        printf("#%d;2;%d;%d;%d", colorIdx,
               PPM_GETR(scaledColor),
               PPM_GETG(scaledColor),
               PPM_GETB(scaledColor));
    }
    printf("\n");
}



static void
writeRawImage(pixel **        const pixels,
              unsigned int    const rows,
              unsigned int    const cols,
              colorhash_table const cht) {

    unsigned int row;

    for (row = 0; row < rows; ++row) {
        pixel * const thisRow = pixels[row];
        unsigned int const b = row % 6;

        unsigned int col;

        for (col = 0; col < cols; ++col)
            printf("#%d%c", ppm_lookupcolor(cht, &thisRow[col]), sixel[b]);

        printf("$\n");   /* Carriage Return */

        if (b == 5)
            printf("-\n");   /* Line Feed (one sixel height) */
    }
}



static void
writeEnd(bool              const wantMargin,
         EscapeSequenceSet const eseqs) {

    if (wantMargin)
        printf ("%s%d;%ds", eseqs.CSI, 1, 80);

    printf("%s\n", eseqs.ST);
}



int
main(int argc, const char ** argv) {

    struct CmdlineInfo cmdline;
    FILE * ifP;
    int rows, cols;
    pixval maxval;
    int colorCt;
    pixel ** pixels;
    colorhist_vector chv;
        /* List of colors in the image, indexed by colormap index */
    colorhash_table cht;
        /* Hash table for fast colormap index lookup */
    EscapeSequenceSet eseqs;
        /* The escape sequences we use in our output for various functions */

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFileName);

    pixels = ppm_readppm(ifP, &cols, &rows, &maxval);

    if (maxval > SIXEL_MAXVAL) {
        pm_message(
            "maxval of input is not the sixel maxval (%u) - "
            "rescaling to fewer colors", SIXEL_MAXVAL);
    }

    pm_message("computing colormap...");
    chv = ppm_computecolorhist(pixels, cols, rows, MAXCOLORCT, &colorCt);
    if (!chv)
        pm_error("too many colors - try 'pnmquant %u'", MAXCOLORCT);

    pm_message("%d colors found", colorCt);

    cht = ppm_colorhisttocolorhash(chv, colorCt);

    eseqs = escapeSequenceSet(cmdline.charWidth);

    writeHeader(!!cmdline.margin, eseqs);

    writeColorMap(chv, colorCt, maxval);

    if (cmdline.raw)
        writeRawImage(pixels, rows, cols, cht);
    else
        writePackedImage(pixels, rows, cols, cht);

    writeEnd(!!cmdline.margin, eseqs);

    /* If the program failed, it previously aborted with nonzero completion
       code, via various function calls.
    */
    return 0;
}



