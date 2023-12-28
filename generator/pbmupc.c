/* pbmupc.c - create a Universal Product Code bitmap
**
** Copyright (C) 1988 by Jef Poskanzer.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

#include <string.h>

#include "pbm.h"

#define MARGIN 20
#define DIGIT_WIDTH 14
#define DIGIT_HEIGHT 23
#define LINE1_WIDTH 2

#define LINE2_WIDTH ( 2 * LINE1_WIDTH )
#define LINE3_WIDTH ( 3 * LINE1_WIDTH )
#define LINE4_WIDTH ( 4 * LINE1_WIDTH )
#define LINES_WIDTH ( 7 * LINE1_WIDTH )
#define SHORT_HEIGHT ( 8 * LINES_WIDTH )
#define TALL_HEIGHT ( SHORT_HEIGHT + DIGIT_HEIGHT / 2 )



static int
alldig(const char * const cp) {

    unsigned int i;

    for (i = 0; cp[i] != '\0'; ++i)
        if (cp[i] < '0' || cp[i] > '9')
            return 0;

    return 1;
}



static void
putDigit(int          const d,
         bit **       const bits,
         unsigned int const row0,
         unsigned int const col0) {

    static bit const digits[10][DIGIT_HEIGHT][DIGIT_WIDTH] = {
        /* 0 */
        {
            {0,0,0,0,0,0,0,0,0,0,0,0,0,0},
            {0,0,0,0,0,0,0,0,0,0,0,0,0,0},
            {0,0,0,0,1,1,1,1,1,1,0,0,0,0},
            {0,0,0,1,1,1,1,1,1,1,1,0,0,0},
            {0,0,1,1,1,0,0,0,0,1,1,1,0,0},
            {0,0,1,1,0,0,0,0,0,0,1,1,0,0},
            {0,1,1,1,0,0,0,0,0,0,1,1,1,0},
            {0,1,1,0,0,0,0,0,0,0,0,1,1,0},
            {0,1,1,0,0,0,0,0,0,0,0,1,1,0},
            {0,1,1,0,0,0,0,0,0,0,0,1,1,0},
            {0,1,1,0,0,0,0,0,0,0,0,1,1,0},
            {0,1,1,0,0,0,0,0,0,0,0,1,1,0},
            {0,1,1,0,0,0,0,0,0,0,0,1,1,0},
            {0,1,1,0,0,0,0,0,0,0,0,1,1,0},
            {0,1,1,0,0,0,0,0,0,0,0,1,1,0},
            {0,1,1,0,0,0,0,0,0,0,0,1,1,0},
            {0,1,1,1,0,0,0,0,0,0,1,1,1,0},
            {0,0,1,1,0,0,0,0,0,0,1,1,0,0},
            {0,0,1,1,1,0,0,0,0,1,1,1,0,0},
            {0,0,0,1,1,1,1,1,1,1,1,0,0,0},
            {0,0,0,0,1,1,1,1,1,1,0,0,0,0},
            {0,0,0,0,0,0,0,0,0,0,0,0,0,0},
            {0,0,0,0,0,0,0,0,0,0,0,0,0,0}
        },
        /* 1 */
        {
            {0,0,0,0,0,0,0,0,0,0,0,0,0,0},
            {0,0,0,0,0,0,0,0,0,0,0,0,0,0},
            {0,0,0,0,0,0,1,1,0,0,0,0,0,0},
            {0,0,0,0,0,1,1,1,0,0,0,0,0,0},
            {0,0,0,0,1,1,1,1,0,0,0,0,0,0},
            {0,0,0,1,1,1,1,1,0,0,0,0,0,0},
            {0,0,1,1,1,0,1,1,0,0,0,0,0,0},
            {0,0,1,1,0,0,1,1,0,0,0,0,0,0},
            {0,0,0,0,0,0,1,1,0,0,0,0,0,0},
            {0,0,0,0,0,0,1,1,0,0,0,0,0,0},
            {0,0,0,0,0,0,1,1,0,0,0,0,0,0},
            {0,0,0,0,0,0,1,1,0,0,0,0,0,0},
            {0,0,0,0,0,0,1,1,0,0,0,0,0,0},
            {0,0,0,0,0,0,1,1,0,0,0,0,0,0},
            {0,0,0,0,0,0,1,1,0,0,0,0,0,0},
            {0,0,0,0,0,0,1,1,0,0,0,0,0,0},
            {0,0,0,0,0,0,1,1,0,0,0,0,0,0},
            {0,0,0,0,0,0,1,1,0,0,0,0,0,0},
            {0,0,0,0,0,0,1,1,0,0,0,0,0,0},
            {0,0,0,0,0,0,1,1,0,0,0,0,0,0},
            {0,0,0,0,0,0,1,1,0,0,0,0,0,0},
            {0,0,0,0,0,0,0,0,0,0,0,0,0,0},
            {0,0,0,0,0,0,0,0,0,0,0,0,0,0}
        },
        /* 2 */
        {
            {0,0,0,0,0,0,0,0,0,0,0,0,0,0},
            {0,0,0,0,0,0,0,0,0,0,0,0,0,0},
            {0,0,0,0,0,1,1,1,1,0,0,0,0,0},
            {0,0,0,1,1,1,1,1,1,1,1,0,0,0},
            {0,0,1,1,1,1,0,0,1,1,1,1,0,0},
            {0,1,1,1,0,0,0,0,0,0,1,1,0,0},
            {0,1,1,0,0,0,0,0,0,0,1,1,1,0},
            {0,0,0,0,0,0,0,0,0,0,0,1,1,0},
            {0,0,0,0,0,0,0,0,0,0,0,1,1,0},
            {0,0,0,0,0,0,0,0,0,0,1,1,1,0},
            {0,0,0,0,0,0,0,0,0,1,1,1,0,0},
            {0,0,0,0,0,0,0,0,1,1,1,0,0,0},
            {0,0,0,0,0,0,0,1,1,1,0,0,0,0},
            {0,0,0,0,0,0,1,1,1,0,0,0,0,0},
            {0,0,0,0,0,1,1,1,0,0,0,0,0,0},
            {0,0,0,0,1,1,1,0,0,0,0,0,0,0},
            {0,0,0,1,1,1,0,0,0,0,0,0,0,0},
            {0,0,1,1,1,0,0,0,0,0,0,0,0,0},
            {0,1,1,1,0,0,0,0,0,0,0,0,0,0},
            {0,1,1,1,1,1,1,1,1,1,1,1,1,0},
            {0,1,1,1,1,1,1,1,1,1,1,1,1,0},
            {0,0,0,0,0,0,0,0,0,0,0,0,0,0},
            {0,0,0,0,0,0,0,0,0,0,0,0,0,0}
        },
        /* 3 */
        {
            {0,0,0,0,0,0,0,0,0,0,0,0,0,0},
            {0,0,0,0,0,0,0,0,0,0,0,0,0,0},
            {0,1,1,1,1,1,1,1,1,1,1,1,1,0},
            {0,1,1,1,1,1,1,1,1,1,1,1,1,0},
            {0,0,0,0,0,0,0,0,0,0,1,1,1,0},
            {0,0,0,0,0,0,0,0,0,1,1,1,0,0},
            {0,0,0,0,0,0,0,0,1,1,1,0,0,0},
            {0,0,0,0,0,0,0,1,1,1,0,0,0,0},
            {0,0,0,0,0,0,1,1,1,0,0,0,0,0},
            {0,0,0,0,0,1,1,1,1,0,0,0,0,0},
            {0,0,0,0,0,1,1,1,1,1,1,0,0,0},
            {0,0,0,0,0,0,0,0,1,1,1,1,0,0},
            {0,0,0,0,0,0,0,0,0,0,1,1,0,0},
            {0,0,0,0,0,0,0,0,0,0,1,1,1,0},
            {0,0,0,0,0,0,0,0,0,0,0,1,1,0},
            {0,0,0,0,0,0,0,0,0,0,0,1,1,0},
            {0,1,1,0,0,0,0,0,0,0,1,1,1,0},
            {0,1,1,1,0,0,0,0,0,0,1,1,0,0},
            {0,0,1,1,1,1,0,0,1,1,1,1,0,0},
            {0,0,0,1,1,1,1,1,1,1,1,0,0,0},
            {0,0,0,0,0,1,1,1,1,0,0,0,0,0},
            {0,0,0,0,0,0,0,0,0,0,0,0,0,0},
            {0,0,0,0,0,0,0,0,0,0,0,0,0,0}
        },
        /* 4 */
        {
            {0,0,0,0,0,0,0,0,0,0,0,0,0,0},
            {0,0,0,0,0,0,0,0,0,0,0,0,0,0},
            {0,0,0,0,0,0,1,1,0,0,0,0,0,0},
            {0,0,0,0,0,0,1,1,0,0,0,0,0,0},
            {0,0,0,0,0,1,1,1,0,0,0,0,0,0},
            {0,0,0,0,0,1,1,0,0,0,0,0,0,0},
            {0,0,0,0,1,1,1,0,0,0,0,0,0,0},
            {0,0,0,0,1,1,0,0,0,0,0,0,0,0},
            {0,0,0,1,1,1,0,0,0,0,0,0,0,0},
            {0,0,0,1,1,0,0,0,1,1,0,0,0,0},
            {0,0,1,1,1,0,0,0,1,1,0,0,0,0},
            {0,0,1,1,0,0,0,0,1,1,0,0,0,0},
            {0,1,1,1,0,0,0,0,1,1,0,0,0,0},
            {0,1,1,1,1,1,1,1,1,1,1,1,1,0},
            {0,1,1,1,1,1,1,1,1,1,1,1,1,0},
            {0,0,0,0,0,0,0,0,1,1,0,0,0,0},
            {0,0,0,0,0,0,0,0,1,1,0,0,0,0},
            {0,0,0,0,0,0,0,0,1,1,0,0,0,0},
            {0,0,0,0,0,0,0,0,1,1,0,0,0,0},
            {0,0,0,0,0,0,0,0,1,1,0,0,0,0},
            {0,0,0,0,0,0,0,0,1,1,0,0,0,0},
            {0,0,0,0,0,0,0,0,0,0,0,0,0,0},
            {0,0,0,0,0,0,0,0,0,0,0,0,0,0}
        },
        /* 5 */
        {
            {0,0,0,0,0,0,0,0,0,0,0,0,0,0},
            {0,0,0,0,0,0,0,0,0,0,0,0,0,0},
            {0,1,1,1,1,1,1,1,1,1,1,1,1,0},
            {0,1,1,1,1,1,1,1,1,1,1,1,1,0},
            {0,1,1,0,0,0,0,0,0,0,0,0,0,0},
            {0,1,1,0,0,0,0,0,0,0,0,0,0,0},
            {0,1,1,0,0,0,0,0,0,0,0,0,0,0},
            {0,1,1,0,0,0,0,0,0,0,0,0,0,0},
            {0,1,1,0,0,0,0,0,0,0,0,0,0,0},
            {0,1,1,1,1,1,1,1,1,0,0,0,0,0},
            {0,1,1,1,1,1,1,1,1,1,1,0,0,0},
            {0,0,0,0,0,0,0,0,1,1,1,1,0,0},
            {0,0,0,0,0,0,0,0,0,0,1,1,0,0},
            {0,0,0,0,0,0,0,0,0,0,1,1,1,0},
            {0,0,0,0,0,0,0,0,0,0,0,1,1,0},
            {0,0,0,0,0,0,0,0,0,0,0,1,1,0},
            {0,1,1,0,0,0,0,0,0,0,1,1,1,0},
            {0,1,1,1,0,0,0,0,0,0,1,1,0,0},
            {0,0,1,1,1,1,0,0,1,1,1,1,0,0},
            {0,0,0,1,1,1,1,1,1,1,1,0,0,0},
            {0,0,0,0,0,1,1,1,1,0,0,0,0,0},
            {0,0,0,0,0,0,0,0,0,0,0,0,0,0},
            {0,0,0,0,0,0,0,0,0,0,0,0,0,0}
        },
        /* 6 */
        {
            {0,0,0,0,0,0,0,0,0,0,0,0,0,0},
            {0,0,0,0,0,0,0,0,0,0,0,0,0,0},
            {0,0,0,0,0,0,0,1,1,0,0,0,0,0},
            {0,0,0,0,0,0,1,1,1,0,0,0,0,0},
            {0,0,0,0,0,1,1,1,0,0,0,0,0,0},
            {0,0,0,0,1,1,1,0,0,0,0,0,0,0},
            {0,0,0,1,1,1,0,0,0,0,0,0,0,0},
            {0,0,0,1,1,0,0,0,0,0,0,0,0,0},
            {0,0,1,1,1,0,0,0,0,0,0,0,0,0},
            {0,0,1,1,0,1,1,1,1,0,0,0,0,0},
            {0,0,1,1,1,1,1,1,1,1,1,0,0,0},
            {0,1,1,1,1,1,0,0,1,1,1,1,0,0},
            {0,1,1,1,0,0,0,0,0,0,1,1,0,0},
            {0,1,1,1,0,0,0,0,0,0,1,1,1,0},
            {0,1,1,0,0,0,0,0,0,0,0,1,1,0},
            {0,1,1,0,0,0,0,0,0,0,0,1,1,0},
            {0,1,1,1,0,0,0,0,0,0,1,1,1,0},
            {0,0,1,1,0,0,0,0,0,0,1,1,0,0},
            {0,0,1,1,1,1,0,0,1,1,1,1,0,0},
            {0,0,0,1,1,1,1,1,1,1,1,0,0,0},
            {0,0,0,0,0,1,1,1,1,0,0,0,0,0},
            {0,0,0,0,0,0,0,0,0,0,0,0,0,0},
            {0,0,0,0,0,0,0,0,0,0,0,0,0,0}
        },
        /* 7 */
        {
            {0,0,0,0,0,0,0,0,0,0,0,0,0,0},
            {0,0,0,0,0,0,0,0,0,0,0,0,0,0},
            {0,1,1,1,1,1,1,1,1,1,1,1,1,0},
            {0,1,1,1,1,1,1,1,1,1,1,1,1,0},
            {0,0,0,0,0,0,0,0,0,0,1,1,1,0},
            {0,0,0,0,0,0,0,0,0,0,1,1,0,0},
            {0,0,0,0,0,0,0,0,0,1,1,1,0,0},
            {0,0,0,0,0,0,0,0,0,1,1,0,0,0},
            {0,0,0,0,0,0,0,0,1,1,1,0,0,0},
            {0,0,0,0,0,0,0,0,1,1,0,0,0,0},
            {0,0,0,0,0,0,0,1,1,1,0,0,0,0},
            {0,0,0,0,0,0,0,1,1,0,0,0,0,0},
            {0,0,0,0,0,0,1,1,1,0,0,0,0,0},
            {0,0,0,0,0,0,1,1,0,0,0,0,0,0},
            {0,0,0,0,0,1,1,1,0,0,0,0,0,0},
            {0,0,0,0,0,1,1,0,0,0,0,0,0,0},
            {0,0,0,0,0,1,1,0,0,0,0,0,0,0},
            {0,0,0,0,1,1,1,0,0,0,0,0,0,0},
            {0,0,0,0,1,1,0,0,0,0,0,0,0,0},
            {0,0,0,0,1,1,0,0,0,0,0,0,0,0},
            {0,0,0,0,1,1,0,0,0,0,0,0,0,0},
            {0,0,0,0,0,0,0,0,0,0,0,0,0,0},
            {0,0,0,0,0,0,0,0,0,0,0,0,0,0}
        },
        /* 8 */
        {
            {0,0,0,0,0,0,0,0,0,0,0,0,0,0},
            {0,0,0,0,0,0,0,0,0,0,0,0,0,0},
            {0,0,0,1,1,1,1,1,1,1,1,0,0,0},
            {0,0,1,1,1,1,1,1,1,1,1,1,0,0},
            {0,1,1,1,0,0,0,0,0,0,1,1,1,0},
            {0,1,1,0,0,0,0,0,0,0,0,1,1,0},
            {0,1,1,0,0,0,0,0,0,0,0,1,1,0},
            {0,1,1,1,0,0,0,0,0,0,1,1,1,0},
            {0,0,1,1,1,0,0,0,0,1,1,1,0,0},
            {0,0,0,1,1,1,0,0,1,1,1,0,0,0},
            {0,0,0,0,1,1,1,1,1,1,0,0,0,0},
            {0,0,0,0,1,1,1,1,1,1,0,0,0,0},
            {0,0,0,1,1,1,0,0,1,1,1,0,0,0},
            {0,0,1,1,1,0,0,0,0,1,1,1,0,0},
            {0,1,1,1,0,0,0,0,0,0,1,1,1,0},
            {0,1,1,0,0,0,0,0,0,0,0,1,1,0},
            {0,1,1,0,0,0,0,0,0,0,0,1,1,0},
            {0,1,1,0,0,0,0,0,0,0,0,1,1,0},
            {0,1,1,1,0,0,0,0,0,0,1,1,1,0},
            {0,0,1,1,1,1,1,1,1,1,1,1,0,0},
            {0,0,0,1,1,1,1,1,1,1,1,0,0,0},
            {0,0,0,0,0,0,0,0,0,0,0,0,0,0},
            {0,0,0,0,0,0,0,0,0,0,0,0,0,0}
        },
        /* 9 */
        {
            {0,0,0,0,0,0,0,0,0,0,0,0,0,0},
            {0,0,0,0,0,0,0,0,0,0,0,0,0,0},
            {0,0,0,0,0,1,1,1,1,0,0,0,0,0},
            {0,0,0,1,1,1,1,1,1,1,1,0,0,0},
            {0,0,1,1,1,1,0,0,1,1,1,1,0,0},
            {0,0,1,1,0,0,0,0,0,0,1,1,0,0},
            {0,1,1,1,0,0,0,0,0,0,1,1,1,0},
            {0,1,1,0,0,0,0,0,0,0,0,1,1,0},
            {0,1,1,0,0,0,0,0,0,0,0,1,1,0},
            {0,1,1,1,0,0,0,0,0,0,1,1,1,0},
            {0,0,1,1,0,0,0,0,0,0,1,1,1,0},
            {0,0,1,1,1,1,0,0,1,1,1,1,1,0},
            {0,0,0,1,1,1,1,1,1,1,1,1,0,0},
            {0,0,0,0,0,1,1,1,1,0,1,1,0,0},
            {0,0,0,0,0,0,0,0,0,1,1,1,0,0},
            {0,0,0,0,0,0,0,0,0,1,1,0,0,0},
            {0,0,0,0,0,0,0,0,1,1,1,0,0,0},
            {0,0,0,0,0,0,0,1,1,1,0,0,0,0},
            {0,0,0,0,0,0,1,1,1,0,0,0,0,0},
            {0,0,0,0,0,1,1,1,0,0,0,0,0,0},
            {0,0,0,0,0,1,1,0,0,0,0,0,0,0},
            {0,0,0,0,0,0,0,0,0,0,0,0,0,0},
            {0,0,0,0,0,0,0,0,0,0,0,0,0,0}
        }
    };

    unsigned int row;

    for (row = 0; row < DIGIT_HEIGHT; ++row) {
        unsigned int col;

        for (col = 0; col < DIGIT_WIDTH; ++col)
            bits[row0 + row][col0 + col] = digits[d][row][col];
    }
}



static unsigned int
rect(bit ** const bits,
     unsigned int const row0,
     unsigned int const col0,
     unsigned int const height,
     unsigned int const width,
     bit          const color) {

    unsigned int row;

    for (row = row0; row < row0 + height; ++row) {
        unsigned int col;

        for (col = col0; col < col0 + width; ++col)
            bits[row][col] = color;
    }
    return col0 + width;
}



static unsigned int
addLines(int          const d,
         bit **       const bits,
         unsigned int const row0,
         unsigned int const startCol,
         unsigned int const height,
         bit          const color) {

    unsigned int col0;

    col0 = startCol;  /* initial value */

    switch (d) {
    case 0:
        col0 = rect(bits, row0, col0, height, LINE3_WIDTH, color);
        col0 = rect(bits, row0, col0, height, LINE2_WIDTH, 1 - color);
        col0 = rect(bits, row0, col0, height, LINE1_WIDTH, color);
        col0 = rect(bits, row0, col0, height, LINE1_WIDTH, 1 - color);
        break;

    case 1:
        col0 = rect(bits, row0, col0, height, LINE2_WIDTH, color);
        col0 = rect(bits, row0, col0, height, LINE2_WIDTH, 1 - color);
        col0 = rect(bits, row0, col0, height, LINE2_WIDTH, color);
        col0 = rect(bits, row0, col0, height, LINE1_WIDTH, 1 - color);
        break;

    case 2:
        col0 = rect(bits, row0, col0, height, LINE2_WIDTH, color);
        col0 = rect(bits, row0, col0, height, LINE1_WIDTH, 1 - color);
        col0 = rect(bits, row0, col0, height, LINE2_WIDTH, color);
        col0 = rect(bits, row0, col0, height, LINE2_WIDTH, 1 - color);
        break;

    case 3:
        col0 = rect(bits, row0, col0, height, LINE1_WIDTH, color);
        col0 = rect(bits, row0, col0, height, LINE4_WIDTH, 1 - color);
        col0 = rect(bits, row0, col0, height, LINE1_WIDTH, color);
        col0 = rect(bits, row0, col0, height, LINE1_WIDTH, 1 - color);
        break;

    case 4:
        col0 = rect(bits, row0, col0, height, LINE1_WIDTH, color);
        col0 = rect(bits, row0, col0, height, LINE1_WIDTH, 1 - color);
        col0 = rect(bits, row0, col0, height, LINE3_WIDTH, color);
        col0 = rect(bits, row0, col0, height, LINE2_WIDTH, 1 - color);
        break;

    case 5:
        col0 = rect(bits, row0, col0, height, LINE1_WIDTH, color);
        col0 = rect(bits, row0, col0, height, LINE2_WIDTH, 1 - color);
        col0 = rect(bits, row0, col0, height, LINE3_WIDTH, color);
        col0 = rect(bits, row0, col0, height, LINE1_WIDTH, 1 - color);
        break;

    case 6:
        col0 = rect(bits, row0, col0, height, LINE1_WIDTH, color);
        col0 = rect(bits, row0, col0, height, LINE1_WIDTH, 1 - color);
        col0 = rect(bits, row0, col0, height, LINE1_WIDTH, color);
        col0 = rect(bits, row0, col0, height, LINE4_WIDTH, 1 - color);
        break;

    case 7:
        col0 = rect(bits, row0, col0, height, LINE1_WIDTH, color);
        col0 = rect(bits, row0, col0, height, LINE3_WIDTH, 1 - color);
        col0 = rect(bits, row0, col0, height, LINE1_WIDTH, color);
        col0 = rect(bits, row0, col0, height, LINE2_WIDTH, 1 - color);
        break;

    case 8:
        col0 = rect(bits, row0, col0, height, LINE1_WIDTH, color);
        col0 = rect(bits, row0, col0, height, LINE2_WIDTH, 1 - color);
        col0 = rect(bits, row0, col0, height, LINE1_WIDTH, color);
        col0 = rect(bits, row0, col0, height, LINE3_WIDTH, 1 - color);
        break;

    case 9:
        col0 = rect(bits, row0, col0, height, LINE3_WIDTH, color);
        col0 = rect(bits, row0, col0, height, LINE1_WIDTH, 1 - color);
        col0 = rect(bits, row0, col0, height, LINE1_WIDTH, color);
        col0 = rect(bits, row0, col0, height, LINE2_WIDTH, 1 - color);
        break;

    default:
        pm_error("INTERNAL ERROR: invalid digit passed to 'addlines'");
    }

    return col0;
}



int
main(int argc, const char ** argv) {

    const char* const usage = "[-s1|-s2] <type> <manufac> <product>";

    bit ** bits;
    int argn, style, rows, cols, row, digrow, col, digcolofs;
    const char * typecode;
    const char * manufcode;
    const char * prodcode;
    int sum, p, lc0, lc1, lc2, lc3, lc4, rc0, rc1, rc2, rc3, rc4;

    pm_proginit(&argc, argv);

    argn = 1;
    style = 1;

    /* Check for flags. */
    while ( argn < argc && argv[argn][0] == '-' && argv[argn][1] != '\0' )
        {
        if ( pm_keymatch( argv[argn], "-s1", 3 ) )
            style = 1;
        else if ( pm_keymatch( argv[argn], "-s2", 3 ) )
            style = 2;
        else
            pm_usage( usage );
        argn++;
        }

    if ( argn + 3 < argc )
        pm_usage( usage );
    typecode = argv[argn];
    manufcode = argv[argn + 1];
    prodcode = argv[argn + 2];
    argn += 3;

    if ( argn != argc )
        pm_usage( usage );

    if ( strlen( typecode ) != 1 || ( ! alldig( typecode ) ) ||
         strlen( manufcode ) != 5 || ( ! alldig ( manufcode ) ) ||
         strlen( prodcode ) != 5 || ( ! alldig ( prodcode ) ) )
        pm_error(
            "type code must be one digit, and\n    manufacturer and product codes must be five digits" );
    p = typecode[0] - '0';
    lc0 = manufcode[0] - '0';
    lc1 = manufcode[1] - '0';
    lc2 = manufcode[2] - '0';
    lc3 = manufcode[3] - '0';
    lc4 = manufcode[4] - '0';
    rc0 = prodcode[0] - '0';
    rc1 = prodcode[1] - '0';
    rc2 = prodcode[2] - '0';
    rc3 = prodcode[3] - '0';
    rc4 = prodcode[4] - '0';
    sum = (10 -
           (((p + lc1 + lc3 + rc0 + rc2 + rc4 ) * 3 +
             lc0 + lc2 + lc4 + rc1 + rc3)
            % 10)
        )
        % 10;

    rows = 2 * MARGIN + SHORT_HEIGHT + DIGIT_HEIGHT;
    cols = 2 * MARGIN + 12 * LINES_WIDTH + 11 * LINE1_WIDTH;
    bits = pbm_allocarray(cols, rows);

    rect(bits, 0, 0, rows, cols, PBM_WHITE);

    row = MARGIN;
    digrow = row + SHORT_HEIGHT;
    col = MARGIN;
    digcolofs = (LINES_WIDTH - DIGIT_WIDTH) / 2;

    if (style == 1)
        putDigit(p, bits, digrow, col - DIGIT_WIDTH - LINE1_WIDTH);
    else if (style == 2)
        putDigit(
            p, bits, row + SHORT_HEIGHT / 2, col - DIGIT_WIDTH - LINE1_WIDTH);

    col = rect(bits, row, col, TALL_HEIGHT, LINE1_WIDTH, PBM_BLACK);
    col = rect(bits, row, col, TALL_HEIGHT, LINE1_WIDTH, PBM_WHITE);
    col = rect(bits, row, col, TALL_HEIGHT, LINE1_WIDTH, PBM_BLACK);
    col = addLines(p, bits, row, col, TALL_HEIGHT, PBM_WHITE);
    putDigit(lc0, bits, digrow, col + digcolofs);
    col = addLines(lc0, bits, row, col, SHORT_HEIGHT, PBM_WHITE);
    putDigit(lc1, bits, digrow, col + digcolofs);
    col = addLines(lc1, bits, row, col, SHORT_HEIGHT, PBM_WHITE);
    putDigit(lc2, bits, digrow, col + digcolofs);
    col = addLines(lc2, bits, row, col, SHORT_HEIGHT, PBM_WHITE);
    putDigit(lc3, bits, digrow, col + digcolofs);
    col = addLines(lc3, bits, row, col, SHORT_HEIGHT, PBM_WHITE);
    putDigit(lc4, bits, digrow, col + digcolofs);
    col = addLines(lc4, bits, row, col, SHORT_HEIGHT, PBM_WHITE);
    col = rect(bits, row, col, TALL_HEIGHT, LINE1_WIDTH, PBM_WHITE);
    col = rect(bits, row, col, TALL_HEIGHT, LINE1_WIDTH, PBM_BLACK);
    col = rect(bits, row, col, TALL_HEIGHT, LINE1_WIDTH, PBM_WHITE);
    col = rect(bits, row, col, TALL_HEIGHT, LINE1_WIDTH, PBM_BLACK);
    col = rect(bits, row, col, TALL_HEIGHT, LINE1_WIDTH, PBM_WHITE);
    putDigit(rc0, bits, digrow, col + digcolofs);
    col = addLines(rc0, bits, row, col, SHORT_HEIGHT, PBM_BLACK);
    putDigit(rc1, bits, digrow, col + digcolofs);
    col = addLines(rc1, bits, row, col, SHORT_HEIGHT, PBM_BLACK);
    putDigit(rc2, bits, digrow, col + digcolofs);
    col = addLines(rc2, bits, row, col, SHORT_HEIGHT, PBM_BLACK);
    putDigit(rc3, bits, digrow, col + digcolofs);
    col = addLines(rc3, bits, row, col, SHORT_HEIGHT, PBM_BLACK);
    putDigit(rc4, bits, digrow, col + digcolofs);
    col = addLines(rc4, bits, row, col, SHORT_HEIGHT, PBM_BLACK);
    col = addLines(sum, bits, row, col, TALL_HEIGHT, PBM_BLACK);
    col = rect(bits, row, col, TALL_HEIGHT, LINE1_WIDTH, PBM_BLACK);
    col = rect(bits, row, col, TALL_HEIGHT, LINE1_WIDTH, PBM_WHITE);
    col = rect(bits, row, col, TALL_HEIGHT, LINE1_WIDTH, PBM_BLACK);
    if (style == 1)
        putDigit(sum, bits, digrow, col + LINE1_WIDTH);

    pbm_writepbm(stdout, bits, cols, rows, 0);

    pm_close(stdout );

    exit(0);
}



