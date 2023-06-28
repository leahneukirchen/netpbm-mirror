/*=============================================================================
                                 boundaries.c
===============================================================================
   Boundary buffer functions

   New triangles are drawn one row at a time, and for every such row we have
   left and right boundary columns within the frame buffer such that the
   fraction of the triangle's area within that scanline is enclosed between
   those two points (inclusive). Those coordinates may correspond to columns
   outside the frame buffer's actual limits, in which case proper
   post-processing should be made wherever such coordinates are used to
   actually plot anything into the frame buffer.
=============================================================================*/

#include <stdlib.h>

#include <netpbm/mallocvar.h>
#include <netpbm/pm.h>

#include "varying.h"
#include "utils.h"


#include "boundaries.h"



void
init_boundary_buffer(boundary_info * const bi,
                     int16_t         const height) {

    MALLOCARRAY(bi->buffer, height * 2);

    if (!bi->buffer) {
        pm_error("unable to get memory for %u-row high boundary buffer.",
                 height);
    }
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
  start_scanline is accordingly set to -1. "xy" is a 3-element array
  of pairs of integers representing the coordinates of the vertices of
  a triangle. Those vertices MUST be already sorted in order from the
  uppermost to the lowermost vertex (which is what draw_triangle, the
  only function which uses this one, does with the help of sort3).

  The return value indicates whether the middle vertex is to the left of
  the line connecting the top vertex to the bottom vertex or not.
-----------------------------------------------------------------------------*/
    int16_t leftmost_x;
    int16_t rightmost_x;
    int mid_is_to_the_left;
    varying top_x;
    varying mid_x;
    varying bot_x;
    varying top2mid;
    varying top2bot;
    varying mid2bot;
    varying* upper_left;
    varying* lower_left;
    varying* upper_right;
    varying* lower_right;
    varying* left[2];
    varying* right[2];
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
        /* Triangle is either completely above the uppermost scanline or
           completely below the lowermost scanline.
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
        /* Triangle is degenerate: its visual representation consists only of
           a horizontal straight line.
        */

        bi->start_scanline = xy._[0][1];

        return false; /* Actual value doesn't matter. */
    }

    mid_is_to_the_left = 2;

    int32_to_varying_array(&xy._[0][0], &top_x, 1);
    int32_to_varying_array(&xy._[1][0], &mid_x, 1);
    int32_to_varying_array(&xy._[2][0], &bot_x, 1);

    if (xy._[0][1] == xy._[1][1]) {
        /* Triangle has only a lower part. */
        k = 1;

        mid_is_to_the_left = 0;
    } else {
        k = 0;

        if (xy._[1][1] == xy._[2][1]) {
            /* Triangle has only an upper part (plus the row of the middle
               vertex).
            */
            mid_is_to_the_left = 1;
        }
    }

    prepare_for_interpolation(&top_x, &mid_x, &top2mid, xy._[1][1] - xy._[0][1], 1);
    prepare_for_interpolation(&top_x, &bot_x, &top2bot, xy._[2][1] - xy._[0][1], 1);
    prepare_for_interpolation(&mid_x, &bot_x, &mid2bot, xy._[2][1] - xy._[1][1], 1);

    if (mid_is_to_the_left == 2) {
        mid_is_to_the_left = top2mid.s < top2bot.s;
    }

    if (mid_is_to_the_left) {
        upper_left     = &top2mid;
        lower_left     = &mid2bot;
        upper_right    = &top2bot;
        lower_right    = upper_right;
    } else {
        upper_right    = &top2mid;
        lower_right    = &mid2bot;
        upper_left     = &top2bot;
        lower_left     = upper_left;
    }

    left[0] = upper_left;
    left[1] = lower_left;
    right[0] = upper_right;
    right[1] = lower_right;

    num_rows_ptr[0] = &bi->num_upper_rows;
    num_rows_ptr[1] = &bi->num_lower_rows;

    y = xy._[0][1];

    i = 0;

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

            multi_step_up(left[k], delta, 1);
            multi_step_up(right[k], delta, 1);

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
            if (round_varying(*left[k]) >= width || round_varying(*right[k]) < 0) {
                if (bi->start_scanline > -1) {
                    return mid_is_to_the_left;
                }
            } else {
                if (bi->start_scanline == -1) {
                    bi->start_scanline = y;
                }

                bi->buffer[i++] = round_varying(*left[k]);
                bi->buffer[i++] = round_varying(*right[k]);

                (*(num_rows_ptr[k]))++;
            }

            step_up(left[k], 1);
            step_up(right[k], 1);

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


