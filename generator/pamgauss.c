#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "pm_c_util.h"
#include "shhopt.h"
#include "mallocvar.h"
#include "pam.h"



struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    unsigned int width;
    unsigned int height;
    unsigned int maxval;
    float        sigma;
    unsigned int oversample;
    unsigned int maximize;
    const char * tupletype;
};



static void
parseCommandLine(int argc, const char ** argv,
                 struct CmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
  Convert program invocation arguments (argc,argv) into a format the
  program can use easily, struct cmdlineInfo.  Validate arguments along
  the way and exit program with message if invalid.

  Note that some string information we return as *cmdlineP is in the storage
  argv[] points to.
-----------------------------------------------------------------------------*/
    optEntry * option_def;
        /* Instructions to OptParseOptions2 on how to parse our options.
         */
    optStruct3 opt;

    unsigned int tupletypeSpec, maxvalSpec, sigmaSpec, oversampleSpec;
    unsigned int option_def_index;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENTRY */
    OPTENT3(0,   "tupletype",  OPT_STRING, &cmdlineP->tupletype,
            &tupletypeSpec,      0);
    OPTENT3(0,   "maxval",     OPT_UINT,   &cmdlineP->maxval,
            &maxvalSpec,         0);
    OPTENT3(0,   "sigma",      OPT_FLOAT,  &cmdlineP->sigma,
            &sigmaSpec,          0);
    OPTENT3(0,   "maximize",   OPT_FLAG,   NULL,
            &cmdlineP->maximize, 0);
    OPTENT3(0,   "oversample", OPT_UINT,   &cmdlineP->oversample,
            &oversampleSpec,     0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We have no parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (!tupletypeSpec)
        cmdlineP->tupletype = "";
    else {
        struct pam pam;
        if (strlen(cmdlineP->tupletype)+1 > sizeof(pam.tuple_type))
            pm_error("The tuple type you specified is too long.  "
                     "Maximum %u characters.",
                     (unsigned)sizeof(pam.tuple_type)-1);
    }

    if (!sigmaSpec)
        pm_error("You must specify the -sigma option.");
    else if (cmdlineP->sigma <= 0.0)
        pm_error("-sigma must be positive.  You specified %f",
                 cmdlineP->sigma);

    if (!maxvalSpec)
        cmdlineP->maxval = PNM_MAXMAXVAL;
    else {
        if (cmdlineP->maxval > PNM_OVERALLMAXVAL)
            pm_error("The maxval you specified (%u) is too big.  "
                     "Maximum is %u", cmdlineP->maxval, PNM_OVERALLMAXVAL);
        if (cmdlineP->maxval < 1)
            pm_error("-maxval must be at least 1");
    }

    if (oversampleSpec) {
        if (cmdlineP->oversample < 1)
            pm_error("The oversample factor (-oversample) "
                     "must be at least 1.");
    } else
        cmdlineP->oversample = ceil(5.0 / cmdlineP->sigma);

    if (argc-1 < 2)
        pm_error("Need two arguments: width and height.");
    else if (argc-1 > 2)
        pm_error("Only two arguments allowed: width and height.  "
                 "You specified %d", argc-1);
    else {
        cmdlineP->width  = pm_parse_width(argv[1]);
        cmdlineP->height = pm_parse_height(argv[2]);
        if (cmdlineP->width <= 0)
            pm_error("width argument must be a positive number.  You "
                     "specified '%s'", argv[1]);
        if (cmdlineP->height <= 0)
            pm_error("height argument must be a positive number.  You "
                     "specified '%s'", argv[2]);
    }
    free(option_def);
}



static double
distFromCenter(unsigned int const width,
               unsigned int const height,
               double       const x,
               double       const y)
{
    return sqrt(SQR(x - (double)width  / 2) +
                SQR(y - (double)height / 2));
}



static double
gauss(double const arg,
      double const sigma) {
/*----------------------------------------------------------------------------
   Compute the value of the gaussian function centered at zero with
   standard deviation 'sigma' and amplitude 1, at 'arg'.
-----------------------------------------------------------------------------*/
    double const exponent = - SQR(arg) / (2 * SQR(sigma));

    return exp(exponent);
}



static double
pixelValue(unsigned int const width,
           unsigned int const height,
           unsigned int const row,
           unsigned int const col,
           unsigned int const subpixDivision,
           double       const sigma) {
/*----------------------------------------------------------------------------
  The gaussian value for the pixel at row 'row', column 'col' in an image
  described by *pamP.

  This is the mean of the values of the gaussian function computed at
  all the subpixel locations within the pixel when it is divided into
  subpixels 'subpixDivision' times horizontally and vertically.

  The gaussian function has standard deviation 'sigma' and amplitude 1.
-----------------------------------------------------------------------------*/
    double const offset = 1.0 / (subpixDivision * 2);
    double const y0     = (double)row + offset;
    double const x0     = (double)col + offset;

    double const subpixSize = 1.0 / subpixDivision;

    unsigned int i;
    double total;
        /* Running total of the gaussian values at all subpixel locations */

    for (i = 0, total = 0.0; i < subpixDivision; ++i) {
        /* Sum up one column of subpixels */

        unsigned int j;

        for (j = 0; j < subpixDivision; ++j) {
            double const dist =
                distFromCenter(width, height,
                               x0 + i * subpixSize,
                               y0 + j * subpixSize);

            total += gauss(dist, sigma);
        }
    }

    return total / SQR(subpixDivision);
}



static double **
gaussianKernel(unsigned int const width,
               unsigned int const height,
               unsigned int const subpixDivision,
               double       const sigma) {
/*----------------------------------------------------------------------------
   A Gaussian matrix 'width' by 'height', with each value being the mean
   of a Gaussian function evaluated at 'subpixDivision' x 'subpixDivision'
   locations.

   Return value is newly malloc'ed storage that Caller must free.
-----------------------------------------------------------------------------*/
    double ** kernel;
    unsigned int row;

    MALLOCARRAY2(kernel, height, width);

    if (!kernel)
        pm_error("Unable to allocate %u x %u array in which to build kernel",
                 height, width);

    for (row = 0; row < height; ++row) {
        unsigned int col;
        for (col = 0; col < width; ++col) {
            double const gaussval =
                pixelValue(width, height, row, col, subpixDivision, sigma);
            kernel[row][col] = gaussval;
        }
    }
    return kernel;
}



static double
maximumKernelValue(double **    const kernel,
                   unsigned int const width,
                   unsigned int const height) {

    /* As this is Gaussian in both directions, centered at the center,
       we know the maximum value is at the center.
    */
    return kernel[height/2][width/2];
}



static double
totalKernelValue(double **    const kernel,
                 unsigned int const width,
                 unsigned int const height) {

    double total;
    unsigned int row;

    for (row = 0, total = 0.0; row < height; ++row) {
        unsigned int col;

        for (col = 0; col < width; ++col)
            total += kernel[row][col];
    }

    return total;
}



static void
initpam(struct pam * const pamP,
        unsigned int const width,
        unsigned int const height,
        sample       const maxval,
        const char * const tupleType,
        FILE *       const ofP) {

    pamP->size        = sizeof(*pamP);
    pamP->len         = PAM_STRUCT_SIZE(tuple_type);
    pamP->file        = ofP;
    pamP->format      = PAM_FORMAT;
    pamP->plainformat = 0;
    pamP->width       = width;
    pamP->height      = height;
    pamP->depth       = 1;
    pamP->maxval      = maxval;
    strcpy(pamP->tuple_type, tupleType);
}



static void
writePam(double **    const kernel,
         unsigned int const width,
         unsigned int const height,
         sample       const maxval,
         const char * const tupleType,
         double       const normalizer,
         FILE *       const ofP) {
/*----------------------------------------------------------------------------
   Write the kernel 'kernel', which is 'width' by 'height', as a PAM image
   with maxval 'maxval' and tuple type 'tupleType' to file *ofP.

   Divide the kernel values by 'normalizer' to get the normalized PAM sample
   value.  Assume that no value in 'kernel' is greater that 'normalizer'.
-----------------------------------------------------------------------------*/
    struct pam pam;
    unsigned int row;
    tuplen * tuplerown;

    initpam(&pam, width, height, maxval, tupleType, ofP);

    pnm_writepaminit(&pam);

    tuplerown = pnm_allocpamrown(&pam);

    for (row = 0; row < pam.height; ++row) {
        unsigned int col;
        for (col = 0; col < pam.width; ++col) {
            tuplerown[col][0] = kernel[row][col] / normalizer;

            assert(tuplerown[col][0] <= 1.0);
        }
        pnm_writepamrown(&pam, tuplerown);
    }

    pnm_freepamrown(tuplerown);
}



int
main(int argc, const char **argv) {
    struct CmdlineInfo cmdline;
    double ** kernel;
    double    normalizer;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    kernel = gaussianKernel(cmdline.width, cmdline.height, cmdline.oversample,
                            cmdline.sigma);

    normalizer = cmdline.maximize ?
        maximumKernelValue(kernel, cmdline.width, cmdline.height) :
        totalKernelValue(kernel, cmdline.width, cmdline.height);

    writePam(kernel,
             cmdline.width, cmdline.height, cmdline.maxval, cmdline.tupletype,
             normalizer, stdout);

    pm_freearray2((void **)kernel);

    return 0;
}



