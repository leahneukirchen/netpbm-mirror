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

#include <math.h>

#include "pm_c_util.h"
#include "pam.h"

static double const EPSILON = 1.0e-5;



static double
rand1() {

    return (double)rand()/RAND_MAX;
}



enum noiseType {
    GAUSSIAN,
    IMPULSE,  /* aka salt and pepper noise */
    LAPLACIAN,
    MULTIPLICATIVE_GAUSSIAN,
    POISSON,
    MAX_NOISE_TYPES
};



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

    double const lambda  = origSamplen * lambdaOfMaxval;

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

    *newSampleP = pnm_unnormalized_sample(pamP, k/lambdaOfMaxval);
}



int
main(int argc, char * argv[]) {

    FILE * ifP;
    struct pam inpam;
    struct pam outpam;
    tuple * tuplerow;
    const tuple * newtuplerow;
    unsigned int row;
    double infinity;

    int argn;
    const char * inputFilename;
    int noise_type;
    unsigned int seed;
    int i;
    const char * const usage = "[-type noise_type] [-lsigma x] [-mgsigma x] "
        "[-sigma1 x] [-sigma2 x] [-lambda x] [-seed n] "
        "[-tolerance ratio] [pgmfile]";

    const char * const noise_name[] = {
        "gaussian",
        "impulse",
        "laplacian",
        "multiplicative_gaussian",
        "poisson"
    };
    int const noise_id[] = {
        GAUSSIAN,
        IMPULSE,
        LAPLACIAN,
        MULTIPLICATIVE_GAUSSIAN,
        POISSON
    };
    /* minimum number of characters to match noise name for pm_keymatch() */
    int const noise_compare[] = {
        1,
        1,
        1,
        1,
        1
    };

    /* define default values for configurable options */
    float lambda = 12.0;
    float lsigma = 10.0;
    float mgsigma = 0.5;
    float sigma1 = 4.0;
    float sigma2 = 20.0;
    float tolerance = 0.10;

    pnm_init(&argc, argv);

    seed = pm_randseed();
    noise_type = GAUSSIAN;

    argn = 1;
    while ( argn < argc && argv[argn][0] == '-' && argv[argn][1] != '\0' )
    {
        if ( pm_keymatch( argv[argn], "-lambda", 3 ) )
        {
            ++argn;
            if ( argn >= argc )
            {
                pm_message(
                    "incorrect number of arguments for -lambda option" );
                pm_usage( usage );
            }
            else if ( argv[argn][0] == '-' )
            {
                pm_message( "invalid argument to -lambda option: %s",
                            argv[argn] );
                pm_usage( usage );
            }
            lambda = atof( argv[argn] );
        }
        else if ( pm_keymatch( argv[argn], "-lsigma", 3 ) )
        {
            ++argn;
            if ( argn >= argc )
            {
                pm_message(
                    "incorrect number of arguments for -lsigma option" );
                pm_usage( usage );
            }
            else if ( argv[argn][0] == '-' )
            {
                pm_message( "invalid argument to -lsigma option: %s",
                            argv[argn] );
                pm_usage( usage );
            }
            lsigma = atof( argv[argn] );
        }
        else if ( pm_keymatch( argv[argn], "-mgsigma", 2 ) )
        {
            ++argn;
            if ( argn >= argc )
            {
                pm_message(
                    "incorrect number of arguments for -mgsigma option" );
                pm_usage( usage );
            }
            else if ( argv[argn][0] == '-' )
            {
                pm_message( "invalid argument to -mgsigma option: %s",
                            argv[argn] );
                pm_usage( usage );
            }
            mgsigma = atof( argv[argn] );
        }
        else if ( pm_keymatch( argv[argn], "-seed", 3 ) )
        {
            ++argn;
            if ( argn >= argc )
            {
                pm_message( "incorrect number of arguments for -seed option" );
                pm_usage( usage );
            }
            else if ( argv[argn][0] == '-' )
            {
                pm_message( "invalid argument to -seed option: %s",
                            argv[argn] );
                pm_usage( usage );
            }
            seed = atoi(argv[argn]);
        }
        else if ( pm_keymatch( argv[argn], "-sigma1", 7 ) ||
                  pm_keymatch( argv[argn], "-s1", 3 ) )
        {
            ++argn;
            if ( argn >= argc )
            {
                pm_message(
                    "incorrect number of arguments for -sigma1 option" );
                pm_usage( usage );
            }
            else if ( argv[argn][0] == '-' )
            {
                pm_message( "invalid argument to -sigma1 option: %s",
                            argv[argn] );
                pm_usage( usage );
            }
            sigma1 = atof( argv[argn] );
        }
        else if ( pm_keymatch( argv[argn], "-sigma2", 7 ) ||
                  pm_keymatch( argv[argn], "-s2", 3 ) )
        {
            ++argn;
            if ( argn >= argc )
            {
                pm_message(
                    "incorrect number of arguments for -sigma2 option" );
                pm_usage( usage );
            }
            else if ( argv[argn][0] == '-' )
            {
                pm_message( "invalid argument to -sigma2 option: %s",
                            argv[argn] );
                pm_usage( usage );
            }
            sigma2 = atof( argv[argn] );
        }
        else if ( pm_keymatch( argv[argn], "-tolerance", 3 ) )
        {
            ++argn;
            if ( argn >= argc )
            {
                pm_message(
                    "incorrect number of arguments for -tolerance option" );
                pm_usage( usage );
            }
            else if ( argv[argn][0] == '-' )
            {
                pm_message( "invalid argument to -tolerance option: %s",
                            argv[argn] );
                pm_usage( usage );
            }
            tolerance = atof( argv[argn] );
        }
        else if ( pm_keymatch( argv[argn], "-type", 3 ) )
        {
            ++argn;
            if ( argn >= argc )
            {
                pm_message( "incorrect number of arguments for -type option" );
                pm_usage( usage );
            }
            else if ( argv[argn][0] == '-' )
            {
                pm_message( "invalid argument to -type option: %s",
                            argv[argn] );
                pm_usage( usage );
            }
            /* search through list of valid noise types and compare */
            i = 0;
            while ( ( i < MAX_NOISE_TYPES ) &&
                    !pm_keymatch( argv[argn],
                                  noise_name[i], noise_compare[i] ) )
                ++i;
            if ( i >= MAX_NOISE_TYPES )
            {
                pm_message( "invalid argument to -type option: %s",
                            argv[argn] );
                pm_usage( usage );
            }
            noise_type = noise_id[i];
        }
        else
            pm_usage( usage );
        ++argn;
    }

    if ( argn < argc )
    {
        inputFilename = argv[argn];
        argn++;
    }
    else
        inputFilename = "-";

    if ( argn != argc )
        pm_usage( usage );

    srand(seed);

    ifP = pm_openr(inputFilename);

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
                switch (noise_type) {
                case GAUSSIAN:
                    addGaussianNoise(inpam.maxval,
                                     tuplerow[col][plane],
                                     &newtuplerow[col][plane],
                                     sigma1, sigma2);
                    break;

                case IMPULSE:
                    addImpulseNoise(inpam.maxval,
                                    tuplerow[col][plane],
                                    &newtuplerow[col][plane],
                                    tolerance);
                   break;

                case LAPLACIAN:
                    addLaplacianNoise(inpam.maxval, infinity,
                                      tuplerow[col][plane],
                                      &newtuplerow[col][plane],
                                      lsigma);
                    break;

                case MULTIPLICATIVE_GAUSSIAN:
                    addMultiplicativeGaussianNoise(inpam.maxval, infinity,
                                                   tuplerow[col][plane],
                                                   &newtuplerow[col][plane],
                                                   mgsigma);
                    break;

                case POISSON:
                    addPoissonNoise(&inpam,
                                    tuplerow[col][plane],
                                    &newtuplerow[col][plane],
                                    lambda);
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



