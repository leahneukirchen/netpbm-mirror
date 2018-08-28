/*=============================================================================
                                  triangle.c
===============================================================================
   Triangle functions
=============================================================================*/
#include <stdlib.h>
#include <string.h>

#include "netpbm/mallocvar.h"

#include "utils.h"
#include "fract.h"
#include "limits.h"
#include "boundaries.h"
#include "framebuffer.h"

#include "triangle.h"

static void
draw_partial_triangle(
    const fract *         const left_attribs_input,
    const fract *         const left_attribs_steps,
    const fract *         const right_attribs_input,
    const fract *         const right_attribs_steps,
    int32_t               const left_div,
    int32_t               const right_div,
    bool                  const upper_part,
    const boundary_info * const bi,
    framebuffer_info *    const fbi) {

    uint8_t num_planes = fbi->num_attribs + 1;

    fract * left_attribs;
    fract * right_attribs;

    int32_t first_row_index;
    int32_t last_row_index;

    MALLOCARRAY_NOFAIL(left_attribs, num_planes);
    MALLOCARRAY_NOFAIL(right_attribs, num_planes);

    memcpy(left_attribs, left_attribs_input, num_planes * sizeof(fract));
    memcpy(right_attribs, right_attribs_input, num_planes * sizeof(fract));

    if (upper_part) {
        first_row_index = 0;
        last_row_index = bi->num_upper_rows - 1;
    } else {
        first_row_index = bi->num_upper_rows;
        last_row_index = bi->num_upper_rows + bi->num_lower_rows - 1;
    }

    {
        int32_t row_delta = last_row_index - first_row_index;
        int32_t row = first_row_index;

        int32_t left_boundary;
        int32_t right_boundary;

        while (row <= last_row_index) {
            get_triangle_boundaries(row, &left_boundary, &right_boundary, bi);
            {
                int32_t column_delta = right_boundary - left_boundary;
                int32_t start_column = left_boundary;
                int32_t span_length = column_delta;

                fract   * attribs_start;
                int32_t * attribs_begin;
                int32_t * attribs_end;
                fract   * attribs_steps;

                MALLOCARRAY(attribs_start, num_planes);
                MALLOCARRAY(attribs_begin, num_planes);
                MALLOCARRAY(attribs_end,   num_planes);
                MALLOCARRAY(attribs_steps, num_planes);

                fract_to_int32_array(left_attribs, attribs_begin, num_planes);
                fract_to_int32_array(right_attribs, attribs_end, num_planes);

                int32_to_fract_array(attribs_begin, attribs_start, num_planes);

                gen_steps(attribs_begin, attribs_end, attribs_steps,
                          num_planes, column_delta);

                if (left_boundary < 0) {
                    start_column = 0;

                    span_length += left_boundary;

                    multi_step_up(attribs_start, attribs_steps, num_planes,
                                  -left_boundary, column_delta);
                }

                if (right_boundary >= fbi->width) {
                    span_length -= right_boundary - fbi->width;
                } else {
                    span_length++;
                }

                draw_span(
                    ((bi->start_scanline + row) * fbi->width) + start_column,
                    span_length, attribs_start, attribs_steps, column_delta,
                    fbi);

                if (row_delta > 0) {
                    step_up(left_attribs, left_attribs_steps, num_planes,
                            left_div);
                    step_up(right_attribs, right_attribs_steps, num_planes,
                            right_div);
                }
                row++;
                free(attribs_steps);
                free(attribs_end);
                free(attribs_begin);
                free(attribs_start);
            }
        }
    }
    free(right_attribs);
    free(left_attribs);
}



static void
draw_degenerate_horizontal(Xy                 const xy,
                           fract *            const attribs_left,
                           fract *            const attribs_mid,
                           const fract *      const top2mid_steps,
                           const fract *      const top2bot_steps,
                           const fract *      const mid2bot_steps,
                           int32_t            const top2mid_delta,
                           int32_t            const top2bot_delta,
                           int32_t            const mid2bot_delta,
                           framebuffer_info * const fbi) {

    uint8_t num_planes = fbi->num_attribs + 1;

    fract * attribs_left_bkup;

    MALLOCARRAY_NOFAIL(attribs_left_bkup, num_planes);

    memcpy(attribs_left_bkup, attribs_left, num_planes * sizeof(fract));

    {
        int16_t y = xy._[0][1];

        int16_t x[3];
        int16_t x_start[3];
        fract * attribs[3];
        const fract * steps[3];
        int32_t span_length[3];
        unsigned int i;

        x[0] = xy._[0][0];
        x[1] = xy._[1][0];
        x[2] = xy._[2][0];

        x_start[0] = x[0];
        x_start[1] = x[0];
        x_start[2] = x[1];

        attribs[0] = attribs_left;
        attribs[1] = attribs_left_bkup;
        attribs[2] = attribs_mid;

        steps[0] = top2bot_steps;
        steps[1] = top2mid_steps;
        steps[2] = mid2bot_steps;

        span_length[0] = x[2] - x[0];
        span_length[1] = x[1] - x[0];
        span_length[2] = x[2] - x[1];

        for (i = 0; i < 3; i++) {
            int32_t column_delta = span_length[i];

            if (x_start[i] >= fbi->width || x_start[i] + span_length[i] < 0) {
                continue;
            }

            if (x_start[i] < 0) {
                multi_step_up(attribs[i], steps[i], num_planes, -x_start[i],
                              column_delta);

                span_length[i] += x_start[i];

                x_start[i] = 0;
            }

            if (x_start[i] + span_length[i] >= fbi->width) {
                span_length[i] -= x_start[i] + span_length[i] - fbi->width;
            } else {
                span_length[i]++;
            }

            draw_span((y * fbi->width) + x_start[i], span_length[i],
                      attribs[i], steps[i], column_delta, fbi);
        }
    }
    free(attribs_left_bkup);
}



void
draw_triangle(Xy                 const xy_input,
              Attribs            const attribs_input,
              boundary_info *    const bi,
              framebuffer_info * const fbi) {

    uint8_t num_planes = fbi->num_attribs + 1;

    Xy xy;
    int32_t * attribs[3];
    unsigned int i;
    uint8_t index_array[3];
    int32_t y_array[3];
    int32_t x_array[3];

    MALLOCARRAY_NOFAIL(attribs[0], num_planes);
    MALLOCARRAY_NOFAIL(attribs[1], num_planes);
    MALLOCARRAY_NOFAIL(attribs[2], num_planes);

    xy = xy_input;

    for (i = 0; i < 3; i++) {
        memcpy(attribs[i], attribs_input._[i], num_planes * sizeof(int32_t));
    }

    /* Argument preparations for sort3: */

    index_array[0] = 0; index_array[1] = 1; index_array[2] = 2;
    y_array[0] = xy._[0][1]; y_array[1] = xy._[1][1]; y_array[2] = xy._[2][1];
    x_array[0] = xy._[0][0]; x_array[1] = xy._[1][0]; x_array[2] = xy._[2][0];

    sort3(index_array, y_array, x_array);

    {
        uint8_t top = index_array[0];
        uint8_t mid = index_array[1];
        uint8_t bot = index_array[2];

        bool mid_is_to_the_left;

        Xy xy_sorted;

        xy_sorted._[0][0] = xy._[top][0];
        xy_sorted._[0][1] = xy._[top][1];
        xy_sorted._[1][0] = xy._[mid][0];
        xy_sorted._[1][1] = xy._[mid][1];
        xy_sorted._[2][0] = xy._[bot][0];
        xy_sorted._[2][1] = xy._[bot][1];

        mid_is_to_the_left =
            gen_triangle_boundaries(xy_sorted, bi, fbi->width, fbi->height);

        if (bi->start_scanline == -1) {
            /* Triangle completely out of the bounds of the framebuffer. */

            return;
        } else {
            bool no_upper_part = (xy_sorted._[1][1] == xy_sorted._[0][1]);

            bool horizontal =
                (xy._[0][1] == xy._[1][1] && xy._[1][1] == xy._[2][1]);
                /* We are dealing with a degenerate horizontal triangle */

            uint8_t t = ~horizontal & 1;

            int32_t top2mid_delta = xy._[mid][t] - xy._[top][t];
            int32_t top2bot_delta = xy._[bot][t] - xy._[top][t];
            int32_t mid2bot_delta = xy._[bot][t] - xy._[mid][t];

            fract * top2mid_steps;
            fract * top2bot_steps;
            fract * mid2bot_steps;

            fract * upper_left_attribs_steps;
            fract * lower_left_attribs_steps;
            fract * upper_right_attribs_steps;
            fract * lower_right_attribs_steps;

            int32_t upper_left_delta;
            int32_t lower_left_delta;
            int32_t upper_right_delta;
            int32_t lower_right_delta;

            fract * left_attribs;
            fract * right_attribs;

            bool degenerate_horizontal;

            MALLOCARRAY_NOFAIL(top2mid_steps, num_planes);
            MALLOCARRAY_NOFAIL(top2bot_steps, num_planes);
            MALLOCARRAY_NOFAIL(mid2bot_steps, num_planes);
            MALLOCARRAY_NOFAIL(left_attribs, num_planes);
            MALLOCARRAY_NOFAIL(right_attribs, num_planes);

            if (horizontal == false) {
                top2mid_delta += !no_upper_part;
                top2bot_delta += 1;
                mid2bot_delta += no_upper_part;
            }

            gen_steps(attribs[top], attribs[mid], top2mid_steps, num_planes,
                      top2mid_delta);
            gen_steps(attribs[top], attribs[bot], top2bot_steps, num_planes,
                      top2bot_delta);
            gen_steps(attribs[mid], attribs[bot], mid2bot_steps, num_planes,
                      mid2bot_delta);

            int32_to_fract_array(attribs[top], left_attribs, num_planes);
            int32_to_fract_array(attribs[top], right_attribs, num_planes);

            if (mid_is_to_the_left) {
                upper_left_attribs_steps    = top2mid_steps;
                lower_left_attribs_steps    = mid2bot_steps;
                upper_right_attribs_steps   = top2bot_steps;
                lower_right_attribs_steps   = upper_right_attribs_steps;

                upper_left_delta        = top2mid_delta;
                lower_left_delta        = mid2bot_delta;
                upper_right_delta       = top2bot_delta;
                lower_right_delta       = upper_right_delta;
            } else {
                upper_right_attribs_steps   = top2mid_steps;
                lower_right_attribs_steps   = mid2bot_steps;
                upper_left_attribs_steps    = top2bot_steps;
                lower_left_attribs_steps    = upper_left_attribs_steps;

                upper_right_delta       = top2mid_delta;
                lower_right_delta       = mid2bot_delta;
                upper_left_delta        = top2bot_delta;
                lower_left_delta        = upper_left_delta;
            }

            if (no_upper_part) {
                int32_to_fract_array(attribs[mid], right_attribs, num_planes);

                if (horizontal) {
                    degenerate_horizontal = true;
                } else {
                    degenerate_horizontal = false;

                    step_up(left_attribs, lower_left_attribs_steps, num_planes,
                            lower_left_delta);
                    step_up(right_attribs, lower_right_attribs_steps, num_planes,
                            lower_right_delta);
                }
            } else {
                int32_t delta;

                degenerate_horizontal = false;

                step_up(left_attribs, upper_left_attribs_steps, num_planes,
                        upper_left_delta);
                step_up(right_attribs, upper_right_attribs_steps, num_planes,
                        upper_right_delta);

                if (bi->num_upper_rows > 0) {

                    if (bi->start_scanline > xy._[top][1]) {
                        delta = bi->start_scanline - xy._[top][1];

                        multi_step_up(left_attribs, upper_left_attribs_steps,
                                      num_planes, delta, upper_left_delta);
                        multi_step_up(right_attribs, upper_right_attribs_steps,
                                      num_planes, delta, upper_right_delta);
                    }

                    draw_partial_triangle(
                        left_attribs, upper_left_attribs_steps,
                        right_attribs, upper_right_attribs_steps,
                        upper_left_delta, upper_right_delta,
                        true,
                        bi,
                        fbi
                        );

                    delta = xy._[mid][1] - bi->start_scanline;
                } else {
                    delta = top2mid_delta;
                }

                multi_step_up(left_attribs, upper_left_attribs_steps,
                              num_planes, delta, upper_left_delta);
                multi_step_up(right_attribs, upper_right_attribs_steps,
                              num_planes, delta, upper_right_delta);
            }
            if (degenerate_horizontal) {
                draw_degenerate_horizontal(
                    xy_sorted,
                    left_attribs, right_attribs,
                    top2mid_steps, top2bot_steps, mid2bot_steps,
                    top2mid_delta, top2bot_delta, mid2bot_delta,
                    fbi
                    );
            } else {
                if (bi->start_scanline > xy._[mid][1]) {
                    int32_t delta = bi->start_scanline - xy._[mid][1];

                    multi_step_up(left_attribs, lower_left_attribs_steps,
                                  num_planes, delta, lower_left_delta);
                    multi_step_up(right_attribs, lower_right_attribs_steps,
                                  num_planes, delta, lower_right_delta);
                }

                draw_partial_triangle(
                    left_attribs, lower_left_attribs_steps,
                    right_attribs, lower_right_attribs_steps,
                    lower_left_delta, lower_right_delta,
                    false,
                    bi,
                    fbi
                    );
            }
            free(right_attribs); free(left_attribs);
            free(mid2bot_steps); free(top2bot_steps); free(top2mid_steps);
        }
    }
    free(attribs[2]); free(attribs[1]); free(attribs[0]);
}


