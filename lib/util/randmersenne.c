#include "netpbm/pm.h"
#include "netpbm/rand.h"

/* +++++ Start of Mersenne Twister pseudorandom number generator code +++++ */

/*
   Original source code from:
   http://www.math.sci.hiroshima-u.ac.jp/m-mat/MT/VERSIONS/C-LANG/c-lang.html

   A C-program for MT19937, with initialization improved 2002/1/26.
   Coded by Takuji Nishimura and Makoto Matsumoto.

   Copyright (C) 1997 - 2002, Makoto Matsumoto and Takuji Nishimura,
   All rights reserved.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

     1. Redistributions of source code must retain the above copyright
        notice, this list of conditions and the following disclaimer.

     2. Redistributions in binary form must reproduce the above copyright
        notice, this list of conditions and the following disclaimer in the
        documentation and/or other materials provided with the distribution.

     3. The names of its contributors may not be used to endorse or promote
        products derived from this software without specific prior written
        permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT
   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   Any feedback is very welcome.
   http://www.math.sci.hiroshima-u.ac.jp/~m-mat/MT/emt.html
   email: m-mat @ math.sci.hiroshima-u.ac.jp (remove space)

   Above conditions apply in the following code to the line which says:
   +++++ End of Mersenne Twister pseudorandom number generator code +++++
*/

/* Period parameters */

#define MT_N 624
#define MT_M 397
#define MT_MATRIX_A 0x9908b0dfUL   /* constant vector a */

struct MtState {
    uint32_t mt[MT_N]; /* the array for the state vector  */
    unsigned int mtIndex;
};



static void
randMtAlloc(struct MtState ** const statePP) {

    struct MtState * stateP;

    MALLOCVAR_NOFAIL(stateP);

    *statePP = stateP;
}



/* 32 bit masks */

static uint32_t const FMASK = 0xffffffffUL; /* all bits */
static uint32_t const UMASK = 0x80000000UL; /* most significant bit */
static uint32_t const LMASK = 0x7fffffffUL; /* least significant 31 bits */



static void
srandMt(struct MtState * const stateP,
        unsigned int     const seed) {
/*-----------------------------------------------------------------------------
  Initialize state array mt[MT_N] with seed
-----------------------------------------------------------------------------*/
    unsigned int mtIndex;
    uint32_t * const mt = stateP->mt;

    mt[0]= seed & FMASK;

    for (mtIndex = 1; mtIndex < MT_N; ++mtIndex) {
        mt[mtIndex] = (1812433253UL * (mt[mtIndex-1]
                       ^ (mt[mtIndex-1] >> 30)) + mtIndex);

        /* See Knuth TAOCP Vol2. 3rd Ed. P.106 for multiplier. */
    }

    stateP->mtIndex = mtIndex;
}



static unsigned long int
randMt32(struct MtState * const stateP) {
/*----------------------------------------------------------------------------
  Generate a 32 bit random number   interval: [0, 0xffffffff]
  ----------------------------------------------------------------------------*/
    unsigned int mtIndex;
    uint32_t retval;

    if (stateP->mtIndex >= MT_N) {
        /* generate N words at one time */
        uint32_t * const mt = stateP->mt;
        uint32_t const mag01[2]={0x0UL, MT_MATRIX_A};
        /* mag01[x] = x * MT_MATRIX_A  for x=0, 1 */

        int k;
        uint32_t y;

        if (stateP->mtIndex >= MT_N+1) {
            pm_error("Internal error in Mersenne Twister random number"
                     "generator");
        }

        for (k = 0; k < MT_N-MT_M; ++k) {
            y = (mt[k] & UMASK) | (mt[k+1] & LMASK);
            mt[k] = mt[k + MT_M] ^ (y >> 1) ^ mag01[y & 0x1UL];
        }
        for (; k < MT_N-1; ++k) {
            y = (mt[k] & UMASK) | (mt[k+1] & LMASK);
            mt[k] = mt[k+(MT_M-MT_N)] ^ (y >> 1) ^ mag01[y & 0x1UL];
        }
        y = (mt[MT_N - 1] & UMASK) | (mt[0] & LMASK);
        mt[MT_N - 1] = mt[MT_M - 1] ^ (y >> 1) ^ mag01[y & 0x1UL];

        mtIndex = 0;
    } else
        mtIndex = stateP->mtIndex;

    retval = stateP->mt[mtIndex];

    /* Tempering */
    retval ^= (retval >> 11);
    retval ^= (retval <<  7) & 0x9d2c5680UL;
    retval ^= (retval << 15) & 0xefc60000UL;
    retval ^= (retval >> 18);

    stateP->mtIndex = mtIndex + 1;

    return retval;
}

/* +++++ End of Mersenne Twister pseudorandom number generator code +++++ */


static void
vinit(struct pm_randSt * const randStP) {

    randMtAlloc((struct MtState ** const) &randStP->stateP);
    randStP->max    = 0xffffffffUL;
}



static void
vsrand(struct pm_randSt * const randStP,
       unsigned int       const seed) {

    srandMt(randStP->stateP, seed);
}



static unsigned long int
vrand(struct pm_randSt * const randStP) {

    return randMt32(randStP->stateP);
}



struct pm_rand_vtable const pm_randmersenne_vtable = {
    &vinit,
    &vsrand,
    &vrand
};


