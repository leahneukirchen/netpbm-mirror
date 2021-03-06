#include "netpbm/rand.h"

static void
vinit(struct pm_randSt * const randStP) {

    randStP->max    = RAND_MAX;
    randStP->stateP = NULL;
}



static void
vsrand(struct pm_randSt * const randStP,
       unsigned int       const seed) {

    srand(seed);
}



static unsigned long int
vrand(struct pm_randSt * const randStP) {

    return rand();
}



struct pm_rand_vtable const pm_randsysrand_vtable = {
    &vinit,
    &vsrand,
    &vrand
};


