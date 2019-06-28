/*=============================================================================
                              utils.c
===============================================================================
   Utility functions
=============================================================================*/

#include <stdlib.h>
#include <stdint.h>
#include <math.h>

#include "limits_pamtris.h"
#include "varying.h"

#include "utils.h"



void
prepare_for_interpolation(const varying * const begin,
                          const varying * const end,
                          varying *       const out,
                          int32_t               num_steps,
                          uint8_t         const elements) {

    double inverse_num_steps;
    unsigned int i;

    if (num_steps < 1) {
        num_steps = 1;
    }

    inverse_num_steps = 1.0 / num_steps;

    for (i = 0; i < elements; i++) {
        out[i].v = begin[i].v;
        out[i].s = (end[i].v - begin[i].v) * inverse_num_steps;
    }
}



varying
compute_varying_z(int32_t const input_z) {

    varying retval;

    retval.v = 1.0 / (1 + input_z - MIN_COORD);
    retval.s = 0.0;

    return retval;
}



void
multiply_varying_array_by_varying(varying * const vars,
                                  varying   const multiplier,
                                  uint8_t   const elements) {

    unsigned int i;

    for (i = 0; i < elements; i++) {
        vars[i].v *= multiplier.v;
	vars[i].s  = 0.0;
    }
}


void
divide_varying_array_by_varying(varying * const vars,
                                varying   const divisor,
                                uint8_t   const elements) {

    double const inverse_divisor = 1.0 / divisor.v;

    unsigned int i;

    for (i = 0; i < elements; i++) {
        vars[i].v *= inverse_divisor;
	vars[i].s  = 0.0;
    }
}



varying
inverse_varying(varying const var) {

    varying retval;

    retval.v = 1.0 / var.v;
    retval.s = 0.0;

    return retval;
}



varying
multiply_varyings(varying const a,
                  varying const b) {

    varying retval;

    retval.v = a.v * b.v;
    retval.s = 0.0;

    return retval;
}



void
step_up(varying * const vars,
        uint8_t   const elements) {

    unsigned int i;

    for (i = 0; i < elements; i++) {
        vars[i].v += vars[i].s;
    }
}



void
multi_step_up(varying * const vars,
              int32_t   const times,
              uint8_t   const elements) {

    unsigned int i;

    for (i = 0; i < elements; i++) {
        vars[i].v += times * vars[i].s;
    }
}



void
int32_to_varying_array(const int32_t * const in,
                       varying *       const out,
                       uint8_t         const elements) {

    unsigned int i;

    for (i = 0; i < elements; i++) {
        out[i].v = in[i];
        out[i].s = 0.0;
    }
}



/* static int64_t
abs64(int64_t x)
{

    int64_t const nm = ~geq_mask64(x, 0);

    return (-x & nm) | (x & ~nm);
} */



int32_t
round_varying(varying const var) {

    return round(var.v);
}



int64_t
geq_mask64(int64_t a, int64_t b) {

    uint64_t const diff = a - b;

    return -((~diff) >> 63);
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


