/*
 * Convert a SRF (Garmin vehicle) to a PAM image
 *
 * Copyright (C) 2011 by Mike Frysinger <vapier@gentoo.org>
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose and without fee is hereby granted, provided
 * that the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation.  This software is provided "as is" without express or
 * implied warranty.
 */

#include <assert.h>
#include <stdio.h>

#include "pm_c_util.h"
#include "shhopt.h"
#include "mallocvar.h"
#include "nstring.h"
#include "pam.h"
#include "srf.h"

struct cmdlineInfo {
  /* All the information the user supplied in the command line,
     in a form easy for the program to use.
  */
  const char *  inputFileName;  /* '-' if stdin */
  unsigned int  verbose;
};

static bool verbose;



static void
parseCommandLine(int argc, const char ** argv,
                 struct cmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   parse program command line described in Unix standard form by argc
   and argv.  Return the information in the options as *cmdlineP.

   If command line is internally inconsistent (invalid options, etc.),
   issue error message to stderr and abort program.

   Note that the strings we return are stored in the storage that
   was passed to us as the argv array.  We also trash *argv.
-----------------------------------------------------------------------------*/
    optEntry *option_def;
    /* Instructions to pm_optParseOptions3 on how to parse our options.
     */
    optStruct3 opt;

    unsigned int option_def_index;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0, "verbose",          OPT_FLAG,      NULL,
            &cmdlineP->verbose,    0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We have no parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
    /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (argc-1 < 1)
        cmdlineP->inputFileName = "-";
    else if (argc-1 == 1)
        cmdlineP->inputFileName = argv[1];
    else
        pm_error("Program takes at most one argument:  input file name");
}



static unsigned int
srfRed(uint16_t const pixel) {
    return ((pixel >> 11) & 0x1f) << 3;
}


static unsigned int
srfGrn(uint16_t const pixel) {
    return ((pixel >>  6) & 0x1f) << 3;
}


static unsigned int
srfBlu(uint16_t const pixel) {
    return ((pixel >>  0) & 0x1f) << 3;
}


static uint8_t
srfAlpha(uint8_t const d) {

    uint8_t retval;

    if (d == SRF_ALPHA_OPAQUE)
        retval = 0xff;
    else
        retval = (128 - d) << 1;

    return retval;
}



static void
writeRaster(struct pam *     const pamP,
            struct srf_img * const imgP) {

    tuple *  tuplerow;
    unsigned int row;

    assert(imgP->header.width <= pamP->width);

    tuplerow = pnm_allocpamrow(pamP);

    for (row = 0; row < imgP->header.height; ++row) {
        unsigned int const rowStart = row * imgP->header.width;

        unsigned int col;

        for (col = 0; col < imgP->header.width; ++col) {
            uint16_t const data  = imgP->data.data[rowStart + col];
            uint16_t const alpha = imgP->alpha.data[rowStart + col];

            assert(col < pamP->width);
            
            tuplerow[col][PAM_RED_PLANE] = srfRed(data);
            tuplerow[col][PAM_GRN_PLANE] = srfGrn(data);
            tuplerow[col][PAM_BLU_PLANE] = srfBlu(data);
            tuplerow[col][PAM_TRN_PLANE] = srfAlpha(alpha);
        }

        for (; col < pamP->width; ++col) {
            tuplerow[col][PAM_RED_PLANE] = 0;
            tuplerow[col][PAM_GRN_PLANE] = 0;
            tuplerow[col][PAM_BLU_PLANE] = 0;
            tuplerow[col][PAM_TRN_PLANE] = 0;
        }            

        pnm_writepamrow(pamP, tuplerow);
    }
    pnm_freepamrow(tuplerow);
}



static void
convertOneImage(struct srf_img * const imgP,
                FILE *           const ofP) {

    const char * comment = "Produced by srftopam";  /* constant */

    struct pam   outPam;

    outPam.size             = sizeof(struct pam);
    outPam.len              = PAM_STRUCT_SIZE(comment_p);
    outPam.file             = ofP;
    outPam.format           = PAM_FORMAT;
    outPam.plainformat      = 0;
    outPam.width            = imgP->header.width;
    outPam.height           = imgP->header.height;
    outPam.depth            = 4;
    outPam.maxval           = 255;
    outPam.bytes_per_sample = 1;
    sprintf(outPam.tuple_type, "RGB_ALPHA");
    outPam.allocation_depth = 4;
    outPam.comment_p        = &comment;

    pnm_writepaminit(&outPam);

    writeRaster(&outPam, imgP);
}



static void
srftopam(struct cmdlineInfo const cmdline,
         FILE *             const ifP,
         FILE *             const ofP) {

    unsigned int imgSeq;
    struct srf   srf;

    srf_read(ifP, verbose, &srf);

    for (imgSeq = 0; imgSeq < srf.header.img_cnt; ++imgSeq) {
        if (verbose)
            pm_message("Converting Image %u", imgSeq);
        convertOneImage(&srf.imgs[imgSeq], ofP);
    }

    srf_term(&srf);
}



int
main(int argc, const char * argv[]) {

    struct cmdlineInfo cmdline;
    FILE *             ifP;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    verbose = cmdline.verbose;

    ifP = pm_openr(cmdline.inputFileName);

    srftopam(cmdline, ifP, stdout);

    pm_closer(ifP);

    return 0;
}



