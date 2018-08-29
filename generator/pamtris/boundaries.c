/*=============================================================================
                                 boundaries.c
===============================================================================
   Boundary buffer functions

   New triangles are drawn one scanline at a time, and for every such scanline
   we have left and right boundary columns within the frame buffer such that
   the fraction of the triangle's area within that scanline is enclosed
   between those two points (inclusive). Those coordinates may correspond to
   columns outside the frame buffer's actual limits, in which case proper
   post-processing should be made wherever such coordinates are used to
   actually plot anything into the frame buffer.
=============================================================================*/

#include <stdlib.h>

#include <netpbm/mallocvar.h>
#include <netpbm/pm.h>

#include "utils.h"
#include "fract.h"


#include "boundaries.h"



static fract
make_pos_fract(int32_t const quotient,
               int32_t const remainder) {

    fract retval;

    retval.q = quotient;
    retval.r = remainder;
    retval.negative_flag = 0;

    return retval;
}



void
init_boundary_buffer(boundary_info * const bi,
                     int16_t         const height) {

    MALLOCARRAY(bi->buffer, height * 2);

    if (!bi->buffer)
        pm_error("Unable to get memory for %u-row high boundary buffer",
                 height);
}



void
free_boundary_buffer(boundary_info * bi) {
    free(bi->buffer);
}



bool
gen_triangle_boundaries(Xy              const xy,
                        boundary_info * const bi,
                        int16_t         const width,
                        int16_t         const height) {
/*----------------------------------------------------------------------------
  Generate an entry in the boundary buffer for the boundaries of every
  VISIBLE row of a particular triangle. In case there is no such row,
  start_row is accordingly set to -1. The argument is a 3-element array
  of pairs of int16_t's representing the coordinates of the vertices of
  a triangle. Those vertices MUST be already sorted in order from the
  uppermost to the lowermost vertex (which is what draw_triangle, the
  only function which uses this one, does with the help of sort3).

  The return value indicates whether the middle vertex is to the left of the
  line connecting the top vertex to the bottom vertex or not.
-----------------------------------------------------------------------------*/
    int16_t leftmost_x;
    int16_t rightmost_x;
    int mid_is_to_the_left;
    fract left_x;
    fract right_x;
    bool no_upper_part;
    int32_t top2mid_delta;
    int32_t top2bot_delta;
    int32_t mid2bot_delta;
    fract top2mid_step;
    fract top2bot_step;
    fract mid2bot_step;
    fract* upper_left_step;
    fract* lower_left_step;
    fract* upper_right_step;
    fract* lower_right_step;
    int32_t upper_left_delta;
    int32_t lower_left_delta;
    int32_t upper_right_delta;
    int32_t lower_right_delta;
    fract* left_step[2];
    fract* right_step[2];
    int32_t left_delta[2];
    int32_t right_delta[2];
    int16_t* num_rows_ptr[2];
    int32_t y;
    int32_t i;
    uint8_t k;

    leftmost_x = xy._[0][0];   /* initial value */
    rightmost_x = xy._[0][0];  /* initial value */

    bi->start_scanline = -1;
    bi->num_upper_rows = 0;
    bi->num_lower_rows = 0;

    if (xy._[2][1] < 0 || xy._[0][1] >= height) {
        /* Triangle is either completely above the topmost scanline or
           completely below the bottom scanline.
        */

        return false; /* Actual value doesn't matter. */
    }

    {
        unsigned int i;

        for (i = 1; i < 3; i++) {
            if (xy._[i][0] < leftmost_x) {
                leftmost_x = xy._[i][0];
            }

            if (xy._[i][0] > rightmost_x) {
                rightmost_x = xy._[i][0];
            }
        }
    }
    if (rightmost_x < 0 || leftmost_x >= width) {
        /* Triangle is either completely to the left of the leftmost
           framebuffer column or completely to the right of the rightmost
           framebuffer column.
        */
        return false; /* Actual value doesn't matter. */
    }

    if (xy._[0][1] == xy._[1][1] && xy._[1][1] == xy._[2][1]) {
        /* Triangle is degenarate: its visual representation consists only of
           a horizontal straight line.
        */

        bi->start_scanline = xy._[0][1];

        return false; /* Actual value doesn't matter. */
    }

    mid_is_to_the_left = 2;

    left_x  = make_pos_fract(xy._[0][0], 0);
    right_x = make_pos_fract(xy._[0][0], 0);

    if (xy._[0][1] == xy._[1][1]) {
        /* Triangle has only a lower part. */

        mid_is_to_the_left = 0;

        right_x.q = xy._[1][0];
    } else if (xy._[1][1] == xy._[2][1]) {
        /* Triangle has only an upper part (plus the row of the middle
           vertex).
        */

        mid_is_to_the_left = 1;
    }

    no_upper_part = (xy._[1][1] == xy._[0][1]);

    top2mid_delta = xy._[1][1] - xy._[0][1] + !no_upper_part;
    top2bot_delta = xy._[2][1] - xy._[0][1] + 1;
    mid2bot_delta = xy._[2][1] - xy._[1][1] + no_upper_part;

    gen_steps(&xy._[0][0], &xy._[1][0], &top2mid_step, 1, top2mid_delta);
    gen_steps(&xy._[0][0], &xy._[2][0], &top2bot_step, 1, top2bot_delta);
    gen_steps(&xy._[1][0], &xy._[2][0], &mid2bot_step, 1, mid2bot_delta);

    if (mid_is_to_the_left == 2) {
        if (top2bot_step.negative_flag) {
            if (top2mid_step.negative_flag) {
                if (top2mid_step.q == top2bot_step.q) {
                    mid_is_to_the_left =
                        top2mid_step.r * top2bot_delta >
                        top2bot_step.r * top2mid_delta;
                } else {
                    mid_is_to_the_left = top2mid_step.q < top2bot_step.q;
                }
            } else {
                mid_is_to_the_left = 0;
            }
        } else {
            if (!top2mid_step.negative_flag) {
                if (top2mid_step.q == top2bot_step.q) {
                    mid_is_to_the_left =
                        top2mid_step.r * top2bot_delta <
                        top2bot_step.r * top2mid_delta;
                } else {
                    mid_is_to_the_left = top2mid_step.q < top2bot_step.q;
                }
            } else {
                mid_is_to_the_left = 1;
            }
        }
    }
    if (mid_is_to_the_left) {
        upper_left_step     = &top2mid_step;
        lower_left_step     = &mid2bot_step;
        upper_right_step    = &top2bot_step;
        lower_right_step    = upper_right_step;

        upper_left_delta    = top2mid_delta;
        lower_left_delta    = mid2bot_delta;
        upper_right_delta   = top2bot_delta;
        lower_right_delta   = upper_right_delta;
    } else {
        upper_right_step    = &top2mid_step;
        lower_right_step    = &mid2bot_step;
        upper_left_step     = &top2bot_step;
        lower_left_step     = upper_left_step;

        upper_right_delta   = top2mid_delta;
        lower_right_delta   = mid2bot_delta;
        upper_left_delta    = top2bot_delta;
        lower_left_delta    = upper_left_delta;
    }

    left_step[0] = upper_left_step;
    left_step[1] = lower_left_step;
    right_step[0] = upper_right_step;
    right_step[1] = lower_right_step;
    left_delta[0] = upper_left_delta;
    left_delta[1] = lower_left_delta;
    right_delta[0] = upper_right_delta;
    right_delta[1] = lower_right_delta;
    num_rows_ptr[0] = &bi->num_upper_rows;
    num_rows_ptr[1] = &bi->num_lower_rows;

    y = xy._[0][1];

    i = 0;
    k = 0;

    if (no_upper_part) {
        k = 1;

        right_x.q = xy._[1][0];
    }

    step_up(&left_x, left_step[k], 1, left_delta[k]);
    step_up(&right_x, right_step[k], 1, right_delta[k]);

    while (k < 2) {
        int32_t end;

        end = xy._[k + 1][1] + k;  /* initial value */

        if (y < 0) {
            int32_t delta;

            if (end > 0) {
                delta = -y;
            } else {
                delta = xy._[k + 1][1] - y;
            }

            y += delta;

            multi_step_up(&left_x, left_step[k], 1, delta, left_delta[k]);
            multi_step_up(&right_x, right_step[k], 1, delta, right_delta[k]);

            if (y < 0) {
                k++;
                continue;
            }
        } else if(y >= height) {
            return mid_is_to_the_left;
        }

        if (end > height) {
            end = height;
        }

        while (y < end) {
            if (left_x.q >= width || right_x.q < 0) {
                if (bi->start_scanline > -1) {
                    return mid_is_to_the_left;
                }
            } else {
                if (bi->start_scanline == -1) {
                    bi->start_scanline = y;
                }

                bi->buffer[i++] = left_x.q;
                bi->buffer[i++] = right_x.q;

                (*(num_rows_ptr[k]))++;
            }

            step_up(&left_x, left_step[k], 1, left_delta[k]);
            step_up(&right_x, right_step[k], 1, right_delta[k]);

            y++;
        }
        k++;
    }
    return mid_is_to_the_left;
}



void
get_triangle_boundaries(uint16_t              const row_index,
                        int32_t *             const left,
                        int32_t *             const right,
                        const boundary_info * const bi) {
/*----------------------------------------------------------------------------
  Return the left and right boundaries for a given VISIBLE triangle row (the
  row index is relative to the first visible row). These values may be out of
  the horizontal limits of the frame buffer, which is necessary in order to
  compute correct attribute interpolations.
-----------------------------------------------------------------------------*/
    uint32_t const i  = row_index << 1;

    *left       = bi->buffer[i];
    *right      = bi->buffer[i + 1];
}


