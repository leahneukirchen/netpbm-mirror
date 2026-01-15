/* asciitopgm.c - read an ASCII graphics file and produce a PGM
**
** Copyright (C) 1989 by Wilson H. Bent, Jr
**
** - Based on fstopgm.c and other works which bear the following notice:
** Copyright (C) 1989 by Jef Poskanzer.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

#include <string.h>

#include "pm_c_util.h"
#include "mallocvar.h"
#include "nstring.h"
#include "shhopt.h"
#include "pgm.h"



struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFileNm;  /* File name of input file */
    unsigned int divisor;
    unsigned int rows;    /* height of output image in pixels */
    unsigned int cols;    /* width of output image in pixels */
};



static void
parseSizeParm(const char *   const sizeString,
              const char *   const description,
              unsigned int * const sizeP) {

    char * endptr;
    long int sizeLong;

    sizeLong = strtol(sizeString, &endptr, 10);
    if (strlen(sizeString) > 0 && *endptr != '\0')
        pm_error("%s argument not an integer: '%s'",
                 description, sizeString);
    else if (sizeLong > INT_MAX - 2)
        pm_error("%s argument is too large "
                 "for computations: %ld",
                 description, sizeLong);
    else if (sizeLong <= 0)
        pm_error("%s argument is not positive: %ld",
                 description, sizeLong);
    else
        *sizeP = (unsigned int) sizeLong;
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
    unsigned int divisorSpec;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENTRY */
    OPTENT3(0, "divisor",    OPT_UINT,   &cmdlineP->divisor,  &divisorSpec, 0);

    opt.opt_table = option_def;
    opt.short_allowed = false;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = false;   /* We have no parms that are negative numbers */

    pm_optParseOptions4(&argc, argv, opt, sizeof(opt), 0);
    /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (!divisorSpec)
        cmdlineP->divisor = 1;
    else {
        if (cmdlineP->divisor == 0)
            pm_error("Divisor cannot be zero");
    }

    if (argc-1 < 2)
        pm_error("Not enough arguments.  Need at least height and width "
                 "of output image, in pixels");
    else {
        parseSizeParm(argv[1], "height", &cmdlineP->rows);
        parseSizeParm(argv[2], "width", &cmdlineP->cols);

        if (argc-1 < 3)
            cmdlineP->inputFileNm = "-";
        else {
            cmdlineP->inputFileNm = argv[3];

            if (argc-1 > 3) {
                pm_error("Too many arguments: %u.  Only possible "
                         "non-option arguments are "
                         "height, width, and input file name", argc-1);
            }
        }
    }

    free(option_def);
}



static unsigned int const gmap [128] = {
/*00 nul-bel*/  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/*08 bs -si */  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/*10 dle-etb*/  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/*18 can-us */  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/*20 sp - ' */  0x00, 0x21, 0x1b, 0x7f, 0x70, 0x25, 0x20, 0x0a,
/*28  ( - / */  0x11, 0x11, 0x2a, 0x2b, 0x0b, 0x13, 0x04, 0x10,
/*30  0 - 7 */  0x30, 0x28, 0x32, 0x68, 0x39, 0x35, 0x39, 0x16,
/*38  8 - ? */  0x38, 0x39, 0x14, 0x15, 0x11, 0x1c, 0x11, 0x3f,
/*40  @ - G */  0x40, 0x49, 0x52, 0x18, 0x44, 0x3c, 0x38, 0x38,
/*48  H - O */  0x55, 0x28, 0x2a, 0x70, 0x16, 0x7f, 0x70, 0x14,
/*50  P - W */  0x60, 0x20, 0x62, 0x53, 0x1a, 0x55, 0x36, 0x57,
/*58  X - _ */  0x50, 0x4c, 0x5a, 0x24, 0x10, 0x24, 0x5e, 0x13,
/*60  ` - g */  0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
/*68  h - o */  0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x2a,
/*70  p - w */  0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
/*78  x -del*/  0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f,
};

static gray const maxval = 127;



static void
zeroObuf(unsigned int * const obuf,
         unsigned int   const cols) {

    unsigned int col;
    for (col = 0; col < cols; ++col)
        obuf[col] = 0;
}



static void
convertRowToPgm(const unsigned int * const obuf,
                unsigned int         const cols,
                gray                 const maxval,
                gray *               const grayrow) {
/*----------------------------------------------------------------------------
   Convert the row in obuf[], which is 'cols' columns wide, to PGM
   in grayrow[] with maxval 'maxval'.

   The values in 'obuf' are _darkness_ values; 0 is white; 'maxval' is black.
-----------------------------------------------------------------------------*/
    unsigned int col;

    for (col = 0; col < cols; ++col)
        grayrow[col] = maxval - MIN(maxval, obuf[col]);
}



static bool warnedNonAscii;

static unsigned int
darknessOfChar(char const c) {

    unsigned char asciifiedC;

    if (c & 0x80) {       /* !isascii(c) */
        if (!warnedNonAscii) {
            pm_message("Warning: non-ASCII char(s) in input");
            warnedNonAscii = true;
        }
        asciifiedC = c & 0x7f;      /* toascii(c) */
    } else
        asciifiedC = c;

    return gmap[asciifiedC];
}



static void
convertAsciiToPgm(FILE *         const ifP,
                  unsigned int   const cols,
                  unsigned int   const rows,
                  unsigned int   const divisor,
                  gray           const maxval,
                  gray **        const grays) {

    unsigned int * obuf;
        /* The current row, in darkness values (0 is white; 127 is maximum
           black)
        */
    unsigned int outRow;
    unsigned int outCursor;
    bool beginningOfImage;
    bool beginningOfLine;
    bool warnedTrunc;
    bool eof;

    MALLOCARRAY(obuf, cols);
    if (obuf == NULL)
        pm_error("Unable to allocate memory for %u columns", cols);

    zeroObuf(obuf, cols);

    warnedNonAscii = false;
    warnedTrunc = false;
    outCursor = 0;
    beginningOfImage = true;
    beginningOfLine = true;
    for (eof = false, outRow = 0; outRow < rows && !eof; ) {
        int c;

        c = getc(ifP);

        if (c == EOF)
            eof = true;
        else {
            if (beginningOfLine) {
                if (c == '+') {
                    /* + at start of line means rest of line
                       overstrikes previous
                    */
                    c = getc(ifP);
                    if (c == EOF)
                        eof = true;
                } else {
                    if (!beginningOfImage) {
                        /* Output previous line, move to next */

                        convertRowToPgm(obuf, cols, maxval, grays[outRow]);

                        zeroObuf(obuf, cols);

                        ++outRow;
                    }
                }
                outCursor = 0;
                beginningOfLine = false;
            }
            if (!eof) {
                if (c == '\n')
                    beginningOfLine = true;
                else {
                    if (outRow < rows && outCursor < cols)
                        obuf[outCursor++] += darknessOfChar(c) / divisor;
                    else {
                        if (!warnedTrunc) {
                            pm_message("Warning: "
                                       "truncating image to %u columns "
                                       "x %u rows", cols, rows);
                            warnedTrunc = true;
                        }
                    }
                }
            }
        }
        beginningOfImage = false;
    }
    while (outRow < rows) {
        /* Output previous line, move to next */

        convertRowToPgm(obuf, cols, maxval, grays[outRow]);

        zeroObuf(obuf, cols);

        ++outRow;
    }
    free(obuf);
}



int
main(int argc, const char ** argv) {

    struct CmdlineInfo cmdline;
    FILE * ifP;
    gray ** grays;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFileNm);

    grays = pgm_allocarray(cmdline.cols, cmdline.rows);

    convertAsciiToPgm(ifP, cmdline.cols, cmdline.rows, cmdline.divisor,
                      maxval, grays);

    pm_close(ifP);

    pgm_writepgm(stdout, grays, cmdline.cols, cmdline.rows, maxval, 0);

    pgm_freearray(grays, cmdline.rows);

    return 0;
}


