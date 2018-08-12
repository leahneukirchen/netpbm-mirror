#include <stdlib.h>

#include "common.h"



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
init_boundary_buffer(boundary_info * bi,
                     int16_t         height) {
    MALLOCARRAY_NOFAIL(bi->buffer, height * 2 * sizeof(int16_t));
}



void
free_boundary_buffer(boundary_info * bi) {
    free(bi->buffer);
}



bool
gen_triangle_boundaries(int32_t         xy[3][2],
                        boundary_info * bi,
                        int16_t         width,
                        int16_t         height) {

    int16_t leftmost_x = xy[0][0];
    int16_t rightmost_x = xy[0][0];
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
    int32_t i = 0;
    uint8_t k = 0;

    bi->start_scanline = -1;
    bi->num_upper_rows = 0;
    bi->num_lower_rows = 0;

    if (xy[2][1] < 0 || xy[0][1] >= height) {
        /* Triangle is either completely above the topmost scanline or
           completely below the bottom scanline.
        */

        return false; /* Actual value doesn't matter. */
    }

    {
        unsigned int i;

        for (i = 1; i < 3; i++) {
            if (xy[i][0] < leftmost_x) {
                leftmost_x = xy[i][0];
            }

            if (xy[i][0] > rightmost_x) {
                rightmost_x = xy[i][0];
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

    if (xy[0][1] == xy[1][1] && xy[1][1] == xy[2][1]) {
        /* Triangle is degenarate: its visual representation consists only of
           a horizontal straight line.
        */

        bi->start_scanline = xy[0][1];

        return false; /* Actual value doesn't matter. */
    }

    mid_is_to_the_left = 2;

    left_x  = make_pos_fract(xy[0][0], 0);
    right_x = make_pos_fract(xy[0][0], 0);

    if (xy[0][1] == xy[1][1]) {
        /* Triangle has only a lower part. */

        mid_is_to_the_left = 0;

        right_x.q = xy[1][0];
    } else if (xy[1][1] == xy[2][1]) {
        /* Triangle has only an upper part (plus the row of the middle
           vertex).
        */

        mid_is_to_the_left = 1;
    }

    no_upper_part = (xy[1][1] == xy[0][1]);

    top2mid_delta = xy[1][1] - xy[0][1] + !no_upper_part;
    top2bot_delta = xy[2][1] - xy[0][1] + 1;
    mid2bot_delta = xy[2][1] - xy[1][1] + no_upper_part;

    gen_steps(&xy[0][0], &xy[1][0], &top2mid_step, 1, top2mid_delta);
    gen_steps(&xy[0][0], &xy[2][0], &top2bot_step, 1, top2bot_delta);
    gen_steps(&xy[1][0], &xy[2][0], &mid2bot_step, 1, mid2bot_delta);

    if (mid_is_to_the_left == 2) {
        if (top2bot_step.negative_flag == true) {
            if (top2mid_step.negative_flag == true) {
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
            if (top2mid_step.negative_flag == false) {
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

    y = xy[0][1];

    i = 0;
    k = 0;

    if (no_upper_part == true) {
        k = 1;

        right_x.q = xy[1][0];
    }

    step_up(&left_x, left_step[k], 1, left_delta[k]);
    step_up(&right_x, right_step[k], 1, right_delta[k]);

    while (k < 2) {
        int32_t end = xy[k + 1][1] + k;

        if (y < 0) {
            int32_t delta;

            if (end > 0) {
                delta = -y;
            } else {
                delta = xy[k + 1][1] - y;
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
get_triangle_boundaries(uint16_t              row_index,
                        int32_t *             left,
                        int32_t *             right,
                        const boundary_info * bi) {

    uint32_t i  = row_index << 1;

    *left       = bi->buffer[i];
    *right      = bi->buffer[i + 1];
}


