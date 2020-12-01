/*
These functions were taken from Ingo Wilken's ilbm package by Bryan
Henderson on 01.03.10.  Because ppmtoilbm and ilbmtoppm are the only
programs that will use these in the foreseeable future, they remain
lightly documented and tested.

But they look like they would be useful in other Netpbm programs that
do Floyd-Steinberg.
*/



/* libfloyd.c - generic Floyd-Steinberg error distribution routines for PBMPlus
**
** Copyright (C) 1994 Ingo Wilken (Ingo.Wilken@informatik.uni-oldenburg.de)
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

#include "netpbm/mallocvar.h"
#include "ppm.h"
#include "ppmfloyd.h"



static void
fs_adjust(ppm_fs_info * const fi,
          int           const col) {

    int     const errcol = col+1;
    pixel * const pP     = &(fi->pixrow[col]);
    pixval  const maxval = fi->maxval;

    long r, g, b;

    /* Use Floyd-Steinberg errors to adjust actual color. */
    r = fi->thisrederr  [errcol]; if( r < 0 ) r -= 8; else r += 8; r /= 16;
    g = fi->thisgreenerr[errcol]; if( g < 0 ) g -= 8; else g += 8; g /= 16;
    b = fi->thisblueerr [errcol]; if( b < 0 ) b -= 8; else b += 8; b /= 16;

    r += PPM_GETR(*pP); if ( r < 0 ) r = 0; else if ( r > maxval ) r = maxval;
    g += PPM_GETG(*pP); if ( g < 0 ) g = 0; else if ( g > maxval ) g = maxval;
    b += PPM_GETB(*pP); if ( b < 0 ) b = 0; else if ( b > maxval ) b = maxval;

    PPM_ASSIGN(*pP, r, g, b);
    fi->red = r; fi->green = g; fi->blue = b;
}



static ppm_fs_info *
allocateFi(int const cols) {

    ppm_fs_info * fi;

    MALLOCVAR(fi);

    if (fi != NULL) {
        MALLOCARRAY(fi->thisrederr  , cols + 2);
        MALLOCARRAY(fi->thisgreenerr, cols + 2);
        MALLOCARRAY(fi->thisblueerr , cols + 2);
        MALLOCARRAY(fi->nextrederr  , cols + 2);
        MALLOCARRAY(fi->nextgreenerr, cols + 2);
        MALLOCARRAY(fi->nextblueerr , cols + 2);

        if (fi->thisrederr   == NULL ||
            fi->thisgreenerr == NULL ||
            fi->thisblueerr  == NULL ||
            fi->nextrederr   == NULL ||
            fi->nextgreenerr == NULL ||
            fi->nextblueerr  == NULL)
            pm_error("out of memory allocating "
                     "Floyd-Steinberg control structure");
    } else
        pm_error("out of memory allocating Floyd-Steinberg control structure");

    return(fi);
}



ppm_fs_info *
ppm_fs_init(unsigned int const cols,
            pixval       const maxval,
            unsigned int const flags) {

    ppm_fs_info * fiP;

    fiP = allocateFi(cols);

    fiP->lefttoright = 1;
    fiP->cols        = cols;
    fiP->maxval      = maxval;
    fiP->flags       = flags;

    if (flags & FS_RANDOMINIT) {
        unsigned int i;
        srand(pm_randseed());
        for (i = 0; i < cols +2; ++i) {
            /* random errors in [-1..+1] */
            fiP->thisrederr[i]   = rand() % 32 - 16;
            fiP->thisgreenerr[i] = rand() % 32 - 16;
            fiP->thisblueerr[i]  = rand() % 32 - 16;
        }
    } else {
        unsigned int i;

        for (i = 0; i < cols + 2; ++i)
            fiP->thisrederr[i] = fiP->thisgreenerr[i] =
                fiP->thisblueerr[i] = 0;
    }
    return fiP;
}



void
ppm_fs_free(ppm_fs_info * const fiP) {

    if (fiP) {
        free(fiP->thisrederr); free(fiP->thisgreenerr); free(fiP->thisblueerr);
        free(fiP->nextrederr); free(fiP->nextgreenerr); free(fiP->nextblueerr);
        free(fiP);
    }
}



int
ppm_fs_startrow(ppm_fs_info * const fiP,
                pixel *       const pixrow) {

    int retval;

    if (!fiP)
        retval = 0;
    else {
        unsigned int col;

        fiP->pixrow = pixrow;

        for (col = 0; col < fiP->cols + 2; ++col) {
            fiP->nextrederr  [col] = 0;
            fiP->nextgreenerr[col] = 0;
            fiP->nextblueerr [col] = 0;
        }

        if(fiP->lefttoright) {
            fiP->col_end = fiP->cols;
            col = 0;
        } else {
            fiP->col_end = -1;
            col = fiP->cols - 1;
        }
        fs_adjust(fiP, col);

        retval = col;
    }
    return retval;
}



int
ppm_fs_next(ppm_fs_info * const fiP,
            int           const startCol) {

    int col;

    col = startCol;  /* initial value */

    if (!fiP)
        ++col;
    else {
        if (fiP->lefttoright)
            ++col;
        else
            --col;
        if (col == fiP->col_end)
            col = fiP->cols;
        else
            fs_adjust(fiP, col);
    }
    return col;
}



void
ppm_fs_endrow(ppm_fs_info * const fiP) {

    if (fiP) {
        {
            long * const tmp = fiP->thisrederr;
            fiP->thisrederr = fiP->nextrederr;
            fiP->nextrederr = tmp;
        }
        {
            long * const tmp = fiP->thisgreenerr;
            fiP->thisgreenerr = fiP->nextgreenerr;
            fiP->nextgreenerr = tmp;
        }
        {
            long * const tmp = fiP->thisblueerr;
            fiP->thisblueerr = fiP->nextblueerr;
            fiP->nextblueerr = tmp;
        }
        if (fiP->flags & FS_ALTERNATE)
            fiP->lefttoright = !fiP->lefttoright;
    }
}



void
ppm_fs_update(ppm_fs_info * const fiP,
              int           const col,
              pixel *       const pP) {

    if (fiP)
        ppm_fs_update3(fiP, col, PPM_GETR(*pP), PPM_GETG(*pP), PPM_GETB(*pP));
}



void
ppm_fs_update3(ppm_fs_info * const fiP,
               int           const col,
               pixval        const r,
               pixval        const g,
               pixval        const b) {

    int const errcol = col + 1;

    if (fiP) {
        long const rerr = (long)(fiP->red)   - (long)r;
        long const gerr = (long)(fiP->green) - (long)g;
        long const berr = (long)(fiP->blue)  - (long)b;

        if ( fiP->lefttoright ) {
            {
                long const two_err = 2*rerr;

                long err;

                err = rerr;     fiP->nextrederr[errcol+1] += err;    /* 1/16 */
                err += two_err; fiP->nextrederr[errcol-1] += err;    /* 3/16 */
                err += two_err; fiP->nextrederr[errcol  ] += err;    /* 5/16 */
                err += two_err; fiP->thisrederr[errcol+1] += err;    /* 7/16 */
            }
            {
                long const two_err = 2*gerr;

                long err;

                err = gerr;     fiP->nextgreenerr[errcol+1] += err;  /* 1/16 */
                err += two_err; fiP->nextgreenerr[errcol-1] += err;  /* 3/16 */
                err += two_err; fiP->nextgreenerr[errcol  ] += err;  /* 5/16 */
                err += two_err; fiP->thisgreenerr[errcol+1] += err;  /* 7/16 */
            }
            {
                long const two_err = 2*berr;

                long err;

                err = berr;     fiP->nextblueerr[errcol+1] += err;  /* 1/16 */
                err += two_err; fiP->nextblueerr[errcol-1] += err;  /* 3/16 */
                err += two_err; fiP->nextblueerr[errcol  ] += err;  /* 5/16 */
                err += two_err; fiP->thisblueerr[errcol+1] += err;  /* 7/16 */
            }
        } else {
            {
                long const two_err = 2*rerr;

                long err;

                err = rerr;     fiP->nextrederr[errcol-1] += err;    /* 1/16 */
                err += two_err; fiP->nextrederr[errcol+1] += err;    /* 3/16 */
                err += two_err; fiP->nextrederr[errcol  ] += err;    /* 5/16 */
                err += two_err; fiP->thisrederr[errcol-1] += err;    /* 7/16 */
            }
            {
                long const two_err = 2*gerr;

                long err;

                err = gerr;     fiP->nextgreenerr[errcol-1] += err;  /* 1/16 */
                err += two_err; fiP->nextgreenerr[errcol+1] += err;  /* 3/16 */
                err += two_err; fiP->nextgreenerr[errcol  ] += err;  /* 5/16 */
                err += two_err; fiP->thisgreenerr[errcol-1] += err;  /* 7/16 */
            }
            {
                long const two_err = 2*berr;

                long err;

                err = berr;     fiP->nextblueerr[errcol-1] += err;  /* 1/16 */
                err += two_err; fiP->nextblueerr[errcol+1] += err;  /* 3/16 */
                err += two_err; fiP->nextblueerr[errcol  ] += err;  /* 5/16 */
                err += two_err; fiP->thisblueerr[errcol-1] += err;  /* 7/16 */
            }
        }
    }
}



