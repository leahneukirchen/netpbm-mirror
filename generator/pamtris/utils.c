#include <stdlib.h>

#include "common.h"

void
step_up(fract *       vars,
        const fract * steps,
        uint8_t       elements,
        int32_t       div) {

    unsigned int i;

    for (i = 0; i < elements; ++i) {
        uint32_t negative_mask = -steps[i].negative_flag;
        vars[i].q += steps[i].q;
        vars[i].r += steps[i].r;

        {
            uint32_t overdiv_mask = -(((uint32_t)~(vars[i].r - div)) >> 31);
                /*  = ~0 if var->r >= div; 0 otherwise. */

            vars[i].q += (negative_mask | 1) & overdiv_mask;
                /* = (-1 if the step is negative; 1 otherwise) &'ed with
                   overdiv_mask.  vars[i].r -= div & overdiv_mask;
                */
        }
    }
}



void
multi_step_up(fract *       vars,
              const fract * steps,
              uint8_t       elements,
              int32_t       times,
              int32_t       div) {

    unsigned int i;

    for (i = 0; i < elements; i++) {
        uint32_t negative_mask = -steps[i].negative_flag;

        vars[i].q += times * steps[i].q;
        vars[i].r += times * steps[i].r;

        if(vars[i].r >= div && div != 0) {
            int32_t r_q = vars[i].r / div;
            int32_t r_r = vars[i].r % div;

            vars[i].q += (-r_q & negative_mask) | (r_q & ~negative_mask);
                /* = -r_q if the step is negative; r_q, otherwise. */
            vars[i].r = r_r;
        }
    }
}



void
gen_steps(const int32_t * begin,
          const int32_t * end,
          fract         * out, uint8_t elements, int32_t div) {

    if (div > 1) {
        unsigned int i;

        for (i = 0; i < elements; i++) {
            int32_t delta = end[i] - begin[i];

            out[i].q = delta / div;
            out[i].r = abs(delta % div);
            out[i].negative_flag = ((uint32_t)delta) >> 31;
        }
    } else {
        unsigned int i;

        for (i = 0; i < elements; i++) {
            int32_t delta = end[i] - begin[i];

            out[i].q = delta;
            out[i].r = 0;
            out[i].negative_flag = ((uint32_t)delta) >> 31;
        }
    }
}



void
fract_to_int32_array(const fract * in,
                     int32_t *     out,
                     uint8_t       elements) {

    unsigned int i;

    for (i = 0; i < elements; i++) {
        out[i] = in[i].q;
    }
}



void
int32_to_fract_array(const int32_t * in,
                     fract *         out,
                     uint8_t         elements) {

    unsigned int i;

    for (i = 0; i < elements; i++) {
        out[i].q = in[i];
        out[i].r = 0;
    }
}



static void
swap(uint8_t * a,
     uint8_t * b) {
/*----------------------------------------------------------------------------
  Swap the contents pointed to by a and b.
-----------------------------------------------------------------------------*/
    uint8_t temp = *a;
    *a = *b;
    *b = temp;
}



void
sort3(uint8_t *       index_array,
      const int32_t * y_array,
      const int32_t * x_array) {

    uint8_t * ia = index_array;
    const int32_t * ya = y_array;
    const int32_t * xa = x_array;

    if (ya[0] == ya[1] && ya[1] == ya[2]) {
        /* In case the vertices represent a degenerate horizontal triangle, we
           sort according to relative X coordinate, as opposed to Y.
        */
        ya = xa;
    }

    if (ya[ia[2]] < ya[ia[1]]) {
        swap(ia, ia + 2);
        if (ya[ia[2]] < ya[ia[1]]) {
            swap(ia + 1, ia + 2);
            if (ya[ia[1]] < ya[ia[0]]) {
                swap(ia, ia + 1);
            }
        }
    } else if (ya[ia[1]] < ya[ia[0]]) {
        swap(ia, ia + 1);
        if (ya[ia[2]] < ya[ia[1]]) {
            swap(ia + 1, ia + 2);
        }
    }

    if (ya == xa) {
        return;
    }

    if (ya[ia[0]] == ya[ia[1]]) {
        if (xa[ia[1]] < xa[ia[0]]) {
            swap(ia, ia + 1);
        }
    } else if (ya[ia[1]] == ya[ia[2]]) {
        if (xa[ia[2]] < xa[ia[1]]) {
            swap(ia + 1, ia + 2);
        }
    }
}


