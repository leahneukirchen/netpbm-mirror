/* pgmoil.c - read a PPM image and turn into an oil painting
**
** Copyright (C) 1990 by Wilson Bent (whb@hoh-2.att.com)
** Shamelessly butchered into a color version by Chris Sheppard
** 2001
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "mallocvar.h"
#include "shhopt.h"
#include "pam.h"



struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFileNm;
    unsigned int n;
};



static void
parseCommandLine(int argc, const char ** argv,
                 struct CmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   Note that the file spec array we return is stored in the storage that
   was passed to us as the argv array.
-----------------------------------------------------------------------------*/
    optStruct3 opt;  /* set by OPTENT3 */
    optEntry * option_def;
    unsigned int option_def_index;

    unsigned int nSpec;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0,   "n", OPT_UINT, &cmdlineP->n, &nSpec, 0);

    opt.opt_table = option_def;
    opt.short_allowed = false;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = false;  /* We have no parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    free(option_def);

    if (!nSpec)
        cmdlineP->n = 3;

    if (argc-1 < 1)
        cmdlineP->inputFileNm = "-";
    else if (argc-1 == 1)
        cmdlineP->inputFileNm = argv[1];
    else
        pm_error("You specified too many arguments (%u).  The only "
                 "possible argument is the optional input file specification.",
                 argc-1);
}



static void
computeRowHist(struct pam   const inpam,
               tuple **     const tuples,
               unsigned int const smearFactor,
               unsigned int const plane,
               unsigned int const row,
               unsigned int const col,
               sample *     const hist) {
/*----------------------------------------------------------------------------
  Compute hist[] - frequencies, in the neighborhood of row 'row', column
  'col', in plane 'plane', of each sample value
-----------------------------------------------------------------------------*/
    sample i;
    int drow;

    for (i = 0; i <= inpam.maxval; ++i)
        hist[i] = 0;

    for (drow = row - smearFactor; drow <= row + smearFactor; ++drow) {
        if (drow >= 0 && drow < inpam.height) {
            int dcol;

            for (dcol = col - smearFactor;
                 dcol <= col + smearFactor;
                 ++dcol) {
                if (dcol >= 0 && dcol < inpam.width)
                    ++hist[tuples[drow][dcol][plane]];
            }
        }
    }
}



static sample
modalValue(sample * const hist,
           sample   const maxval) {
/*----------------------------------------------------------------------------
  The sample value that occurs most often according to histogram hist[].
-----------------------------------------------------------------------------*/
    sample modalval;
    unsigned int maxfreq;
    sample sampleval;

    for (sampleval = 0, maxfreq = 0, modalval = 0;
         sampleval <= maxval;
         ++sampleval) {

        if (hist[sampleval] > maxfreq) {
            maxfreq = hist[sampleval];
            modalval = sampleval;
        }
    }
    return modalval;
}



static void
convertRow(struct pam     const inpam,
           tuple **       const tuples,
           tuple *        const tuplerow,
           unsigned int   const row,
           unsigned int   const smearFactor,
           sample *       const hist) {
/*----------------------------------------------------------------------------
   'hist' is a working buffer inpam.width wide.
-----------------------------------------------------------------------------*/
    unsigned int plane;

    for (plane = 0; plane < inpam.depth; plane++) {
        unsigned int col;

        for (col = 0; col < inpam.width; ++col)  {
            sample modalval;
                /* The sample value that occurs most often in the neighborhood
                   of column 'col' of row 'row', in plane 'plane'.
                */

            computeRowHist(inpam, tuples, smearFactor, plane, row, col, hist);

            modalval = modalValue(hist, inpam.maxval);

            tuplerow[col][plane] = modalval;
        }
    }
}



int
main(int argc, const char ** argv) {

    struct pam inpam, outpam;
    FILE * ifP;
    tuple ** tuples;  /* malloc'ed */
    tuple * tuplerow;  /* malloc'ed */
    sample * hist;  /* malloc'ed */
        /* A buffer for the convertRow subroutine to use */
    int row;
    struct CmdlineInfo cmdline;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFileNm);

    tuples = pnm_readpam(ifP, &inpam, PAM_STRUCT_SIZE(tuple_type));

    MALLOCARRAY(hist, inpam.maxval + 1);
    if (hist == NULL)
        pm_error("Unable to allocate memory for histogram.");

    outpam = inpam; outpam.file = stdout;

    pnm_writepaminit(&outpam);

    tuplerow = pnm_allocpamrow(&inpam);

    for (row = 0; row < inpam.height; ++row) {
        convertRow(inpam, tuples, tuplerow, row, cmdline.n, hist);
        pnm_writepamrow(&outpam, tuplerow);
    }

    pnm_freepamrow(tuplerow);
    free(hist);
    pnm_freepamarray(tuples, &inpam);

    pm_close(ifP);
    pm_close(stdout);
    return 0;
}


