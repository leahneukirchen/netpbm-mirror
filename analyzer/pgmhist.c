/* pgmhist.c - print a histogram of the values in a PGM image
**
** Copyright (C) 1989 by Jef Poskanzer.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

#include <assert.h>

#include "mallocvar.h"
#include "shhopt.h"
#include "pgm.h"



struct cmdline_info {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFileName;  /* Filename of input files */
};



static void
parseCommandLine(int argc, const char ** argv,
                 struct cmdline_info * const cmdlineP) {
/*----------------------------------------------------------------------------
   Note that the file spec array we return is stored in the storage that
   was passed to us as the argv array.
-----------------------------------------------------------------------------*/
    optStruct3 opt;  /* set by OPTENT3 */
    optEntry * option_def;
    unsigned int option_def_index;
    
    MALLOCARRAY(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENTRY */

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We have no parms that are negative numbers */

    optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (argc-1 == 0) 
        cmdlineP->inputFileName = "-";
    else if (argc-1 != 1)
        pm_error("Program takes zero or one argument (filename).  You "
                 "specified %d", argc-1);
    else
        cmdlineP->inputFileName = argv[1];
}



static void
buildHistogram(FILE *          const ifP,
               unsigned int ** const histP,
               gray *          const maxvalP) {

    gray * grayrow;
    int rows, cols;
    int format;
    unsigned int row;
    unsigned int i;
    unsigned int * hist;  /* malloc'ed array */
    gray maxval;

    pgm_readpgminit(ifP, &cols, &rows, &maxval, &format);

    if (UINT_MAX / cols < rows)
        pm_error("Too many pixels (%u x %u) in image.  "
                 "Maximum computable is %u",
                 cols, rows, UINT_MAX);

    grayrow = pgm_allocrow(cols);

    MALLOCARRAY(hist, maxval + 1);
    if (hist == NULL)
        pm_error("out of memory");

    for (i = 0; i <= maxval; ++i)
        hist[i] = 0;

    for (row = 0; row < rows; ++row) {
        unsigned int col;

        pgm_readpgmrow(ifP, grayrow, cols, maxval, format);

        for (col = 0; col < cols; ++col) {
            /* Because total pixels in image is limited: */
            assert (hist[grayrow[col]] < INT_MAX);

            ++hist[grayrow[col]];
        }
    }
    pgm_freerow(grayrow);

    *histP   = hist;
    *maxvalP = maxval;
}



static void
countCumulative(unsigned int    const hist[],
                gray            const maxval,
                unsigned int ** const rcountP) {

    unsigned int * rcount;
    unsigned int cumCount;
    int i;
    
    MALLOCARRAY(rcount, maxval + 1);
    if (rcount == NULL)
        pm_error("out of memory");

    for (i = maxval, cumCount = 0; i >= 0; --i) {
        /* Because total pixels in image is limited: */
        assert(UINT_MAX - hist[i] >= cumCount);

        cumCount += hist[i];
        rcount[i] = cumCount;
    }

    *rcountP = rcount;
}



static void
report(unsigned int const hist[],
       unsigned int const rcount[],
       gray         const maxval) {

    unsigned int const totalPixels = rcount[0];
    unsigned int count;
    unsigned int i;

    printf("value  count  b%%      w%%   \n");
    printf("-----  -----  ------  ------\n");

    count = 0;

    for (i = 0; i <= maxval; ++i) {
        if (hist[i] > 0) {
            count += hist[i];
            printf(
                "%5d  %5d  %5.3g%%  %5.3g%%\n", i, hist[i],
                (float) count * 100.0 / totalPixels, 
                (float) rcount[i] * 100.0 / totalPixels);
        }
    }
}



int
main(int argc, const char ** argv) {

    struct cmdline_info cmdline;
    FILE * ifP;
    gray maxval;
    unsigned int * rcount; /* malloc'ed array */
    unsigned int * hist;   /* malloc'ed array */

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFileName);

    buildHistogram(ifP, &hist, &maxval);

    countCumulative(hist, maxval, &rcount);

    report(hist, rcount, maxval);

    free(rcount);
    free(hist);
    pm_close(ifP);

    return 0;
}



