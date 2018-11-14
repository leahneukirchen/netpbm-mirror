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

typedef enum {
  BLEND_AVERAGE,     /* Take the average color of all pixels */
  BLEND_RANDOM,      /* Take each pixel color from a randomly selected image */
  BLEND_MASK         /* Take each pixel color from the image indicated by a mask */
} blendType;

static const unsigned int randSamples = 1024;   /* Random samples to draw per file */

struct programState {
  blendType blend;                /* How to blend the files */
  unsigned int nFiles;            /* Number of input files */
  const char ** inFileNames;      /* Name of each input file */
  struct pam * inPam;             /* List of input-file PAM structures */
  tuple ** inTupleRows;           /* Current row from each input file */
  const char * outFileName;       /* Name of the output file */
  struct pam outPam;              /* Output-file PAM structure */
  tuple * outTupleRow;            /* Row to write to the output file */
  const char * maskFileName;      /* Name of the image-mask file */
  struct pam maskPam;             /* PAM structure for the image mask */
  tuple * maskTupleRow;           /* Row to read from the mask file */
  double sigma;                   /* Standard deviation when selecting images via a mask */
  unsigned long ** imageWeights;  /* Per-image weights as a function of grayscale level */
};

/* Parse the command line. */
static void
parseCommandLine(int argc, const char ** argv,
                 struct programState * const stateP) {
  optStruct3 opt;
  unsigned int option_def_index = 0;
  optEntry * option_def;
  const char * blend_string = "average";
  unsigned int blend_spec = 0;
  unsigned int outfile_spec = 0;
  unsigned int maskfile_spec = 0;
  unsigned int stdev_spec = 0;
  float sigma;
  int i;

  /* Define the allowed command-line options. */
  MALLOCARRAY(option_def, 100);
  OPTENT3('b', "blend", OPT_STRING, &blend_string, &blend_spec, 0);
  OPTENT3('o', "outfile", OPT_STRING, &stateP->outFileName, &outfile_spec, 0);
  OPTENT3('m', "maskfile", OPT_STRING, &stateP->maskFileName, &maskfile_spec, 0);
  OPTENT3('s', "stdev", OPT_FLOAT, &sigma, &stdev_spec, 0);
  opt.opt_table = option_def;
  opt.short_allowed = 1;
  opt.allowNegNum = 0;

  /* Parse the command line. */
  pm_optParseOptions3(&argc, (char **) argv, opt, sizeof(opt), 0);
  if (!outfile_spec)
    stateP->outFileName = "-";
  if (!strcmp(blend_string, "average"))
    stateP->blend = BLEND_AVERAGE;
  else if (!strcmp(blend_string, "random"))
    stateP->blend = BLEND_RANDOM;
  else if (!strcmp(blend_string, "mask"))
    stateP->blend = BLEND_MASK;
  else
    pm_error("Unrecognized blend type \"%s\"", blend_string);
  if (stateP->blend == BLEND_MASK) {
    if (maskfile_spec == 0)
      pm_error("--maskfile=<filename> must be used with --blend=mask");
    stateP->sigma = stdev_spec == 1 ? (double)sigma : 0.25;
  }
  else {
    if (maskfile_spec != 0)
      pm_message("Ignoring the mask file because --blend=mask was not specified");
    if (stdev_spec != 0)
      pm_message("Ignoring the image standard deviation because --blend=mask was not specified");
  }
  if (argc < 2)
    pm_error("You must provide the names of the files to blend together");
  stateP->nFiles = argc - 1;
  MALLOCARRAY(stateP->inFileNames, stateP->nFiles);
  for (i = 1; i < argc; i++)
    stateP->inFileNames[i - 1] = argv[i];
  free(option_def);
}

/* Open all of the input files and the output file.  Abort if the
 * input files don't all have the same size and format. */
static void
openFiles(struct programState * const stateP) {
  struct pam * inPam;
  unsigned int i;

  MALLOCARRAY(stateP->inPam, stateP->nFiles);
  MALLOCARRAY(stateP->inTupleRows, stateP->nFiles);

  /* Open all of the input files. */
  inPam = stateP->inPam;
  for (i = 0; i < stateP->nFiles; i++) {
    FILE * ifP = pm_openr(stateP->inFileNames[i]);
    pnm_readpaminit(ifP, &inPam[i], PAM_STRUCT_SIZE(tuple_type));
    if (inPam[i].width != inPam[0].width || inPam[i].height != inPam[0].height)
      pm_error("All images must have the same dimensions");
    if (inPam[i].depth != inPam[0].depth ||
        inPam[i].maxval != inPam[0].maxval ||
        strcmp(inPam[i].tuple_type, inPam[0].tuple_type))
      pm_error("All images must have the same number and range of colors");
    stateP->inTupleRows[i] = pnm_allocpamrow(&inPam[i]);
  }

  /* Open the mask file for reading. */
  if (stateP->blend == BLEND_MASK) {
    struct pam * maskPam = &stateP->maskPam;
    FILE * mfP = pm_openr(stateP->maskFileName);
    pnm_readpaminit(mfP, maskPam, PAM_STRUCT_SIZE(tuple_type));
    if (maskPam->width != inPam[0].width || maskPam->height != inPam[0].height)
      pm_error("The mask image must have the same dimensions as the input images");
    if (maskPam->depth > 1)
      pm_message("Ignoring all but the first channel of the mask image");
    stateP->maskTupleRow = pnm_allocpamrow(maskPam);
  }

  /* Open the output file for writing. */
  stateP->outPam = inPam[0];
  stateP->outPam.file = pm_openw(stateP->outFileName);
  stateP->outTupleRow = pnm_allocpamrow(&stateP->outPam);
  pnm_writepaminit(&stateP->outPam);
}

/* Blend one tuple of the input images into a new tuple by selecting a tuple
 * from a random input image. */
static void
blendTuplesRandom(struct programState * const stateP, unsigned int col, sample * outSamps) {
  unsigned int depth = stateP->inPam[0].depth;
  unsigned int samp;

  unsigned int img = (unsigned int) (random() % stateP->nFiles);
  for (samp = 0; samp < depth; samp++)
    outSamps[samp] = ((sample *)stateP->inTupleRows[img][col])[samp];
}

/* Blend one tuple of the input images into a new tuple by averaging all input
 * tuples. */
static void
blendTuplesAverage(struct programState * const stateP, unsigned int col, sample * outSamps) {
  unsigned int depth = stateP->inPam[0].depth;
  unsigned int nFiles = stateP->nFiles;
  unsigned int samp;

  for (samp = 0; samp < depth; samp++) {
    unsigned int img;

    outSamps[samp] = 0;
    for (img = 0; img < nFiles; img++)
      outSamps[samp] += ((sample *)stateP->inTupleRows[img][col])[samp];
    outSamps[samp] /= nFiles;
  }
}

/* Return two normally distributed random numbers. */
static void
random_normal_2(double *r1, double *r2) {
  double u1, u2;

  do {
    u1 = drand48();
    u2 = drand48();
  }
  while (u1 <= DBL_EPSILON);
  *r1 = sqrt(-2.0*log(u1))*cos(2.0*M_PI*u2);
  *r2 = sqrt(-2.0*log(u1))*sin(2.0*M_PI*u2);
}

/* Precompute the weight to give to each image as a function of grayscale level. */
static void
precomputeImageWeights(struct programState * const stateP) {
  unsigned int maxGray = (unsigned int) stateP->maskPam.maxval;
  unsigned int nFiles = stateP->nFiles;
  unsigned int i, j, k;

  /* Allocate memory for the image weights, */
  MALLOCARRAY(stateP->imageWeights, maxGray + 1);
  for (i = 0; i <= maxGray; i++) {
    MALLOCARRAY(stateP->imageWeights[i], nFiles);
    memset(stateP->imageWeights[i], 0, nFiles*sizeof(unsigned long));
  }

  /* Populate the image-weight arrays. */
  for (i = 0; i <= maxGray; i++) {
    double pctGray = i / (double)maxGray;

    for (j = 0; j < nFiles*randSamples; ) {
      double r[2];
      int img;

      random_normal_2(&r[0], &r[1]);
      for (k = 0; k < 2; k++) {
        img = (int) (r[k]*stateP->sigma + pctGray*nFiles*0.999999);  /* Scale [0, 1] to [0, 1) (sort of). */
        if (img >= 0 && img < (int)nFiles) {
          stateP->imageWeights[i][img]++;
          j++;
        }
      }
    }
  }
}

/* Blend one tuple of the input images into a new tuple according to the gray
 * levels specified in a mask file. */
static void
blendTuplesMask(struct programState * const stateP, unsigned int col, sample * outSamps) {
  unsigned int depth = stateP->inPam[0].depth;
  sample grayLevel = ((sample *)stateP->maskTupleRow[col])[0];
  unsigned int nFiles = stateP->nFiles;
  unsigned int samp;
  unsigned int img;

  for (samp = 0; samp < depth; samp++)
    outSamps[samp] = 0;
  for (img = 0; img < nFiles; img++) {
    unsigned long weight = stateP->imageWeights[grayLevel][img];
    if (weight != 0)
      for (samp = 0; samp < depth; samp++)
        outSamps[samp] += ((sample *)stateP->inTupleRows[img][col])[samp] * weight;
  }
  for (samp = 0; samp < depth; samp++)
    outSamps[samp] /= randSamples*nFiles;
}

/* Blend one row of input images into a new row. */
static void
blendImageRow(struct programState * const stateP) {
  unsigned int width = stateP->inPam[0].width;
  unsigned int col;

  for (col = 0; col < width; col++) {
    sample * outSamps = (sample *)stateP->outTupleRow[col];

    switch (stateP->blend) {
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

    default:
      pm_error("Internal error: Invalid blend type");
      break;
    }
  }
}

/* Blend the images row-by-row into a new image. */
static void
blendImages(struct programState * const stateP) {
  unsigned int nRows = (unsigned int) stateP->inPam[0].height;
  unsigned int img;
  unsigned int row;

  for (row = 0; row < nRows; row++) {
    for (img = 0; img < stateP->nFiles; img++)
      pnm_readpamrow(&stateP->inPam[img], stateP->inTupleRows[img]);
    if (stateP->blend == BLEND_MASK)
      pnm_readpamrow(&stateP->maskPam, stateP->maskTupleRow);
    blendImageRow(stateP);
    pnm_writepamrow(&stateP->outPam, stateP->outTupleRow);
  }
}

/* Deallocate all of the resources we allocated. */
static void
deallocateResources(struct programState * const stateP) {
  unsigned int i;

  if (stateP->blend == BLEND_MASK) {
    for (i = 0; i <= stateP->maskPam.maxval; i++)
      free(stateP->imageWeights[i]);
    free(stateP->imageWeights);
    pnm_freepamrow(stateP->maskTupleRow);
    pm_close(stateP->maskPam.file);
  }
  for (i = 0; i < stateP->nFiles; i++) {
    pnm_freepamrow(stateP->inTupleRows[i]);
    pm_close(stateP->inPam[i].file);
  }
  free(stateP->outTupleRow);
  free(stateP->inTupleRows);
  free(stateP->inPam);
  free(stateP->inFileNames);
  pm_close(stateP->outPam.file);
}

int
main(int argc, const char * argv[]) {
  struct programState state;

  pm_proginit(&argc, argv);
  parseCommandLine(argc, argv, &state);
  openFiles(&state);
  if (state.blend == BLEND_MASK)
    precomputeImageWeights(&state);
  blendImages(&state);
  deallocateResources(&state);
  return 0;
}
