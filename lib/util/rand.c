/*

Pseudo-random number generator for Netpbm

The interface provided herein should be flexible enough for anybody
who wishes to use some other random number generator.

---

If you desire to implement a different generator, or writing an original
one, first take a look at the random number generator section of the
GNU Scientific Library package (GSL).

GNU Scientific Library
https://www.gnu.org/software/gsl/

GSL Random Number Generators
https://wnww.gnu.org/software/gsl/doc/html/rng.html

*/

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <strings.h>
#include <time.h>
#include <float.h>
#include <math.h>

#include "netpbm/pm_c_util.h"
#include "netpbm/mallocvar.h"
#include "netpbm/pm.h"
#include "netpbm/rand.h"

/*-----------------------------------------------------------------------------
                              Use
-------------------------------------------------------------------------------
  Typical usage:

      #include "rand.h"

      ...

      myfunction( ... , unsigned int const seed , ... ) {

          struct randSt;

          ...

          pm_randinit(&randSt);
          pm_srand(&randSt, seed);  // pm_srand2() is often more useful

          ...

          pm_rand(&randSt);

          ...

          pm_randterm(&randSt);

      }
-----------------------------------------------------------------------------*/



/*-----------------------------------------------------------------------------
                            Design note
-------------------------------------------------------------------------------

  Netpbm code contains multiple instances where random numbers are used.
  Stock Netpbm always uses an internal pseudo-random number generator
  that implements the Mersenne Twister method and does not rely on any
  randomness facility of the operating system, but it is easy to compile
  an alternative version that uses the operating system function, or some
  other generator.

  The Mersenne Twister method was new to Netpbm in Netpbm 10.94 (March 2021).
  Before that, Netpbm used standard OS-provided facilities.

  Programs that use random numbers have existed in Netpbm since PBMPlus days.
  The system rand() function was used where randomness was required;
  exceptions were rare and all of them appear to be errors on the part of the
  original author.

  Although the rand() function is available in every system on which Netpbm
  runs, differences exist in the underlying algorithm, so that Netpbm programs
  produced different output on different systems even when the user specified
  the same random number seed.

  This was not considered a problem in the early days.  Deterministic
  operation was not a feature users requested and it was impossible regardless
  of the random number generation method on most programs because they did not
  allow a user to specify a seed for the generator.

  This state of affairs changed as Netpbm got firmly established as a
  base-level system package.  Security became critical for many users.  A
  crucial component of quality control is automated regression tests (="make
  check").  Unpredictable behavior gets in the way of testing.  One by one
  programs were given the -randomseed (or -seed) option to ensure reproducible
  results.  Often this was done as new test cases were written.  However,
  inconsistent output caused by system-level differences in rand()
  implementation remained a major obstacle.

  In 2020 the decision was made to replace all calls to rand() in the Netpbm
  source code with an internal random number generator.  We decided to use the
  Mersenne Twister, which is concise, enjoys a fine reputation and is
  available under liberal conditions (see below.)
-----------------------------------------------------------------------------*/


void
pm_srand(struct pm_randSt * const randStP,
         unsigned int       const seed) {
/*----------------------------------------------------------------------------
  Initialize (or "seed") the random number generation sequence with value
  'seed'.
-----------------------------------------------------------------------------*/
    pm_randinit(randStP);

    randStP->vtable.srand(randStP, seed);

    randStP->seed = seed;
}



void
pm_srand2(struct pm_randSt * const randStP,
          bool               const seedValid,
          unsigned int       const seed) {
/*----------------------------------------------------------------------------
  Seed the random number generator.  If 'seedValid' is true, use 'seed'.
  Otherwise, use pm_randseed().

  For historical reasons pm_randseed() is defined in libpm.c rather than
  this source file.
-----------------------------------------------------------------------------*/
    pm_srand(randStP, seedValid ? seed : pm_randseed() );

}



unsigned long int
pm_rand(struct pm_randSt * const randStP) {
/*----------------------------------------------------------------------------
  An integer random number in the interval [0, randStP->max].
-----------------------------------------------------------------------------*/
    return randStP->vtable.rand(randStP);
}



double
pm_drand(struct pm_randSt * const randStP) {
/*----------------------------------------------------------------------------
  A floating point random number in the interval [0, 1].

  Although the return value is declared as double, the actual value will have
  no more precision than a single call to pm_rand() provides.  This is 32 bits
  for Mersenne Twister.
-----------------------------------------------------------------------------*/
    return (double) pm_rand(randStP) / randStP->max;
}



double
pm_drand1(struct pm_randSt * const randStP) {
/*----------------------------------------------------------------------------
  A floating point random number in the interval [0, 1).
-----------------------------------------------------------------------------*/
    return (double) pm_rand(randStP) / ((double) randStP->max + 1.0);
}



double
pm_drand2(struct pm_randSt * const randStP) {
/*----------------------------------------------------------------------------
  A floating point random number in the interval (0, 1).
-----------------------------------------------------------------------------*/
    return ((double) pm_rand(randStP) + 0.5) / ((double) randStP->max + 1.0);
}



void
pm_gaussrand2(struct pm_randSt * const randStP,
              double *           const r1P,
              double *           const r2P) {
/*----------------------------------------------------------------------------
  Generate two Gaussian (or normally) distributed random numbers *r1P and
  *r2P.

  Mean = 0, Standard deviation = 1.

  This is called the Box-Muller method.

  For details of this algorithm and other methods for producing
  Gaussian random numbers see:

  http://www.doc.ic.ac.uk/~wl/papers/07/csur07dt.pdf
-----------------------------------------------------------------------------*/
    double u1, u2;

    u1 = pm_drand2(randStP);
    u2 = pm_drand1(randStP);

    *r1P = sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
    *r2P = sqrt(-2.0 * log(u1)) * sin(2.0 * M_PI * u2);
}



double
pm_gaussrand(struct pm_randSt * const randStP) {
/*----------------------------------------------------------------------------
  A Gaussian (or normally) distributed random number.

  Mean = 0, Standard deviation = 1.

  If a randStP->gaussCache has a value, return that value.  Otherwise call
  pm_gaussrand2; return one generated value, remember the other.
-----------------------------------------------------------------------------*/
    double retval;

    if (!randStP->gaussCacheValid) {
        pm_gaussrand2(randStP, &retval, &randStP->gaussCache);
        randStP->gaussCacheValid = true;
    } else {
        retval = randStP->gaussCache;
        randStP->gaussCacheValid = false;
    }

    return retval;
}



uint32_t
pm_rand32(struct pm_randSt * const randStP) {
/*-----------------------------------------------------------------------------
  Generate a 32-bit random number.
-----------------------------------------------------------------------------*/
    unsigned int const randMax = randStP->max;

    /* 'randMax is a power of 2 minus 1.  Function pm_randinit() rejects
       generators which do not satisfy this condition.  It is unlikely that
       such odd generators actually exist.

       Many system generators are known to return 31 bits (max = 2147483647 or
       0x7FFFFFFF).  Historically, there were generators that returned only 15
       bits.
    */

    uint32_t retval;

    if (randMax >= 0xFFFFFFFF)
        retval = pm_rand(randStP);
    else {
        uint32_t scale;

        retval = 0;  /* Initial value */

        for (scale = 0xFFFFFFFF; scale > 0; scale /= (randMax +1))
            retval = retval * (randMax + 1) + pm_rand(randStP);
    }

    return retval;
}



void
pm_randinit(struct pm_randSt * const randStP) {
/*----------------------------------------------------------------------------
  Initialize the random number generator.
-----------------------------------------------------------------------------*/
    switch (PM_RANDOM_NUMBER_GENERATOR) {
    case PM_RAND_SYS_RAND:
        randStP->vtable = pm_randsysrand_vtable;
        break;
    case PM_RAND_SYS_RANDOM:
        randStP->vtable = pm_randsysrandom_vtable;
        break;
    case PM_RAND_MERSENNETWISTER:
        randStP->vtable = pm_randmersenne_vtable;
        break;
    default:
        pm_error("INTERNAL ERROR: Invalid value of "
                 "PM_RANDOM_NUMBER_GENERATOR (random number generator "
                 "engine type): %u", PM_RANDOM_NUMBER_GENERATOR);
    }

    randStP->vtable.init(randStP);

    if (randStP->max == 0)
        pm_error("Random number generator maximum value must be positive.");
    else if (((long int) randStP->max & (long int) (randStP->max + 1)) != 0x0L)
        pm_error("Non-standard random number generator with maximum value "
                 "%u.  Cannot handle maximum values which are not powers "
                 "of 2 minus 1", randStP->max);

    randStP->gaussCacheValid = false;
}



void
pm_randterm(struct pm_randSt * const randStP) {
/*----------------------------------------------------------------------------
  Tear down the random number generator.
-----------------------------------------------------------------------------*/
     if (randStP->stateP)
         free(randStP->stateP);
}



