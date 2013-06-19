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
    int haveOpacity;
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
convertRaster(struct pam *     const pamP,
              struct srf_img * const imgP) {

    tuple * tuplerow;
    unsigned int row;

    tuplerow = pnm_allocpamrow(pamP);

    for (row = 0; row < pamP->height; ++row) {
        uint32_t        const off   = row * pamP->width;
        uint16_t *      const data  = &imgP->data.data[off];
        unsigned char * const alpha = &imgP->alpha.data[off];

        unsigned int col;

        pnm_readpamrow(pamP, tuplerow);

        for (col = 0; col < pamP->width; ++col) {
            alpha[col] = srfAlphaFromTuple(tuplerow[col], pamP);
            data[col]  = srfColorFromTuple(tuplerow[col], pamP);
        }
    }
    pnm_freepamrow(tuplerow);
}



static void
convertImage(FILE *       const ifP,
             struct srf * const srfP) {

    struct pam inpam;

    pnm_readpaminit(ifP, &inpam, PAM_STRUCT_SIZE(tuple_type));

    if (verbose)
        pm_message("reading %ux%u image", inpam.width, inpam.height);

    srf_create_img(srfP, inpam.width, inpam.height);

    convertRaster(&inpam, &srfP->imgs[srfP->header.img_cnt-1]);
}



int
main(int argc, const char * argv[]) {

  struct cmdlineInfo cmdline;
  FILE *             ifP;
  struct srf         srf;
  int                eof;   /* No more images in input */
  unsigned int       imageSeq;
      /* Sequence of current image in input file.  First = 0 */

  pm_proginit(&argc, argv);

  parseCommandLine(argc, argv, &cmdline);

  verbose = cmdline.verbose;

  ifP = pm_openr(cmdline.inputFileName);

  srf_init(&srf);

  eof = FALSE;
  for (imageSeq = 0; !eof; ++imageSeq) {
      if (verbose)
          pm_message("Converting Image %u", imageSeq);

      convertImage(ifP, &srf);

      pnm_nextimage(ifP, &eof);
  }

  srf_write(stdout, &srf);
    
  srf_term(&srf);
  pm_closer(ifP);

  return 0;
}



