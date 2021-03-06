#include "netpbm/rand.h"

static void
vinit(struct pm_randSt * const randStP) {

    randStP->max    = RAND_MAX;
    randStP->stateP = NULL;
}



static void
vsrand(struct pm_randSt * const randStP,
       unsigned int       const seed) {

    srandom(seed);
}



static unsigned long int
vrand(struct pm_randSt * const randStP) {

    return random();
}


struct pm_rand_vtable const pm_randsysrandom_vtable = {
    &vinit,
    &vsrand,
    &vrand
};


