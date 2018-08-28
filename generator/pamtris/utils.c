/*=============================================================================
                              utils.c
===============================================================================
   Utility functions
=============================================================================*/

#include <stdlib.h>
#include <stdint.h>

#include "fract.h"

#include "utils.h"



void
step_up(fract *       const vars,
        const fract * const steps,
        uint8_t       const elements,
        int32_t       const div) {
/*----------------------------------------------------------------------------
  Apply interpolation steps (see above) to a collection of fract
  variables (also see above) once. This is done by adding the
  quotient of each step to the quotient of the corresponding variable
  and the remainder of that step to the remainder of the variable. If the
  remainder of the variable becomes equal to or larger than the
  divisor, we increment the quotient of the variable if the negetive_flag
  of the step is false, or decrement it if the negetive_flag is true, and
  subtract the divisor from the remainder of the variable (in both cases).

  It *is* safe to pass a 0 divisor to this function.
-----------------------------------------------------------------------------*/
    unsigned int i;

    for (i = 0; i < elements; ++i) {
        uint32_t const negative_mask = -steps[i].negative_flag;

        vars[i].q += steps[i].q;
        vars[i].r += steps[i].r;

        {
            uint32_t const overdiv_mask =
                -(((uint32_t)~(vars[i].r - div)) >> 31);
                /*  = ~0 if var->r >= div; 0 otherwise. */

            vars[i].q += (negative_mask | 1) & overdiv_mask;
                /* = (-1 if the step is negative; 1 otherwise) &'ed with
                   overdiv_mask.  vars[i].r -= div & overdiv_mask;
                */
        }
    }
}



void
multi_step_up(fract *       const vars,
              const fract * const steps,
              uint8_t       const elements,
              int32_t       const times,
              int32_t       const div) {
/*----------------------------------------------------------------------------
  Similar to step_up, but apply the interpolation step an arbitrary number
  of times, instead of just once.

  It *is* also safe to pass a 0 divisor to this function.
-----------------------------------------------------------------------------*/
    unsigned int i;

    for (i = 0; i < elements; i++) {
        uint32_t const negative_mask = -steps[i].negative_flag;

        vars[i].q += times * steps[i].q;
        vars[i].r += times * steps[i].r;

        if(vars[i].r >= div && div != 0) {
            int32_t const r_q = vars[i].r / div;
            int32_t const r_r = vars[i].r % div;

            vars[i].q += (-r_q & negative_mask) | (r_q & ~negative_mask);
                /* = -r_q if the step is negative; r_q, otherwise. */
            vars[i].r = r_r;
        }
    }
}



void
gen_steps(const int32_t * const begin,
          const int32_t * const end,
          fract         * const out,
          uint8_t         const elements,
          int32_t         const div) {
/*----------------------------------------------------------------------------
  Generate the interpolation steps for a collection of initial and final
  values. "begin" points to an array of initial values, "end" points to the
  array of corresponding final values; each interpolation step is stored in
  the appropriate position in the array pointed by "out"; "elements" indicates
  the number of elements in each of the previously mentioned arrays and
  "divisor" is the common value by which we want to divide the difference
  between each element in the array pointed to by "end" and the corresponding
  element in the array pointed to by "begin".  After an execution of this
  function, for each out[i], with 0 <= i < elements, the following will hold:

    1. If divisor > 1:
      out[i].q = (end[i] - begin[i]) / divisor
      out[i].r = abs((end[i] - begin[i]) % divisor)

    2. If divisor == 1 || divisor == 0:
      out[i].q = end[i] - begin[i]
      out[i].r = 0
-----------------------------------------------------------------------------*/
    if (div > 1) {
        unsigned int i;

        for (i = 0; i < elements; i++) {
            int32_t const delta = end[i] - begin[i];

            out[i].q = delta / div;
            out[i].r = abs(delta % div);
            out[i].negative_flag = ((uint32_t)delta) >> 31;
        }
    } else {
        unsigned int i;

        for (i = 0; i < elements; i++) {
            int32_t const delta = end[i] - begin[i];

            out[i].q = delta;
            out[i].r = 0;
            out[i].negative_flag = ((uint32_t)delta) >> 31;
        }
    }
}



void
fract_to_int32_array(const fract * const in,
                     int32_t *     const out,
                     uint8_t       const elements) {

    unsigned int i;

    for (i = 0; i < elements; i++) {
        out[i] = in[i].q;
    }
}



void
int32_to_fract_array(const int32_t * const in,
                     fract *         const out,
                     uint8_t         const elements) {

    unsigned int i;

    for (i = 0; i < elements; i++) {
        out[i].q = in[i];
        out[i].r = 0;
    }
}



static void
swap(uint8_t * const a,
     uint8_t * const b) {
/*----------------------------------------------------------------------------
  Swap the contents pointed to by a and b.
-----------------------------------------------------------------------------*/
    uint8_t const temp = *a;

    *a = *b;
    *b = temp;
}



void
sort3(uint8_t *       const index_array,
      const int32_t * const y_array,
      const int32_t * const x_array) {
/*----------------------------------------------------------------------------
  Sort an index array of 3 elements. This function is used to sort vertices
  with regard to relative row from top to bottom, but instead of sorting
  an array of vertices with all their coordinates, we simply sort their
  indices. Each element in the array pointed to by "index_array" should
  contain one of the numbers 0, 1 or 2, and each one of them should be
  different. "y_array" should point to an array containing the corresponding
  Y coordinates (row) of each vertex and "x_array" should point to an array
  containing the corresponding X coordinates (column) of each vertex.

  If the Y coordinates are all equal, the indices are sorted with regard to
  relative X coordinate from left to right. If only the top two vertex have
  the same Y coordinate, the array is sorted normally with regard to relative
  Y coordinate, but the first two indices are then sorted with regard to
  relative X coordinate. Finally, If only the bottom two vertex have the same
  Y coordinate, the array is sorted normally with regard to relative Y
  coordinate, but the last two indices are then sorted with regard to relative
  X coordinate.
-----------------------------------------------------------------------------*/
    uint8_t * const ia = index_array;

    const int32_t * ya;
    const int32_t * xa;

    ya = y_array;  /* initial value */
    xa = x_array;  /* initial value */

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


