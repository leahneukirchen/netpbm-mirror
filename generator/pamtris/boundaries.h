#ifndef BOUNDARIES_H_INCLUDED
#define BOUNDARIES_H_INCLUDED

#include <stdint.h>

typedef struct boundary_info {
/*----------------------------------------------------------------------------
  Information about visible triangle rows' boundaries. Also see the
  "boundary buffer functions" below.

  A "visible" triangle row is one which:

    1. Corresponds to a frame buffer row whose index (from top to bottom) is
       equal to or greater than 0 and smaller than the image height; and

    2. Has at least some of its pixels between the frame buffer columns whose
       index (from left to right) is equal to or greater than 0 and smaller
       than the image width.
-----------------------------------------------------------------------------*/
    int16_t start_scanline;
        /* Index of the frame buffer scanline which contains the first visible
           row of the current triangle, if there is any such row. If not, it
           contains the value -1.
        */

    int16_t num_upper_rows;
        /* The number of visible rows in the upper part of the triangle. The
           upper part of a triangle is composed of all the rows starting from
           the top vertex down to the middle vertex, but not including this
           last one.
        */

    int16_t num_lower_rows;
        /* The number of visible rows in the lower part of the triangle. The
           lower part of a triangle is composed of all the rows from the
           middle vertex to the bottom vertex -- all inclusive.
        */

    int16_t * buffer;
        /* This is the "boundary buffer": a pointer to an array of int16_t's
           where each consecutive pair of values indicates, in this order, the
           columns of the left and right boundary pixels for a particular
           visible triangle row. Those boundaries are inclusive on both sides
           and may be outside the limits of the frame buffer. This field is
           initialized and freed by the functions "init_boundary_buffer" and
           "free_boundary_buffer", respectively.
        */
} boundary_info;

void
init_boundary_buffer(boundary_info * ,
                     int16_t         height);

void
free_boundary_buffer(boundary_info *);

bool
gen_triangle_boundaries(int32_t         xy[3][2],
                        boundary_info *,
                        int16_t         width,
                        int16_t         height);

void
get_triangle_boundaries(uint16_t              row_index,
                        int32_t *             left,
                        int32_t *             right,
                        const boundary_info *);

#endif
