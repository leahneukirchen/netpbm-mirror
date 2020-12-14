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
#include "shhopt.h"
#include "pm_gamma.h"
#include "pam.h"

static double const EPSILON = 1.0e-5;



static double
rand1() {

    return (double)rand()/RAND_MAX;
}



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

    unsigned int seed;

    float lambda;
    float lsigma;
    float mgsigma;
    float sigma1;
    float sigma2;
    float tolerance;
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

    unsigned int typeSpec, seedSpec, lambdaSpec, lsigmaSpec, mgsigmaSpec,
        sigma1Spec, sigma2Spec, toleranceSpec;

    const char * type;

    MALLOCARRAY(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0,   "type",            OPT_STRING,   &type,
            &typeSpec,         0);
    OPTENT3(0,   "seed",            OPT_UINT,     &cmdlineP->seed,
            &seedSpec,         0);
    OPTENT3(0,   "lambda",          OPT_FLOAT,    &cmdlineP->lambda,
            &lambdaSpec,       0);
    OPTENT3(0,   "lsigma",          OPT_FLOAT,    &cmdlineP->lsigma,
            &lsigmaSpec,       0);
    OPTENT3(0,   "mgsigma",         OPT_FLOAT,    &cmdlineP->mgsigma,
            &mgsigmaSpec,      0);
    OPTENT3(0,   "sigma1",          OPT_FLOAT,    &cmdlineP->sigma1,
            &sigma1Spec,       0);
    OPTENT3(0,   "sigma2",          OPT_FLOAT,    &cmdlineP->sigma2,
            &sigma2Spec,       0);
    OPTENT3(0,   "tolerance",       OPT_FLOAT,    &cmdlineP->tolerance,
            &toleranceSpec,    0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We have no parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (!typeSpec)
        cmdlineP->noiseType = NOISETYPE_GAUSSIAN;
    else
        cmdlineP->noiseType = typeFmName(type);

    if (sigma1Spec && cmdlineP->noiseType != NOISETYPE_GAUSSIAN)
        pm_error("-sigma1 is valid only with -type=gaussian");

    if (sigma2Spec && cmdlineP->noiseType != NOISETYPE_GAUSSIAN)
        pm_error("-sigma2 is valid only with -type=gaussian");

    if (mgsigmaSpec &&
        cmdlineP->noiseType != NOISETYPE_MULTIPLICATIVE_GAUSSIAN)
        pm_error("-mgsigma is valid only with -type=multiplicative_guassian");

    if (toleranceSpec && cmdlineP->noiseType != NOISETYPE_IMPULSE)
        pm_error("-tolerance is valid only with -type=impulse");

    if (lsigmaSpec && cmdlineP->noiseType != NOISETYPE_LAPLACIAN)
        pm_error("-lsigma is valid only with -type=laplacian");

    if (lambdaSpec && cmdlineP->noiseType != NOISETYPE_POISSON)
        pm_error("-lambda is valid only with -type=poisson");

    if (!lambdaSpec)
        cmdlineP->lambda = 12.0;

    if (!lsigmaSpec)
        cmdlineP->lsigma = 10.0;

    if (!mgsigmaSpec)
        cmdlineP->mgsigma = 0.5;

    if (!sigma1Spec)
        cmdlineP->sigma1 = 4.0;

    if (!sigma2Spec)
        cmdlineP->sigma2 = 20.0;

    if (!toleranceSpec)
        cmdlineP->tolerance = 0.10;

    if (!seedSpec)
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
addGaussianNoise(sample   const maxval,
                 sample   const origSample,
                 sample * const newSampleP,
                 float    const sigma1,
                 float    const sigma2) {
/*----------------------------------------------------------------------------
   Add Gaussian noise.

   Based on Kasturi/Algorithms of the ACM
-----------------------------------------------------------------------------*/

    double x1, x2, xn, yn;
    double rawNewSample;

    x1 = rand1();

    if (x1 == 0.0)
        x1 = 1.0;
    x2 = rand1();
    xn = sqrt(-2.0 * log(x1)) * cos(2.0 * M_PI * x2);
    yn = sqrt(-2.0 * log(x1)) * sin(2.0 * M_PI * x2);

    rawNewSample =
        origSample + (sqrt((double) origSample) * sigma1 * xn) + (sigma2 * yn);

    *newSampleP = MAX(MIN((int)rawNewSample, maxval), 0);
}



static void
addImpulseNoise(sample   const maxval,
                sample   const origSample,
                sample * const newSampleP,
                float    const tolerance) {
/*----------------------------------------------------------------------------
   Add impulse (salt and pepper) noise
-----------------------------------------------------------------------------*/

    double const low_tol  = tolerance / 2.0;
    double const high_tol = 1.0 - (tolerance / 2.0);
    double const sap = rand1();

    if (sap < low_tol)
        *newSampleP = 0;
    else if ( sap >= high_tol )
        *newSampleP = maxval;
    else
        *newSampleP = origSample;
}



static void
addLaplacianNoise(sample   const maxval,
                  double   const infinity,
                  sample   const origSample,
                  sample * const newSampleP,
                  float    const lsigma) {
/*----------------------------------------------------------------------------
   Add Laplacian noise

   From Pitas' book.
-----------------------------------------------------------------------------*/
    double const u = rand1();

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
addMultiplicativeGaussianNoise(sample   const maxval,
                               double   const infinity,
                               sample   const origSample,
                               sample * const newSampleP,
                               float    const mgsigma) {
/*----------------------------------------------------------------------------
   Add multiplicative Gaussian noise

   From Pitas' book.
-----------------------------------------------------------------------------*/
    double rayleigh, gauss;
    double rawNewSample;

    {
        double const uniform = rand1();
        if (uniform <= EPSILON)
            rayleigh = infinity;
        else
            rayleigh = sqrt(-2.0 * log( uniform));
    }
    {
        double const uniform = rand1();
        gauss = rayleigh * cos(2.0 * M_PI * uniform);
    }
    rawNewSample = origSample + (origSample * mgsigma * gauss);

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
addPoissonNoise(struct pam * const pamP,
                sample       const origSample,
                sample *     const newSampleP,
                float        const lambdaOfMaxval) {
/*----------------------------------------------------------------------------
   Add Poisson noise
-----------------------------------------------------------------------------*/
    samplen const origSamplen = pnm_normalized_sample(pamP, origSample);

    float const origSampleIntensity = pm_ungamma709(origSamplen);

    double const lambda  = origSampleIntensity * lambdaOfMaxval;

    double const u = rand1();

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

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    srand(cmdline.seed);

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
                                     cmdline.sigma1, cmdline.sigma2);
                    break;

                case NOISETYPE_IMPULSE:
                    addImpulseNoise(inpam.maxval,
                                    tuplerow[col][plane],
                                    &newtuplerow[col][plane],
                                    cmdline.tolerance);
                   break;

                case NOISETYPE_LAPLACIAN:
                    addLaplacianNoise(inpam.maxval, infinity,
                                      tuplerow[col][plane],
                                      &newtuplerow[col][plane],
                                      cmdline.lsigma);
                    break;

                case NOISETYPE_MULTIPLICATIVE_GAUSSIAN:
                    addMultiplicativeGaussianNoise(inpam.maxval, infinity,
                                                   tuplerow[col][plane],
                                                   &newtuplerow[col][plane],
                                                   cmdline.mgsigma);
                    break;

                case NOISETYPE_POISSON:
                    addPoissonNoise(&inpam,
                                    tuplerow[col][plane],
                                    &newtuplerow[col][plane],
                                    cmdline.lambda);
                    break;

                }
            }
        }
        pnm_writepamrow(&outpam, newtuplerow);
    }
    pnm_freepamrow(newtuplerow);
    pnm_freepamrow(tuplerow);

    return 0;
}



