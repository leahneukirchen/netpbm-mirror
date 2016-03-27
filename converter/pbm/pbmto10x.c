/* pbmto10x.c - read a portable bitmap and produce a Gemini 10X printer file
**
** Copyright (C) 1990, 1994 by Ken Yap
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
**
** Modified to shorten stripes and eliminate blank stripes. Dec 1994.
*/

#include <stdbool.h>

#include "pbm.h"
#include "mallocvar.h"

#define LOW_RES_ROWS    8       /* printed per pass */
#define HIGH_RES_ROWS   16      /* printed per pass */



static void
outstripe(char * const stripe, 
          char * const sP, 
          int    const reschar) {

    char * p;

    p = sP;  /* initial value */

    /* scan backwards, removing empty columns */
    while (p != stripe) 
        if (*--p != 0) {
            ++p;
            break;
        }

    {
        unsigned int const ncols = p - stripe;

        if (ncols > 0) {
            printf("\033%c%c%c", reschar, ncols % 256, ncols / 256);
            fwrite(stripe, sizeof(char), ncols, stdout);
        }
    }
    putchar('\n');          /* flush buffer */
}



static void
res_60x72(FILE * const ifP,
          int    const rows,
          int    const cols,
          int    const format) {

    int row;
    unsigned int i;
    bit * bitrows[LOW_RES_ROWS];
    char *stripe;
    char *sP;

    MALLOCARRAY(stripe, cols);
    if (stripe == NULL)
        pm_error("Unable to allocate %u bytes for a stripe buffer.",
                 (unsigned)(cols * sizeof(stripe[0])));

    for (i = 0; i < LOW_RES_ROWS; ++i)
        bitrows[i] = pbm_allocrow(cols);

    printf("\033A\010");        /* '\n' = 8/72 */

    for (row = 0, sP = stripe; row < rows; row += LOW_RES_ROWS, sP = stripe) {
        unsigned int col;
        unsigned int i;
        unsigned int npins;
        bit * bP[LOW_RES_ROWS];

        if (row + LOW_RES_ROWS <= rows)
            npins = LOW_RES_ROWS;
        else
            npins = rows - row;

        for (i = 0; i < npins; ++i)
            pbm_readpbmrow(ifP, bP[i] = bitrows[i], cols, format);

        for (col = 0; col < cols; ++col) {
            unsigned int item;

            item = 0;
            for (i = 0; i < npins; ++i)
                if (*(bP[i]++) == PBM_BLACK)
                    item |= 1 << (7 - i);
            *sP++ = item;
        }
        outstripe(stripe, sP, 'K');
    }
    printf("\033@");

    for (i = 0; i < LOW_RES_ROWS; ++i)
        pbm_freerow(bitrows[i]);

    free(stripe);
}



static void
res_120x144(FILE * const ifP,
            int    const rows,
            int    const cols,
            int    const format) {

    unsigned int i;
    int row;
    char *stripe;
    char * sP;
    bit * bitrows[HIGH_RES_ROWS];

    MALLOCARRAY(stripe, cols);
    if (stripe == NULL)
        pm_error("Unable to allocate %u bytes for a stripe buffer.",
                 (unsigned)(cols * sizeof(stripe[0])));

    for (i = 0; i < HIGH_RES_ROWS; ++i)
        bitrows[i] = pbm_allocrow(cols);

    printf("\0333\001");            /* \n = 1/144" */

    for (row = 0, sP = stripe; row < rows; row += HIGH_RES_ROWS, sP = stripe) {
        unsigned int i;
        unsigned int col;
        bit * bP[HIGH_RES_ROWS];
        unsigned int npins;

        if (row + HIGH_RES_ROWS <= rows)
            npins = HIGH_RES_ROWS;
        else
            npins = rows - row;
        for (i = 0; i < npins; ++i)
            pbm_readpbmrow(ifP, bP[i] = bitrows[i], cols, format);
        for (col = 0; col < cols; ++col) {
            unsigned int pin;
            unsigned int item;
            item = 0;
            /* even rows */
            for (pin = i = 0; i < npins; i += 2, ++pin)
                if (*(bP[i]++) == PBM_BLACK)
                    item |= 1 << (7 - pin);
            *sP++ = item;
        }
        outstripe(stripe, sP, 'L');
        for (col = 0, sP = stripe; col < cols; ++col) {
            unsigned int pin;
            unsigned int item;
            item = 0;
            /* odd rows */
            for (i = 1, pin = 0; i < npins; i += 2, ++pin)
                if (*(bP[i]++) == PBM_BLACK)
                    item |= 1 << (7 - pin);
            *sP++ = item;
        }
        outstripe(stripe, sP, 'L');
        printf("\033J\016");        /* 14/144 down, \n did 1/144 */
    }
    printf("\033@");

    for (i = 0; i < LOW_RES_ROWS; ++i)
        pbm_freerow(bitrows[i]);

    free(stripe);
}



int
main(int argc, const char ** argv) {

    const char * fname;
    static FILE * ifP;
    int rows, cols, format;

    bool isHighRes;

    pm_proginit(&argc, argv);

    isHighRes = false;  /* initial assumption */
    if (argc > 1 && argv[1][0] == '-' && argv[1][1] == 'h') {
        isHighRes = true;
        --argc;
        ++argv;
    }
    if (argc-1 > 1)
        pm_error("Too many arguments.  Only argument is file name");
    else if (argc-1 == 1)
        fname = argv[1];
    else
        fname = "-";
    
    ifP = pm_openr(fname);

    pbm_readpbminit(ifP, &cols, &rows, &format);

    if (isHighRes)
        res_120x144(ifP, rows, cols, format);
    else
        res_60x72(ifP, rows, cols, format);

    pm_close(ifP);

    return 0;
}



