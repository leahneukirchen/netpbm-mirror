/*
 * Convert a Netpbm image to SRF (Garmin vehicle)
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



static uint8_t
srfScale(sample             const unscaled,
         const struct pam * const pamP) {

    return pnm_scalesample(unscaled, pamP->maxval, 255);
}



static uint16_t
srfColorFromTuple(tuple              const t,
                  const struct pam * const pamP) {

    unsigned int redPlane, grnPlane, bluPlane;

    if (pamP->depth >= 3) {
        redPlane = PAM_RED_PLANE;
        grnPlane = PAM_GRN_PLANE;
        bluPlane = PAM_BLU_PLANE;
    } else {
        redPlane = 0;
        grnPlane = 0;
        bluPlane = 0;
    }
    return
        (((srfScale(t[redPlane], pamP) >> 3) & 0x1f) << 11) |
        (((srfScale(t[grnPlane], pamP) >> 3) & 0x1f) <<  6) |
        (((srfScale(t[bluPlane], pamP) >> 3) & 0x1f) <<  0);
}



static uint8_t
srfAlphaFromTuple(tuple              const t,
                  const struct pam * const pamP) {

    uint8_t retval;
    bool haveOpacity;
    unsigned int opacityPlane;

    pnm_getopacity(pamP, &haveOpacity, &opacityPlane);

    if (haveOpacity) {
        uint8_t const scaled = srfScale(t[opacityPlane], pamP);

        retval = scaled == 0xff ? SRF_ALPHA_OPAQUE :  128 - (scaled >> 1);
    } else
        retval = SRF_ALPHA_OPAQUE;

    return retval;
}



static void
producepam(struct cmdlineInfo const cmdline,
           struct pam *       const pamP,
           FILE *             const ofP) {
/*----------------------------------------------------------------------------
   Design note:  It's is really a modularity violation that we have
   all the command line parameters as an argument.  We do it because we're
   lazy -- it takes a great deal of work to carry all that information as
   separate arguments -- and it's only a very small violation.
-----------------------------------------------------------------------------*/
    uint16_t width3d, height3d;
    uint16_t widthOv, heightOv;
    unsigned int row;
    unsigned int imgCt;
    unsigned int i;
    struct srf srf;
    tuple * tuplerow;

    if (verbose)
        pm_message("reading %ux%u image", pamP->width, pamP->height);

    /* Figure out the dimensions.  The frame series should come in pairs,
       each series should contain 36 frames, the first set should never
       be smaller than the 2nd set, the sets should have the same dimension
       combos as other sets, and each frame is square.

       So if we have two frame series with the first being 80px tall and
       the second is 60px tall, we can figure out things from there.
    */
    height3d = pamP->width / SRF_NUM_FRAMES;
    for (row = 1; row <= pamP->height / height3d; ++row) {
        heightOv = (pamP->height - (height3d * row)) / row;
        if (heightOv <= height3d) {
            if ((heightOv + height3d) * row == pamP->height)
                break;
        }
    }
    imgCt = row * 2;
    width3d = height3d * SRF_NUM_FRAMES;
    widthOv = heightOv * SRF_NUM_FRAMES;

    if (verbose)
        pm_message("detected %u sets of 16-bit RGB images (%ux%u and %ux%u)",
                   imgCt, width3d, height3d, widthOv, heightOv);

    srf_init(&srf, imgCt, width3d, height3d, widthOv, heightOv);

    /* Scan out each frame series */
    tuplerow = pnm_allocpamrow(pamP);
    for (i = 0; i < srf.header.img_cnt; ++i) {
        struct srf_img * const imgP = &srf.imgs[i];

        unsigned int row;

        for (row = 0; row < imgP->header.height; ++row) {
            uint32_t        const off   = row * imgP->header.width;
            uint16_t *      const data  = &imgP->data.data[off];
            unsigned char * const alpha = &imgP->alpha.data[off];

            unsigned int col;

            pnm_readpamrow(pamP, tuplerow);
            for (col = 0; col < imgP->header.width; ++col) {
                alpha[col] = srfAlphaFromTuple(tuplerow[col], pamP);
                data[col]  = srfColorFromTuple(tuplerow[col], pamP);
            }
        }
    }
    pnm_freepamrow(tuplerow);

    srf_write(ofP, &srf);

    srf_term(&srf);
}



int
main(int argc, const char * argv[]) {

  struct cmdlineInfo cmdline;
  FILE *             ifP;
  struct pam         inPam;

  pm_proginit(&argc, argv);

  parseCommandLine(argc, argv, &cmdline);

  verbose = cmdline.verbose;

  ifP = pm_openr(cmdline.inputFileName);

  pnm_readpaminit(ifP, &inPam, PAM_STRUCT_SIZE(tuple_type));

  producepam(cmdline, &inPam, stdout);

  pm_closer(ifP);

  return 0;
}
