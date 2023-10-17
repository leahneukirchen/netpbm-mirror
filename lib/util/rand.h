/* Interface header file for random number generator functions in libnetpbm */

#ifndef RAND_H_INCLUDED
#define RAND_H_INCLUDED

#include <inttypes.h>

#include "netpbm/pm_c_util.h"
#include "netpbm/mallocvar.h"

/*
  Definitions for selecting the random number generator

  The default random number generator is Mersenne Twister.  Here we
  provide a means to revert to the system rand() generator if need be.

  Glibc provides generators: rand(), random() and drand48().  Each has
  its own associated functions.  In the Glibc documentation rand() is
  called "ISO", random() is called "BSD" and drand48() is called "SVID".
  If your system has the glibc documentation installed "info rand" should
  get you to the relevant page.  The documentation is available online
  from:

  https://www.gnu.org/software/libc/manual/html_node/Pseudo_002dRandom-Numbers.html
  Pseudo-Random Numbers (The GNU C Library)

  Glibc's choice of name is confusing for what it calls "ISO" rand()
  was available in early BSD systems.

  Functions by these names appear on most Unix systems, but generation
  formulas and default initial states are known to differ.  On systems
  which do not use glibc, what is called rand() may have no relation
  with the formula of the ISO C standard.  Likewise random() may have
  no relation with the BSD formula.
*/

enum PmRandEngine {PM_RAND_SYS_RAND,         /* rand()   */
                   PM_RAND_SYS_RANDOM,       /* random() */
                   PM_RAND_SYS_DRAND48,      /* drand48()  reserved */
                   PM_RAND_MERSENNETWISTER   /* default */};

#ifndef PM_RANDOM_NUMBER_GENERATOR
  #define PM_RANDOM_NUMBER_GENERATOR PM_RAND_MERSENNETWISTER
#endif


/* Structure to hold random number generator profile and internal state */

struct pm_randSt;

struct pm_rand_vtable {
    void
    (*init)(struct pm_randSt * const randStP);

    void
    (*srand)(struct pm_randSt * const randStP,
              unsigned int       const seed);

    unsigned long int
    (*rand)(struct pm_randSt * const randStP);
};

extern struct pm_rand_vtable const pm_randsysrand_vtable;
extern struct pm_rand_vtable const pm_randsysrandom_vtable;
extern struct pm_rand_vtable const pm_randmersenne_vtable;

struct pm_randSt {
    struct pm_rand_vtable vtable;
    void *                stateP;  /* Internal state */
    unsigned int          max;
    unsigned int          seed;
    bool                  gaussCacheValid;
    double                gaussCache;
};

/* Function declarations */

extern void
pm_randinit(struct pm_randSt * const randStP);

extern void
pm_randterm(struct pm_randSt * const randStP);

extern void
pm_srand(struct pm_randSt * const randStP,
         unsigned int       const seed);


extern void
pm_srand2(struct pm_randSt * const randStP,
          bool               const withSeed,
          unsigned int       const seedVal);

extern unsigned long int
pm_rand(struct pm_randSt * const randStP);

extern double
pm_drand(struct pm_randSt * const randStP);

extern double
pm_drand1(struct pm_randSt * const randStP);

extern double
pm_drand2(struct pm_randSt * const randStP);

extern void
pm_gaussrand2(struct pm_randSt * const randStP,
              double *           const r1P,
              double *           const r2P);

extern double
pm_gaussrand(struct pm_randSt * const randStP);

extern uint32_t
pm_rand32(struct pm_randSt * const randStP);


#endif
