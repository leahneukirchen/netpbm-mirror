/*
**
** Add gaussian, multiplicative gaussian, impulse, laplacian or
** poisson noise to a Netpbm image
**
** Version 1.0  November 1995
**
** Copyright (C) 1995 by Mike Burns (burns@cac.psu.edu)
**
** Adapted to Netpbm 2005.08.09, by Bryan Henderson
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

/* References
** ----------
** "Adaptive Image Restoration in Signal-Dependent Noise" by R. Kasturi
** Institute for Electronic Science, Texas Tech University  1982
**
** "Digital Image Processing Algorithms" by Ioannis Pitas
** Prentice Hall, 1993  ISBN 0-13-145814-0
*/

#define _XOPEN_SOURCE 500  /* get M_PI in math.h */

#include <assert.h>
#include <math.h>

#include "pm_c_util.h"
#include "mallocvar.h"
#include "rand.h"
#include "shhopt.h"
#include "pm_gamma.h"
#include "pam.h"

static double const EPSILON = 1.0e-5;

static double const SIGMA1_DEFAULT  = 4.0;
static double const SIGMA2_DEFAULT  = 20.0;
static double const MGSIGMA_DEFAULT = 0.5;
static double const LSIGMA_DEFAULT  = 10.0;
static double const TOLERANCE_DEFAULT  = 0.10;
static double const SALT_RATIO_DEFAULT = 0.5;
static double const LAMBDA_DEFAULT  = 12.0;


enum NoiseType {
    NOISETYPE_GAUSSIAN,
    NOISETYPE_IMPULSE,  /* aka salt and pepper noise */
    NOISETYPE_LAPLACIAN,
    NOISETYPE_MULTIPLICATIVE_GAUSSIAN,
    NOISETYPE_POISSON
};



struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFileName;

    enum NoiseType noiseType;

    unsigned int seedSpec;
    unsigned int seed;

    float lambda;
    float lsigma;
    float mgsigma;
    float sigma1;
    float sigma2;
    float tolerance;
    float saltRatio;
};



static enum NoiseType
typeFmName(const char * const name) {

    enum NoiseType retval;

    if (false)
        assert(false);
    else if (pm_keymatch(name, "gaussian", 1))
        retval = NOISETYPE_GAUSSIAN;
    else if (pm_keymatch(name, "impulse", 1))
        retval = NOISETYPE_IMPULSE;
    else if (pm_keymatch(name, "laplacian", 1))
        retval = NOISETYPE_LAPLACIAN;
    else if (pm_keymatch(name, "multiplicative_gaussian", 1))
        retval = NOISETYPE_MULTIPLICATIVE_GAUSSIAN;
    else if (pm_keymatch(name, "poisson", 1))
        retval = NOISETYPE_POISSON;
    else
        pm_error("Unrecognized -type value '%s'.  "
                 "We recognize 'gaussian', 'impulse', 'laplacian', "
                 "'multiplicative_gaussian', and 'poisson'", name);

    return retval;
}



static void
parseCommandLine(int argc, const char ** const argv,
                 struct CmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   Note that the file spec array we return is stored in the storage that
   was passed to us as the argv array.
-----------------------------------------------------------------------------*/
    optEntry * option_def;
        /* Instructions to OptParseOptions3 on how to parse our options. */
    optStruct3 opt;

    unsigned int option_def_index;

    unsigned int typeSpec, lambdaSpec, lsigmaSpec, mgsigmaSpec,
      sigma1Spec, sigma2Spec, toleranceSpec, saltRatioSpec;

    const char * type;

    MALLOCARRAY(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0,   "type",            OPT_STRING,   &type,
            &typeSpec,           0);
    OPTENT3(0,   "seed",            OPT_UINT,     &cmdlineP->seed,
            &cmdlineP->seedSpec, 0);
    OPTENT3(0,   "lambda",          OPT_FLOAT,    &cmdlineP->lambda,
            &lambdaSpec,         0);
    OPTENT3(0,   "lsigma",          OPT_FLOAT,    &cmdlineP->lsigma,
            &lsigmaSpec,         0);
    OPTENT3(0,   "mgsigma",         OPT_FLOAT,    &cmdlineP->mgsigma,
            &mgsigmaSpec,        0);
    OPTENT3(0,   "sigma1",          OPT_FLOAT,    &cmdlineP->sigma1,
            &sigma1Spec,         0);
    OPTENT3(0,   "sigma2",          OPT_FLOAT,    &cmdlineP->sigma2,
            &sigma2Spec,         0);
    OPTENT3(0,   "tolerance",       OPT_FLOAT,    &cmdlineP->tolerance,
            &toleranceSpec,      0);
    OPTENT3(0,   "salt",            OPT_FLOAT,    &cmdlineP->saltRatio,
            &saltRatioSpec,      0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We have no parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (!typeSpec)
        cmdlineP->noiseType = NOISETYPE_GAUSSIAN;
    else
        cmdlineP->noiseType = typeFmName(type);

    if (sigma1Spec) {
        if (cmdlineP->noiseType != NOISETYPE_GAUSSIAN)
            pm_error("-sigma1 is valid only with -type=gaussian");
        else if (cmdlineP->sigma1 < 0)
            pm_error("-sigma1 value must be non-negative.  You specified %f",
                     cmdlineP->sigma1);
    }

    if (sigma2Spec) {
        if (cmdlineP->noiseType != NOISETYPE_GAUSSIAN)
            pm_error("-sigma2 is valid only with -type=gaussian");
        else if (cmdlineP->sigma2 < 0)
            pm_error("-sigma2 value must be non-negative.  You specified %f",
                     cmdlineP->sigma2);
    }

    if (mgsigmaSpec) {
        if (cmdlineP->noiseType != NOISETYPE_MULTIPLICATIVE_GAUSSIAN)
            pm_error("-mgsigma is valid only with -type=multiplicative_guassian");
        else if (cmdlineP->mgsigma < 0)
            pm_error("-mgsigma value must be non-negative.  You specified %f",
                     cmdlineP->mgsigma);
    }

    if (toleranceSpec) {
        if (cmdlineP->noiseType != NOISETYPE_IMPULSE)
            pm_error("-tolerance is valid only with -type=impulse");
        else if (cmdlineP->tolerance < 0 || cmdlineP->tolerance > 1.0)
            pm_error("-tolerance value must be between 0.0 and 1.0.  "
                     "You specified %f",  cmdlineP->tolerance);
    }

    if (saltRatioSpec) {
        if (cmdlineP->noiseType != NOISETYPE_IMPULSE)
            pm_error("-salt is valid only with -type=impulse");
        else if (cmdlineP->saltRatio < 0 || cmdlineP->saltRatio > 1.0)
            pm_error("-salt value must be between 0.0 and 1.0.  "
                     "You specified %f",  cmdlineP->saltRatio);
    }

    if (lsigmaSpec) {
        if (cmdlineP->noiseType != NOISETYPE_LAPLACIAN)
        pm_error("-lsigma is valid only with -type=laplacian");
        else if (cmdlineP->lsigma <= 0)
            pm_error("-lsigma value must be positive.  You specified %f",
                     cmdlineP->lsigma);
    }

    if (lambdaSpec) {
        if (cmdlineP->noiseType != NOISETYPE_POISSON)
        pm_error("-lambda is valid only with -type=poisson");
        else if (cmdlineP->lambda <= 0)
            pm_error("-lambda value must be positive.  You specified %f",
                     cmdlineP->lambda);
    }

    if (!lambdaSpec)
        cmdlineP->lambda = LAMBDA_DEFAULT;

    if (!lsigmaSpec)
        cmdlineP->lsigma = LSIGMA_DEFAULT;

    if (!mgsigmaSpec)
        cmdlineP->mgsigma = MGSIGMA_DEFAULT;

    if (!sigma1Spec)
        cmdlineP->sigma1 = SIGMA1_DEFAULT;

    if (!sigma2Spec)
        cmdlineP->sigma2 = SIGMA2_DEFAULT;

    if (!toleranceSpec)
        cmdlineP->tolerance = TOLERANCE_DEFAULT;

    if (!saltRatioSpec)
        cmdlineP->saltRatio = SALT_RATIO_DEFAULT;

    if (!cmdlineP->seedSpec)
        cmdlineP->seed = pm_randseed();

    if (argc-1 > 1)
        pm_error("Too many arguments (%u).  File spec is the only argument.",
                 argc-1);

    if (argc-1 < 1)
        cmdlineP->inputFileName = "-";
    else
        cmdlineP->inputFileName = argv[1];

    free(option_def);
}



static void
addGaussianNoise(sample             const maxval,
                 sample             const origSample,
                 sample *           const newSampleP,
                 float              const sigma1,
                 float              const sigma2,
                 struct pm_randSt * const randStP) {
/*----------------------------------------------------------------------------
   Add Gaussian noise.

   Based on Kasturi/Algorithms of the ACM
-----------------------------------------------------------------------------*/

    double grnd1, grnd2; /* Gaussian random numbers.  mean=0 sigma=1 */
    double rawNewSample;

    pm_gaussrand2(randStP, &grnd1, &grnd2);

    rawNewSample =
      origSample + (sqrt((double) origSample) * sigma1 * grnd1) + (sigma2 * grnd2);

    *newSampleP = MAX(MIN((int)rawNewSample, maxval), 0);
}



static void
addImpulseNoise(sample             const maxval,
                sample             const origSample,
                sample *           const newSampleP,
                float              const tolerance,
                double             const saltRatio,
                struct pm_randSt * const randStP) {
/*----------------------------------------------------------------------------
   Add impulse (salt and pepper) noise
-----------------------------------------------------------------------------*/

    double const pepperRatio = 1.0 - saltRatio;
    double const loTolerance = tolerance * pepperRatio;
    double const hiTolerance = 1.0 - tolerance * saltRatio;
    double const sap         = pm_drand(randStP);

    *newSampleP =
        sap < loTolerance ? 0 :
        sap >= hiTolerance? maxval :
        origSample;
}



static void
addLaplacianNoise(sample             const maxval,
                  double             const infinity,
                  sample             const origSample,
                  sample *           const newSampleP,
                  float              const lsigma,
                  struct pm_randSt * const randStP) {
/*----------------------------------------------------------------------------
   Add Laplacian noise

   From Pitas' book.
-----------------------------------------------------------------------------*/
    double const u = pm_drand(randStP);

    double rawNewSample;

    if (u <= 0.5) {
        if (u <= EPSILON)
            rawNewSample = origSample - infinity;
        else
            rawNewSample = origSample + lsigma * log(2.0 * u);
    } else {
        double const u1 = 1.0 - u;
        if (u1 <= 0.5 * EPSILON)
            rawNewSample = origSample + infinity;
        else
            rawNewSample = origSample - lsigma * log(2.0 * u1);
    }
    *newSampleP = MIN(MAX((int)rawNewSample, 0), maxval);
}



static void
addMultiplicativeGaussianNoise(sample             const maxval,
                               double             const infinity,
                               sample             const origSample,
                               sample *           const newSampleP,
                               float              const mgsigma,
                               struct pm_randSt * const randStP) {
/*----------------------------------------------------------------------------
   Add multiplicative Gaussian noise

   From Pitas' book.
-----------------------------------------------------------------------------*/

    double rawNewSample;

    rawNewSample = origSample + (origSample * mgsigma * pm_gaussrand(randStP));

    *newSampleP = MIN(MAX((int)rawNewSample, 0), maxval);
}


static double
poissonPmf(double       const lambda,
           unsigned int const k) {
/*----------------------------------------------------------------------------
   This is the probability mass function (PMF) of a discrete random variable
   with lambda 'lambda'.

   I.e. it gives the probability that a value sampled from a Poisson
   distribution with lambda 'lambda' has the value 'k'.

   That means it's the probability that in a Poisson stream of events in which
   the mean number of events in an interval of a certains size is 'lambda' that
   'k' events happen.
-----------------------------------------------------------------------------*/
    double x;
    unsigned int i;

    /* We're computing the formula

         (pow(lamda, k) * exp(-lambda)) / fact(k).

       Note that k is ordinarily quite small.
    */

    x = exp(-lambda);

    for (i = 1; i <= k; ++i) {
        x *= lambda;
        x /= i;
    }
    return x;
}



static void
addPoissonNoise(struct pam *       const pamP,
                sample             const origSample,
                sample *           const newSampleP,
                float              const lambdaOfMaxval,
                struct pm_randSt * const randStP) {
/*----------------------------------------------------------------------------
   Add Poisson noise
-----------------------------------------------------------------------------*/
    samplen const origSamplen = pnm_normalized_sample(pamP, origSample);

    float const origSampleIntensity = pm_ungamma709(origSamplen);

    double const lambda  = origSampleIntensity * lambdaOfMaxval;

    double const u = pm_drand(randStP);

    /* We now apply the inverse CDF (cumulative distribution function) of the
       Poisson distribution to uniform random variable 'u' to get a Poisson
       random variable.  Unfortunately, we have no algebraic equation for the
       inverse of the CDF, but the random variable is discrete, so we can just
       iterate.
    */

    unsigned int k;
    double cumProb;

    for (k = 0, cumProb = 0.0; k < lambdaOfMaxval; ++k) {

        cumProb += poissonPmf(lambda, k);

        if (cumProb >= u)
            break;
    }

    {
        samplen const newSamplen = pm_gamma709(k/lambdaOfMaxval);

        *newSampleP = pnm_unnormalized_sample(pamP, newSamplen);
    }
}



int
main(int argc, const char ** argv) {

    FILE * ifP;
    struct CmdlineInfo cmdline;
    struct pam inpam;
    struct pam outpam;
    tuple * tuplerow;
    const tuple * newtuplerow;
    unsigned int row;
    double infinity;
    struct pm_randSt randSt;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    pm_randinit(&randSt);
    pm_srand2(&randSt, cmdline.seedSpec, cmdline.seed);

    ifP = pm_openr(cmdline.inputFileName);

    pnm_readpaminit(ifP, &inpam, PAM_STRUCT_SIZE(tuple_type));

    outpam = inpam;
    outpam.file = stdout;

    pnm_writepaminit(&outpam);

    tuplerow    = pnm_allocpamrow(&inpam);
    newtuplerow = pnm_allocpamrow(&inpam);

    infinity = (double) inpam.maxval;

    for (row = 0; row < inpam.height; ++row) {
        unsigned int col;
        pnm_readpamrow(&inpam, tuplerow);
        for (col = 0; col < inpam.width; ++col) {
            unsigned int plane;
            for (plane = 0; plane < inpam.depth; ++plane) {
                switch (cmdline.noiseType) {
                case NOISETYPE_GAUSSIAN:
                    addGaussianNoise(inpam.maxval,
                                     tuplerow[col][plane],
                                     &newtuplerow[col][plane],
                                     cmdline.sigma1, cmdline.sigma2,
                                     &randSt);
                    break;

                case NOISETYPE_IMPULSE:
                    addImpulseNoise(inpam.maxval,
                                    tuplerow[col][plane],
                                    &newtuplerow[col][plane],
                                    cmdline.tolerance, cmdline.saltRatio,
                                    &randSt);
                   break;

                case NOISETYPE_LAPLACIAN:
                    addLaplacianNoise(inpam.maxval, infinity,
                                      tuplerow[col][plane],
                                      &newtuplerow[col][plane],
                                      cmdline.lsigma,
                                      &randSt);
                    break;

                case NOISETYPE_MULTIPLICATIVE_GAUSSIAN:
                    addMultiplicativeGaussianNoise(inpam.maxval, infinity,
                                                   tuplerow[col][plane],
                                                   &newtuplerow[col][plane],
                                                   cmdline.mgsigma,
                                                   &randSt);
                    break;

                case NOISETYPE_POISSON:
                    addPoissonNoise(&inpam,
                                    tuplerow[col][plane],
                                    &newtuplerow[col][plane],
                                    cmdline.lambda,
                                    &randSt);
                    break;

                }
            }
        }
        pnm_writepamrow(&outpam, newtuplerow);
    }
    pm_randterm(&randSt);
    pnm_freepamrow(newtuplerow);
    pnm_freepamrow(tuplerow);

    return 0;
}



