/*************************************************
 * Blend multiple Netpbm files into a single one *
 *                                               *
 * By Scott Pakin <scott+pbm@pakin.org>          *
 *************************************************/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <float.h>
#include <math.h>
#include "pam.h"
#include "shhopt.h"
#include "mallocvar.h"
#include "nstring.h"
#include "rand.h"

typedef enum {
    BLEND_AVERAGE,   /* Take the average color of all pixels */
    BLEND_RANDOM,    /* Take each pixel color from a randomly selected image */
    BLEND_MASK   /* Take each pixel color from the image indicated by a mask */
} BlendType;

static unsigned int const randSamples = 1024;
    /* Random samples to draw per file */

struct ProgramState {
    unsigned int     inFileCt;      /* Number of input files */
    struct pam *     inPam;         /* List of input-file PAM structures */
    tuple **         inTupleRows;   /* Current row from each input file */
    struct pam       outPam;        /* Output-file PAM structure */
    tuple *          outTupleRow;   /* Row to write to the output file */
    const char *     maskFileName;  /* Name of the image-mask file */
    struct pam       maskPam;       /* PAM structure for the image mask */
    tuple *          maskTupleRow;  /* Row to read from the mask file */
    double           sigma;
        /* Standard deviation when selecting images via a mask */
    unsigned long ** imageWeights;
        /* Per-image weights as a function of grayscale level */
    struct pm_randSt randSt;
        /* Random number generator parameters and internal state */
};



struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    BlendType        blend;
    const char *     maskfile;
    float            stdev;
    unsigned int     randomseed;
    unsigned int     randomseedSpec;
    unsigned int     inFileNameCt;  /* Number of input files */
    const char **    inFileName;    /* Name of each input file */
};



static void
freeCmdline(struct CmdlineInfo * const cmdlineP) {

    free(cmdlineP->inFileName);
}



static void
parseCommandLine(int argc, const char ** argv,
                 struct CmdlineInfo * const cmdlineP) {

    optStruct3 opt;
    unsigned int option_def_index = 0;
    optEntry * option_def;
    unsigned int blendSpec, maskfileSpec, stdevSpec;
    const char * blendOpt;

    /* Define the allowed command-line options. */
    MALLOCARRAY(option_def, 100);
    OPTENT3(0, "blend",     OPT_STRING,     &blendOpt,
            &blendSpec,     0);
    OPTENT3(0, "maskfile",  OPT_STRING,     &cmdlineP->maskfile,
            &maskfileSpec,  0);
    OPTENT3(0, "stdev",      OPT_FLOAT,     &cmdlineP->stdev,
            &stdevSpec,     0);
    OPTENT3(0, "randomseed", OPT_UINT,      &cmdlineP->randomseed,
            &cmdlineP->randomseedSpec,     0);

    opt.opt_table = option_def;
    opt.short_allowed = 0;
    opt.allowNegNum = 0;

    pm_optParseOptions3(&argc, (char **) argv, opt, sizeof(opt), 0);
    if (blendSpec) {
        if (streq(blendOpt, "average"))
            cmdlineP->blend = BLEND_AVERAGE;
        else if (streq(blendOpt, "random"))
            cmdlineP->blend = BLEND_RANDOM;
        else if (streq(blendOpt, "mask"))
            cmdlineP->blend = BLEND_MASK;
        else
            pm_error("Unrecognized -blend value '%s'.  "
                     "We recognize 'average', 'random', and 'mask'", blendOpt);
    } else
        cmdlineP->blend = BLEND_AVERAGE;

    if (cmdlineP->blend == BLEND_MASK) {
        if (!maskfileSpec)
            pm_error("Because you specified -blend=mask, "
                     "you must also specify -maskfile");
    } else {
        if (maskfileSpec)
            pm_message("Ignoring -maskfile because -blend=mask "
                       "is not specified");
        if (stdevSpec)
            pm_message("Ignoring -stdev because -blend=mask "
                       "is not specified");
    }
    if (!stdevSpec)
        cmdlineP->stdev = 0.25;

    if (argc-1 < 1)
        pm_error("You must specify the names of the files to blend together "
                 "as arguments");

    cmdlineP->inFileNameCt = argc-1;
    MALLOCARRAY(cmdlineP->inFileName, argc-1);
    if (!cmdlineP->inFileName)
        pm_error("Unable to allocate space for %u file names", argc-1);
    {
        unsigned int i;
        for (i = 0; i < argc-1; ++i)
            cmdlineP->inFileName[i] = argv[1+i];
    }
    free(option_def);
}

static void
initInput(unsigned int          const inFileCt,
          const char **         const inFileName,
          struct ProgramState * const stateP) {
/*----------------------------------------------------------------------------
  Open all of the input files.

  Abort if the input files don't all have the same size and format.
-----------------------------------------------------------------------------*/
    struct pam * inPam;
    unsigned int i;

    MALLOCARRAY(inPam, inFileCt);
    if (!inPam)
        pm_error("Failed to allocated memory for PAM structures for %u "
                 "input files", inFileCt);
    MALLOCARRAY(stateP->inTupleRows, inFileCt);
    if (!stateP->inTupleRows)
        pm_error("Failed to allocated memory for PAM structures for %u "
                 "input rasters", inFileCt);

    for (i = 0; i < inFileCt; ++i) {
        FILE * const ifP = pm_openr(inFileName[i]);
        pnm_readpaminit(ifP, &inPam[i], PAM_STRUCT_SIZE(tuple_type));
        if (inPam[i].width != inPam[0].width ||
            inPam[i].height != inPam[0].height)
            pm_error("Input image %u has different dimensions from "
                     "earlier input images", i);
        if (inPam[i].depth != inPam[0].depth)
            pm_error("Input image %u has different depth from "
                     "earlier input images", i);
        if (inPam[i].maxval != inPam[0].maxval)
            pm_error("Input image %u has different maxval from "
                     "earlier input images", i);
        if (!streq(inPam[i].tuple_type, inPam[0].tuple_type))
            pm_error("Input image %u has different tuple type from "
                     "earlier input images", i);
    }

    for (i = 0; i < inFileCt; ++i)
        stateP->inTupleRows[i] = pnm_allocpamrow(&inPam[i]);

    stateP->inPam    = inPam;
    stateP->inFileCt = inFileCt;
}



static void
termInput(struct ProgramState * const stateP) {
/*----------------------------------------------------------------------------
  Deallocate all of the resources we allocated.
-----------------------------------------------------------------------------*/
    unsigned int i;

    for (i = 0; i < stateP->inFileCt; ++i) {
        pnm_freepamrow(stateP->inTupleRows[i]);
        pm_close(stateP->inPam[i].file);
    }

    free(stateP->inTupleRows);
    free(stateP->inPam);
}



static void
initMask(const char *          const maskFileName,
         struct ProgramState * const stateP) {

    struct pam * const maskPamP = &stateP->maskPam;

    FILE * const mfP = pm_openr(maskFileName);

    pnm_readpaminit(mfP, maskPamP, PAM_STRUCT_SIZE(tuple_type));

    if (maskPamP->width != stateP->inPam[0].width ||
        maskPamP->height != stateP->inPam[0].height) {

        pm_error("The mask image does not have have the same dimensions "
                 "as the input images");
    }
    if (maskPamP->depth > 1)
        pm_message("Ignoring all but the first channel of the mask image");

    stateP->maskTupleRow = pnm_allocpamrow(maskPamP);
}



static void
termMask(struct ProgramState * const stateP) {

    unsigned int i;

    for (i = 0; i <= stateP->maskPam.maxval; ++i)
        free(stateP->imageWeights[i]);

    free(stateP->imageWeights);

    pnm_freepamrow(stateP->maskTupleRow);

    pm_close(stateP->maskPam.file);
}



static void
initOutput(FILE *                const ofP,
           struct ProgramState * const stateP) {

    stateP->outPam      = stateP->inPam[0];
    stateP->outPam.file = ofP;
    stateP->outTupleRow = pnm_allocpamrow(&stateP->outPam);

    pnm_writepaminit(&stateP->outPam);
}


static void
termOutput(struct ProgramState * const stateP) {

    free(stateP->outTupleRow);

    pm_close(stateP->outPam.file);
}



static void
blendTuplesRandom(struct ProgramState * const stateP,
                  unsigned int          const col,
                  sample *              const outSamps) {
/*----------------------------------------------------------------------------
  Blend one tuple of the input images into a new tuple by selecting a tuple
  from a random input image.
-----------------------------------------------------------------------------*/
    unsigned int const depth = stateP->inPam[0].depth;
    unsigned int const img = (unsigned int) (pm_rand(&stateP->randSt) %
                                             stateP->inFileCt);
    unsigned int samp;

    for (samp = 0; samp < depth; ++samp)
        outSamps[samp] = ((sample *)stateP->inTupleRows[img][col])[samp];
}



static void
blendTuplesAverage(struct ProgramState * const stateP,
                   unsigned int          const col,
                   sample *              const outSamps) {
/*----------------------------------------------------------------------------
  Blend one tuple of the input images into a new tuple by averaging all input
  tuples.
-----------------------------------------------------------------------------*/
    unsigned int const depth = stateP->inPam[0].depth;

    unsigned int samp;

    for (samp = 0; samp < depth; ++samp) {
        unsigned int img;

        for (img = 0, outSamps[samp] = 0; img < stateP->inFileCt; ++img)
            outSamps[samp] += ((sample *)stateP->inTupleRows[img][col])[samp];
        outSamps[samp] /= stateP->inFileCt;
    }
}



#if 0
static void
randomNormal2(double *           const r1P,
              double *           const r2P,
              struct pm_randSt * const randStP) {
/*----------------------------------------------------------------------------
  Return two normally distributed random numbers.
-----------------------------------------------------------------------------*/
    double u1, u2;

    do {
        u1 = drand48(randStP);
        u2 = drand48(randStP);
    }
    while (u1 <= DBL_EPSILON);

    *r1P = sqrt(-2.0*log(u1)) * cos(2.0*M_PI*u2);
    *r2P = sqrt(-2.0*log(u1)) * sin(2.0*M_PI*u2);
}
#endif



static void
precomputeImageWeights(struct ProgramState * const stateP,
                       double                const sigma) {
/*----------------------------------------------------------------------------
  Precompute the weight to give to each image as a function of grayscale
  level.
-----------------------------------------------------------------------------*/
    unsigned int const maxGray = (unsigned int) stateP->maskPam.maxval;

    unsigned int i;

    MALLOCARRAY(stateP->imageWeights, maxGray + 1);
    if (!stateP->imageWeights)
        pm_error("Unable to allocate memory for image weights for %u "
                 "gray levels", maxGray);

    for (i = 0; i <= maxGray; ++i) {
        unsigned int j;
        MALLOCARRAY(stateP->imageWeights[i], stateP->inFileCt);
        if (!stateP->imageWeights[i])
            pm_error("Unable to allocate memory for image weights for %u "
                     "images for gray level %u", stateP->inFileCt, i);
        for (j = 0; j < stateP->inFileCt; ++j)
            stateP->imageWeights[i][j] = 0;
    }

    /* Populate the image-weight arrays. */
    for (i = 0; i <= maxGray; ++i) {
        double const pctGray = i / (double)maxGray;

        unsigned int j;

        for (j = 0; j < stateP->inFileCt * randSamples; ) {
            double r[2];
            unsigned int k;

            pm_gaussrand2(&stateP->randSt, &r[0], &r[1]);

            for (k = 0; k < 2; ++k) {
                int const img =
                    r[k] * sigma + pctGray * stateP->inFileCt * 0.999999;
                    /* Scale [0, 1] to [0, 1) (sort of). */
                if (img >= 0 && img < (int)stateP->inFileCt) {
                    ++stateP->imageWeights[i][img];
                    ++j;
                }
            }
        }
    }
}



static void
blendTuplesMask(struct ProgramState * const stateP,
                unsigned int          const col,
                sample *              const outSamps) {
/*----------------------------------------------------------------------------
  Blend one tuple of the input images into a new tuple according to the gray
  levels specified in a mask file.
-----------------------------------------------------------------------------*/
    unsigned int const depth = stateP->inPam[0].depth;
    sample const grayLevel = ((sample *)stateP->maskTupleRow[col])[0];

    unsigned int img;

    /* Initialize outSamps[] to zeroes */
    {
        unsigned int samp;

        for (samp = 0; samp < depth; ++samp)
            outSamps[samp] = 0;
    }

    /* Accumulate to outSamps[] */
    for (img = 0; img < stateP->inFileCt; ++img) {
        unsigned long weight = stateP->imageWeights[grayLevel][img];

        if (weight != 0) {
            unsigned int samp;

            for (samp = 0; samp < depth; ++samp)
                outSamps[samp] +=
                    ((sample *)stateP->inTupleRows[img][col])[samp] * weight;
        }
    }
    /* Scale all outSamps[] */
    {
        unsigned int samp;

        for (samp = 0; samp < depth; ++samp)
            outSamps[samp] /= randSamples * stateP->inFileCt;
    }
}



static void
blendImageRow(BlendType             const blend,
              struct ProgramState * const stateP) {
/*----------------------------------------------------------------------------
  Blend one row of input images into a new row.
-----------------------------------------------------------------------------*/
    unsigned int const width = stateP->inPam[0].width;

    unsigned int col;

    for (col = 0; col < width; ++col) {
        sample * const outSamps = stateP->outTupleRow[col];

        switch (blend) {
        case BLEND_RANDOM:
            /* Take each pixel from a different, randomly selected image. */
            blendTuplesRandom(stateP, col, outSamps);
            break;

        case BLEND_AVERAGE:
            /* Average each sample across all the images. */
            blendTuplesAverage(stateP, col, outSamps);
            break;

        case BLEND_MASK:
            /* Take each pixel from the image specified by the mask image. */
            blendTuplesMask(stateP, col, outSamps);
            break;
        }
    }
}



static void
blendImages(BlendType             const blend,
            struct ProgramState * const stateP) {
/*----------------------------------------------------------------------------
  Blend the images row-by-row into a new image.
-----------------------------------------------------------------------------*/
    unsigned int const nRows = stateP->inPam[0].height;

    unsigned int row;

    for (row = 0; row < nRows; ++row) {
        unsigned int img;

        for (img = 0; img < stateP->inFileCt; ++img)
            pnm_readpamrow(&stateP->inPam[img], stateP->inTupleRows[img]);

        if (blend == BLEND_MASK)
            pnm_readpamrow(&stateP->maskPam, stateP->maskTupleRow);

        blendImageRow(blend, stateP);

        pnm_writepamrow(&stateP->outPam, stateP->outTupleRow);
    }
}



int
main(int argc, const char * argv[]) {

    struct CmdlineInfo cmdline;
    struct ProgramState state;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    initInput(cmdline.inFileNameCt, cmdline.inFileName, &state);

    if (cmdline.blend == BLEND_MASK)
        initMask(cmdline.maskfile, &state);

    pm_randinit(&state.randSt);
    pm_srand2(&state.randSt, cmdline.randomseedSpec, cmdline.randomseed);

    initOutput(stdout, &state);

    if (cmdline.blend == BLEND_MASK)
        precomputeImageWeights(&state, cmdline.stdev);

    blendImages(cmdline.blend, &state);

    termOutput(&state);

    pm_randterm(&state.randSt);

    if (cmdline.blend == BLEND_MASK)
        termMask(&state);

    termInput(&state);

    freeCmdline(&cmdline);

    return 0;
}


/*  COPYRIGHT LICENSE and WARRANTY DISCLAIMER

Copyright (c) 2018 Scott Pakin
All rights reserved

Redistribution and use in source and binary forms, with or without
modification, are permitted (subject to the limitations in the disclaimer
below) provided that the following conditions are met:

  Redistributions of source code must retain the above copyright notice,
  this list of conditions and the following disclaimer.

  Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

  Neither the names of the oopyright owners nor the names of its contributors
  may be used to endorse or promote products derived from this software
  without specific prior written permission.

  NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE GRANTED BY
  THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
  CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT
  NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
  PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
  OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
  WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
  OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
  ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
