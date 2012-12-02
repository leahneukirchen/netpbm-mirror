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
#include <limits.h>

#include "pm_c_util.h"
#include "mallocvar.h"
#include "shhopt.h"
#include "pgm.h"



struct cmdline_info {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFileName;  /* Filename of input files */
    unsigned int machine;
    unsigned int median;
    unsigned int quartile;
    unsigned int decile;
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
    
    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */

    OPTENT3(0,   "machine",       OPT_FLAG,  NULL,
            &cmdlineP->machine,             0);
    OPTENT3(0,   "median",        OPT_FLAG,  NULL,
            &cmdlineP->median,              0);
    OPTENT3(0,   "quartile",      OPT_FLAG,  NULL,
            &cmdlineP->quartile,            0);
    OPTENT3(0,   "decile",        OPT_FLAG,  NULL,
            &cmdlineP->decile,              0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We have no parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (cmdlineP->median + cmdlineP->quartile + cmdlineP->decile > 1)
        pm_error("You may specify only one of -median, -quartile, "
                 "and -decile");

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



static unsigned int
sum(unsigned int const hist[],
    gray         const maxval) {

    unsigned int sum;
    unsigned int sampleVal;

    for (sampleVal = 0, sum = 0; sampleVal <= maxval; ++sampleVal)
        sum += hist[sampleVal];

    return sum;
}



static void
findQuantiles(unsigned int const n,
              unsigned int const hist[],
              gray         const maxval,
              gray *       const quantile) {
/*----------------------------------------------------------------------------
   Find the order-n quantiles (e.g. n == 4 means quartiles) of the pixel
   sample values, given that hist[] is the histogram of them (hist[N] is the
   number of pixels that have sample value N).

   'maxval' is the maxval of the image, so the size of hist[] is 'maxval' + 1.

   We return the ith quantile as quantile[i].  For example, for quartiles,
   quantile[3] is the least sample value for which at least 3/4 of the pixels
   are less than or equal to it.  

   quantile[] must be allocated at least to size 'n'.

   We return 
-----------------------------------------------------------------------------*/
    unsigned int const totalCt = sum(hist, maxval);

    unsigned int quantSeq;
        /* 0 is first quantile, 1 is second quantile, etc. */

    gray sampleVal;
        /* As we increment through all the possible sample values, this
           is the one we're considering now.
        */
    unsigned int cumCt;
        /* The number of pixels that have sample value 'sampleVal' or less. */

    assert(n > 1);

    sampleVal = 0;    /* initial value */
    cumCt = hist[0];  /* initial value */

    for (quantSeq = 1; quantSeq <= n; ++quantSeq) {
        double const quantCt = (double)quantSeq/n * totalCt;
            /* This is how many pixels are (ignoring quantization) in the
               quantile.  E.g. for the 3rd quartile, it is 3/4 of the pixels
               in the image.
            */

        assert(quantCt <= totalCt);

        /* at sampleVal == maxval, cumCt == totalCt, so because
           quantCt <= 'totalCt', 'sampleVal' cannot go above maxval.
        */

        while (cumCt < quantCt) {
            ++sampleVal;
            cumCt += hist[sampleVal];
        }

        assert(sampleVal <= maxval);

        /* 'sampleVal' is the lowest sample value for which at least 'quantCt'
           pixels have that sample value or less.  'cumCt' is the number
           of pixels that have sample value 'sampleVal' or less.
        */
        quantile[quantSeq-1] = sampleVal;
    }
}



static void
countCumulative(unsigned int    const hist[],
                gray            const maxval,
                unsigned int ** const rcountP) {
/*----------------------------------------------------------------------------
   From the histogram hist[] (hist[N] is the number of pixels of sample
   value N), compute the cumulative distribution *rcountP ((*rcountP)[N]
   is the number of pixels of sample value N or higher).

   *rcountP is newly malloced memory.
-----------------------------------------------------------------------------*/
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
reportHistHumanFriendly(unsigned int const hist[],
                        unsigned int const rcount[],
                        gray         const maxval) {

    unsigned int const totalPixels = rcount[0];

    unsigned int cumCount;
    unsigned int i;

    printf("value  count  b%%     w%%   \n");
    printf("-----  -----  ------  ------\n");

    for (i = 0, cumCount = 0; i <= maxval; ++i) {
        if (hist[i] > 0) {
            cumCount += hist[i];
            printf(
                "%5d  %5d  %5.3g%%  %5.3g%%\n", i, hist[i],
                (float) cumCount * 100.0 / totalPixels, 
                (float) rcount[i] * 100.0 / totalPixels);
        }
    }
}



static void
reportHistMachineFriendly(unsigned int const hist[],
                          gray         const maxval) {

    unsigned int i;

    for (i = 0; i <= maxval; ++i) {
        printf("%u %u\n", i, hist[i]);
    }
}



static void
reportQuantilesMachineFriendly(gray         const quantile[],
                               unsigned int const n) {

    unsigned int i;

    for (i = 0; i < n; ++i)
        printf("%u\n", quantile[i]);
}



static void
reportMedianHumanFriendly(gray const median) {

    printf("Median: %5u\n", median);
}



static void
reportQuartilesHumanFriendly(gray const quartile[]) {

    unsigned int i;

    printf("Quartiles:\n");

    printf("Q    Value\n");
    printf("---- -----\n");

    for (i = 1; i <= 4; ++i)
        printf("%3u%% %5u\n", 25*i, quartile[i-1]);
}



static void
reportDecilesHumanFriendly(gray const decile[]) {

    unsigned int i;

    printf("Deciles:\n");

    printf("Q    Value\n");
    printf("---  -----\n");

    for (i = 1; i <= 10; ++i)
        printf("%3u%% %5u\n", 10*i, decile[i-1]);
}



int
main(int argc, const char ** argv) {

    struct cmdline_info cmdline;
    FILE * ifP;
    gray maxval;
    unsigned int * hist;   /* malloc'ed array */

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFileName);

    buildHistogram(ifP, &hist, &maxval);

    if (cmdline.median) {
        gray median;
        findQuantiles(2, hist, maxval, &median); 
        if (cmdline.machine)
            reportQuantilesMachineFriendly(&median, 1);
        else
            reportMedianHumanFriendly(median);
    } else if (cmdline.quartile) {
        gray quartile[4];
        findQuantiles(4, hist, maxval, quartile);
        if (cmdline.machine)
            reportQuantilesMachineFriendly(quartile, 4);
        else
            reportQuartilesHumanFriendly(quartile);
    } else if (cmdline.decile) {
        gray decile[10];
        findQuantiles(10, hist, maxval, decile);
        if (cmdline.machine)
            reportQuantilesMachineFriendly(decile, 10);
        else
            reportDecilesHumanFriendly(decile);
    } else {
        if (cmdline.machine)
            reportHistMachineFriendly(hist, maxval);
        else {
            unsigned int * rcount; /* malloc'ed array */
            countCumulative(hist, maxval, &rcount);
            reportHistHumanFriendly(hist, rcount, maxval);

            free(rcount);
        }
    }

    free(hist);
    pm_close(ifP);

    return 0;
}



