/*=============================================================================
                                  triangle.c
===============================================================================
   Triangle functions
=============================================================================*/
#include <stdlib.h>
#include <string.h>

#include "netpbm/mallocvar.h"

#include "utils.h"
#include "varying.h"
#include "boundaries.h"
#include "framebuffer.h"

#include "triangle.h"

static void
draw_partial_triangle(
    const varying *       const left_attribs_input,
    const varying *       const rght_attribs_input,
    bool                  const upper_part,
    const boundary_info * const bi,
    framebuffer_info *    const fbi) {

    uint8_t const z = fbi->num_attribs;
    uint8_t const w = z + 1;
    uint8_t const n = w + 1;

    varying * left_attribs;
    varying * rght_attribs;

    varying * attribs;

    int32_t first_row_index;
    int32_t last_row_index;

    MALLOCARRAY_NOFAIL(left_attribs, n);
    MALLOCARRAY_NOFAIL(rght_attribs, n);
    MALLOCARRAY_NOFAIL(attribs, n);

    memcpy(left_attribs, left_attribs_input, n * sizeof(varying));
    memcpy(rght_attribs, rght_attribs_input, n * sizeof(varying));

    if (upper_part) {
        first_row_index = 0;
        last_row_index = bi->num_upper_rows - 1;
    } else {
        first_row_index = bi->num_upper_rows;
        last_row_index = bi->num_upper_rows + bi->num_lower_rows - 1;
    }

    {
        int32_t const row_delta = last_row_index - first_row_index;

        int32_t row;

        int32_t left_boundary;
        int32_t rght_boundary;

        for (row = first_row_index; row <= last_row_index; row++) {
            get_triangle_boundaries(row, &left_boundary, &rght_boundary, bi);
            {
                int32_t const column_delta = rght_boundary - left_boundary;
                int32_t start_column;
                int32_t span_length;

                start_column = left_boundary;  /* initial value */
                span_length = column_delta;    /* initial value */

                prepare_for_interpolation(left_attribs, rght_attribs,
                                          attribs, column_delta,
                                          n);

                if (left_boundary < 0) {
                    start_column = 0;

                    span_length += left_boundary;

                    multi_step_up(attribs, -left_boundary, n);
                }

                if (rght_boundary >= fbi->width) {
                    span_length -= rght_boundary - fbi->width;
                } else {
                    span_length++;
                }

                draw_span(
                    (bi->start_scanline + row) * fbi->width + start_column,
                    span_length, attribs, fbi);

                if (row_delta > 0) {
                    step_up(left_attribs, n);
                    step_up(rght_attribs, n);
                }
            }
        }
    }
    free(attribs);
    free(rght_attribs);
    free(left_attribs);
}



static void
draw_degenerate_horizontal(Xy                 const xy,
                           varying *          const top2mid,
                           varying *          const top2bot,
                           varying *          const mid2bot,
                           framebuffer_info * const fbi) {

    uint8_t const n = fbi->num_attribs + 2;

    {
        int16_t const y = xy._[0][1];

        int16_t x[3];
        int16_t x_start[3];
        varying * attribs[3];
        int32_t span_length[3];
        unsigned int i;

        x[0] = xy._[0][0];
        x[1] = xy._[1][0];
        x[2] = xy._[2][0];

        x_start[0] = x[0];
        x_start[1] = x[0];
        x_start[2] = x[1];

        attribs[0] = top2bot;
        attribs[1] = top2mid;
        attribs[2] = mid2bot;

        span_length[0] = x[2] - x[0];
        span_length[1] = x[1] - x[0];
        span_length[2] = x[2] - x[1];

        for (i = 0; i < 3; i++) {
            if (x_start[i] >= fbi->width || x_start[i] + span_length[i] < 0) {
                continue;
            }

            if (x_start[i] < 0) {
                multi_step_up(attribs[i], -x_start[i], n);

                span_length[i] += x_start[i];

                x_start[i] = 0;
            }

            if (x_start[i] + span_length[i] >= fbi->width) {
                span_length[i] -= x_start[i] + span_length[i] - fbi->width;
            } else {
                span_length[i]++;
            }

            draw_span(y * fbi->width + x_start[i], span_length[i],
                      attribs[i], fbi);
        }
    }
}



void
draw_triangle(Xy                 const xy_input,
              Attribs            const attribs_input,
              boundary_info *    const bi,
              framebuffer_info * const fbi) {

    uint8_t const z = fbi->num_attribs;
    uint8_t const w = z + 1;
    uint8_t const n = w + 1;

    Xy xy;
    varying * attribs[3];
    unsigned int i;
    uint8_t index_array[3];
    int32_t y_array[3];
    int32_t x_array[3];

    MALLOCARRAY_NOFAIL(attribs[0], n);
    MALLOCARRAY_NOFAIL(attribs[1], n);
    MALLOCARRAY_NOFAIL(attribs[2], n);

    xy = xy_input;

    for (i = 0; i < 3; i++) {
        int32_to_varying_array(attribs_input._[i], attribs[i], n);
	attribs[i][z] = compute_varying_z(attribs_input._[i][z]);
	attribs[i][w] = inverse_varying(attribs[i][w]);
        multiply_varying_array_by_varying(attribs[i], attribs[i][w], z);
    }

    /* Argument preparations for sort3: */

    index_array[0] = 0; index_array[1] = 1; index_array[2] = 2;
    y_array[0] = xy._[0][1]; y_array[1] = xy._[1][1]; y_array[2] = xy._[2][1];
    x_array[0] = xy._[0][0]; x_array[1] = xy._[1][0]; x_array[2] = xy._[2][0];

    sort3(index_array, y_array, x_array);

    {
        uint8_t const top = index_array[0];
        uint8_t const mid = index_array[1];
        uint8_t const bot = index_array[2];

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
            /* Triangle is completely out of the bounds of the frame buffer. */
        } else {
            bool const no_upper_part =
                (xy_sorted._[1][1] == xy_sorted._[0][1]);

            bool const horizontal =
                (xy._[0][1] == xy._[1][1] && xy._[1][1] == xy._[2][1]);
                /* Tells whether we are dealing with a degenerate
                 * horizontal triangle */

            uint8_t const t = horizontal ^ 1;

            int32_t top2mid_delta = xy._[mid][t] - xy._[top][t];
            int32_t top2bot_delta = xy._[bot][t] - xy._[top][t];
            int32_t mid2bot_delta = xy._[bot][t] - xy._[mid][t];

            varying * top2mid;
            varying * top2bot;
            varying * mid2bot;

            varying * upper_left_attribs;
            varying * lower_left_attribs;
            varying * upper_rght_attribs;
            varying * lower_rght_attribs;

            MALLOCARRAY_NOFAIL(top2mid, n);
            MALLOCARRAY_NOFAIL(top2bot, n);
            MALLOCARRAY_NOFAIL(mid2bot, n);

            prepare_for_interpolation(attribs[top], attribs[mid], top2mid, top2mid_delta, n);
            prepare_for_interpolation(attribs[top], attribs[bot], top2bot, top2bot_delta, n);
            prepare_for_interpolation(attribs[mid], attribs[bot], mid2bot, mid2bot_delta, n);

            if (mid_is_to_the_left) {
                upper_left_attribs = top2mid;
                lower_left_attribs = mid2bot;
                upper_rght_attribs = top2bot;
                lower_rght_attribs = upper_rght_attribs;
            } else {
                upper_rght_attribs = top2mid;
                lower_rght_attribs = mid2bot;
                upper_left_attribs = top2bot;
                lower_left_attribs = upper_left_attribs;
            }

            if (!(horizontal || no_upper_part)) {
                int32_t delta;

                if (bi->num_upper_rows > 0) {
                    if (bi->start_scanline > xy._[top][1]) {
                        delta = bi->start_scanline - xy._[top][1];

                        multi_step_up(upper_left_attribs, delta, n);
                        multi_step_up(upper_rght_attribs, delta, n);
                    }

                    draw_partial_triangle(
                        upper_left_attribs,
                        upper_rght_attribs,
                        true,
                        bi,
                        fbi
                        );

                    delta = xy._[mid][1] - bi->start_scanline;
                } else {
                    delta = top2mid_delta;
                }

                multi_step_up(upper_left_attribs, delta, n);
                multi_step_up(upper_rght_attribs, delta, n);
            }

            if (horizontal) {
                draw_degenerate_horizontal(
                    xy_sorted,
                    top2mid, top2bot, mid2bot,
                    fbi
                    );
            } else {
                if (bi->start_scanline > xy._[mid][1]) {
                    int32_t const delta = bi->start_scanline - xy._[mid][1];

                    multi_step_up(lower_left_attribs, delta, n);
                    multi_step_up(lower_rght_attribs, delta, n);
                }

                draw_partial_triangle(
                    lower_left_attribs,
                    lower_rght_attribs,
                    false,
                    bi,
                    fbi
                    );
            }
            free(mid2bot); free(top2bot); free(top2mid);
        }
    }
    free(attribs[2]); free(attribs[1]); free(attribs[0]);
}


