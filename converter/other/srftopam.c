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
producePam(struct pam *     const pamP,
           uint16_t         const lineLen,
           struct srf_img * const imgP) {

    tuple *  tuplerow;
    uint16_t r;

    tuplerow = pnm_allocpamrow(pamP);

    for (r = 0; r < imgP->header.height; ++r) {
        unsigned int const off = r * imgP->header.width;

        unsigned int col;

        for (col = 0; col < imgP->header.width; ++col) {
            uint16_t const data  = imgP->data.data[off + col];
            uint16_t const alpha = imgP->alpha.data[off + col];

            
            tuplerow[col][PAM_RED_PLANE] = srfRed(data);
            tuplerow[col][PAM_GRN_PLANE] = srfGrn(data);
            tuplerow[col][PAM_BLU_PLANE] = srfBlu(data);
            tuplerow[col][PAM_TRN_PLANE] = srfAlpha(alpha);
        }

        for (; col < lineLen; ++col) {
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
convertsrf(struct cmdlineInfo const cmdline,
           FILE *             const ifP,
           FILE *             const ofP) {
/*----------------------------------------------------------------------------
  Design note:  It's is really a modularity violation that we have
  all the command line parameters as an argument.  We do it because we're
  lazy -- it takes a great deal of work to carry all that information as
  separate arguments -- and it's only a very small violation.
  -----------------------------------------------------------------------------*/
    const char * comment = "Produced by srftopam";  /* constant */
    long         width, height;
    long         fwidth;
    unsigned int i;
    struct srf   srf;
    struct pam   outPam;

    srf_read(ifP, verbose, &srf);

    width = height = 0;  /* initial value */
    for (i = 0; i < srf.header.img_cnt; ++i) {
        if (width < srf.imgs[i].header.width) {
            width  = srf.imgs[i].header.width;
            fwidth = srf.imgs[i].header.height;
        }
        height += srf.imgs[i].header.height;
    }

    outPam.size             = sizeof(struct pam);
    outPam.len              = PAM_STRUCT_SIZE(comment_p);
    outPam.file             = ofP;
    outPam.format           = PAM_FORMAT;
    outPam.plainformat      = 0;
    outPam.width            = width;
    outPam.height           = height;
    outPam.depth            = 4;
    outPam.maxval           = 255;
    outPam.bytes_per_sample = 1;
    sprintf(outPam.tuple_type, "RGB_ALPHA");
    outPam.allocation_depth = 4;
    outPam.comment_p        = &comment;

    pnm_writepaminit(&outPam);

    for (i = 0; i < srf.header.img_cnt; ++i)
        producePam(&outPam, width, &srf.imgs[i]);

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

    convertsrf(cmdline, ifP, stdout);

    pm_closer(ifP);

    return 0;
}
