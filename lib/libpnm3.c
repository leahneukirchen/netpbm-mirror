/* libpnm3.c - pnm utility library part 3
**
** Copyright (C) 1989, 1991 by Jef Poskanzer.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

#include <stdbool.h>
#include <assert.h>

#include "pnm.h"
#include "ppm.h"
#include "pgm.h"
#include "pbm.h"



static xel
mean4(int const format,
      xel const a,
      xel const b,
      xel const c,
      xel const d) {
/*----------------------------------------------------------------------------
   Return cartesian mean of the 4 colors.
-----------------------------------------------------------------------------*/
    xel retval;

    switch (PNM_FORMAT_TYPE(format)) {
    case PPM_TYPE:
        PPM_ASSIGN(
            retval,
            (PPM_GETR(a) + PPM_GETR(b) + PPM_GETR(c) + PPM_GETR(d)) / 4,
            (PPM_GETG(a) + PPM_GETG(b) + PPM_GETG(c) + PPM_GETG(d)) / 4,
            (PPM_GETB(a) + PPM_GETB(b) + PPM_GETB(c) + PPM_GETB(d)) / 4);
        break;

    case PGM_TYPE:
    case PBM_TYPE:
        PNM_ASSIGN1(
            retval,
            (PNM_GET1(a) + PNM_GET1(b) + PNM_GET1(c) + PNM_GET1(d))/4);
        break;

    default:
        pm_error("Invalid format passed to pnm_backgroundxel()");
    }
    return retval;
}



xel
pnm_backgroundxel(xel**  const xels,
                  int    const cols,
                  int    const rows,
                  xelval const maxval,
                  int    const format) {

    xel bgxel, ul, ur, ll, lr;

    /* Guess a good background value. */
    ul = xels[0][0];
    ur = xels[0][cols-1];
    ll = xels[rows-1][0];
    lr = xels[rows-1][cols-1];

    /* We first recognize three corners equal.  If not, we look for any
       two.  If not, we just average all four.
    */
    if (PNM_EQUAL(ul, ur) && PNM_EQUAL(ur, ll))
        bgxel = ul;
    else if (PNM_EQUAL(ul, ur) && PNM_EQUAL(ur, lr))
        bgxel = ul;
    else if (PNM_EQUAL(ul, ll) && PNM_EQUAL(ll, lr))
        bgxel = ul;
    else if (PNM_EQUAL(ur, ll) && PNM_EQUAL(ll, lr))
        bgxel = ur;
    else if (PNM_EQUAL(ul, ur))
        bgxel = ul;
    else if (PNM_EQUAL(ul, ll))
        bgxel = ul;
    else if (PNM_EQUAL(ul, lr))
        bgxel = ul;
    else if (PNM_EQUAL(ur, ll))
        bgxel = ur;
    else if (PNM_EQUAL(ur, lr))
        bgxel = ur;
    else if (PNM_EQUAL(ll, lr))
        bgxel = ll;
    else
        bgxel = mean4(format, ul, ur, ll, lr);

    return bgxel;
}



xel
pnm_backgroundxelrow(xel *  const xelrow,
                     int    const cols,
                     xelval const maxval,
                     int    const format) {
/*----------------------------------------------------------------------------
   Guess a good background color for an image that contains row 'xelrow'
   (probably top or bottom edge).

   'cols', 'maxval', and 'format' describe 'xelrow'.
-----------------------------------------------------------------------------*/
    xel bgxel, l, r;

    l = xelrow[0];
    r = xelrow[cols-1];

    if (PNM_EQUAL(l, r))
        /* Both corners are same color, so that's the background color,
           without any extra computation.
        */
        bgxel = l;
    else {
        /* Corners are different, so use cartesian mean of them */
        switch (PNM_FORMAT_TYPE(format)) {
        case PPM_TYPE:
            PPM_ASSIGN(bgxel,
                       (PPM_GETR(l) + PPM_GETR(r)) / 2,
                       (PPM_GETG(l) + PPM_GETG(r)) / 2,
                       (PPM_GETB(l) + PPM_GETB(r)) / 2
                );
            break;

        case PGM_TYPE:
            PNM_ASSIGN1(bgxel, (PNM_GET1(l) + PNM_GET1(r)) / 2);
            break;

        case PBM_TYPE: {
            unsigned int col, blackCnt;

            /* One black, one white.  Gotta count. */
            for (col = 0, blackCnt = 0; col < cols; ++col) {
                if (PNM_GET1(xelrow[col] ) == 0)
                    ++blackCnt;
            }
            if (blackCnt >= cols / 2)
                PNM_ASSIGN1(bgxel, 0);
            else
                PNM_ASSIGN1(bgxel, maxval);
            break;
        }

        default:
            pm_error("Invalid format passed to pnm_backgroundxelrow()");
        }
    }

    return bgxel;
}



xel
pnm_whitexel(xelval const maxval,
             int    const format) {

    xel retval;

    switch (PNM_FORMAT_TYPE(format)) {
    case PPM_TYPE:
        PPM_ASSIGN(retval, maxval, maxval, maxval);
        break;

    case PGM_TYPE:
    case PBM_TYPE:
        PNM_ASSIGN1(retval, maxval);
        break;

    default:
        pm_error("Invalid format %d passed to pnm_whitexel()", format);
    }

    return retval;
}



xel
pnm_blackxel(xelval const maxval,
             int    const format) {

    xel retval;

    switch (PNM_FORMAT_TYPE(format)) {
    case PPM_TYPE:
        PPM_ASSIGN(retval, 0, 0, 0);
        break;

    case PGM_TYPE:
    case PBM_TYPE:
        PNM_ASSIGN1(retval, 0);
        break;

    default:
        pm_error("Invalid format %d passed to pnm_blackxel()", format);
    }

    return retval;
}



void
pnm_invertxel(xel*   const xP,
              xelval const maxval,
              int    const format) {

    switch (PNM_FORMAT_TYPE(format)) {
    case PPM_TYPE:
        PPM_ASSIGN(*xP,
                   maxval - PPM_GETR(*xP),
                   maxval - PPM_GETG(*xP),
                   maxval - PPM_GETB(*xP));
        break;

    case PGM_TYPE:
        PNM_ASSIGN1(*xP, maxval - PNM_GET1(*xP));
        break;

    case PBM_TYPE:
        PNM_ASSIGN1(*xP, (PNM_GET1(*xP) == 0) ? maxval : 0);
        break;

    default:
        pm_error("Invalid format passed to pnm_invertxel()");
    }
}



const char *
pnm_formattypenm(int const format) {

    switch (PPM_FORMAT_TYPE(format)) {
    case PPM_TYPE: return "PPM"; break;
    case PGM_TYPE: return "PGM"; break;
    case PBM_TYPE: return "PBM"; break;
    default: return "???";
    }
}



void
pnm_promoteformat(xel ** const xels,
                  int    const cols,
                  int    const rows,
                  xelval const maxval,
                  int    const format,
                  xelval const newmaxval,
                  int    const newformat) {

    unsigned int row;

    for (row = 0; row < rows; ++row)
        pnm_promoteformatrow(
            xels[row], cols, maxval, format, newmaxval, newformat);
}



void
pnm_promoteformatrow(xel *  const xelrow,
                     int    const cols,
                     xelval const maxval,
                     int    const format,
                     xelval const newmaxval,
                     int    const newformat) {

    if ((PNM_FORMAT_TYPE(format) == PPM_TYPE &&
         (PNM_FORMAT_TYPE(newformat) == PGM_TYPE ||
          PNM_FORMAT_TYPE(newformat) == PBM_TYPE)) ||
        (PNM_FORMAT_TYPE(format) == PGM_TYPE &&
         PNM_FORMAT_TYPE(newformat) == PBM_TYPE)) {

        pm_error( "pnm_promoteformatrow: can't promote downwards!" );
    } else if (PNM_FORMAT_TYPE(format) == PNM_FORMAT_TYPE(newformat)) {
        /* We're promoting to the same type - but not necessarily maxval */
        if (PNM_FORMAT_TYPE(format) == PBM_TYPE) {
            /* PBM doesn't have maxval, so this is idempotent */
        } else if (newmaxval < maxval)
            pm_error("pnm_promoteformatrow: can't decrease maxval - "
                     "try using pamdepth");
        else if (newmaxval == maxval) {
            /* Same type, same maxval => idempotent function */
        } else {
            /* Increase maxval. */
            switch (PNM_FORMAT_TYPE(format)) {
            case PGM_TYPE: {
                unsigned int col;
                for (col = 0; col < cols; ++col)
                    PNM_ASSIGN1(xelrow[col],
                                PNM_GET1(xelrow[col]) * newmaxval / maxval);
            } break;

            case PPM_TYPE: {
                unsigned int col;
                for (col = 0; col < cols; ++col)
                    PPM_DEPTH(xelrow[col], xelrow[col], maxval, newmaxval);
            } break;

            default:
                pm_error("Invalid old format passed to "
                         "pnm_promoteformatrow()");
            }
        }
    } else {
        /* Promote to a higher type. */
        switch (PNM_FORMAT_TYPE(format)) {
        case PBM_TYPE:
            switch (PNM_FORMAT_TYPE(newformat)) {
            case PGM_TYPE: {
                unsigned int col;
                for (col = 0; col < cols; ++col) {
                    if (PNM_GET1(xelrow[col]) == 0)
                        PNM_ASSIGN1(xelrow[col], 0);
                    else
                        PNM_ASSIGN1(xelrow[col], newmaxval);
                }
            } break;

            case PPM_TYPE: {
                unsigned int col;
                for (col = 0; col < cols; ++col) {
                    if (PNM_GET1(xelrow[col]) == 0)
                        PPM_ASSIGN(xelrow[col], 0, 0, 0);
                    else
                        PPM_ASSIGN(xelrow[col],
                                   newmaxval, newmaxval, newmaxval );
                }
            } break;

            default:
                pm_error("Invalid new format passed to "
                         "pnm_promoteformatrow()");
            }
            break;

        case PGM_TYPE:
            switch (PNM_FORMAT_TYPE(newformat)) {
            case PPM_TYPE:
                if (newmaxval < maxval)
                    pm_error("pnm_promoteformatrow: can't decrease maxval - "
                             "try using pamdepth");
                else if (newmaxval == maxval) {
                    unsigned int col;
                    for (col = 0; col < cols; ++col) {
                        PPM_ASSIGN(xelrow[col],
                                   PNM_GET1(xelrow[col]),
                                   PNM_GET1(xelrow[col]),
                                   PNM_GET1(xelrow[col]));
                    }
                } else {
                    /* Increase maxval. */
                    unsigned int col;
                    for (col = 0; col < cols; ++col) {
                        PPM_ASSIGN(xelrow[col],
                                   PNM_GET1(xelrow[col]) * newmaxval / maxval,
                                   PNM_GET1(xelrow[col]) * newmaxval / maxval,
                                   PNM_GET1(xelrow[col]) * newmaxval / maxval);
                    }
                }
                break;

            default:
                pm_error("Invalid new format passed to "
                         "pnm_promoteformatrow()");
            }
            break;

        default:
            pm_error("Invalid old format passed to pnm_promoteformatrow()");
        }
    }
}



pixel
pnm_xeltopixel(xel const inputXel,
               int const format) {

    pixel outputPixel;

    switch (PNM_FORMAT_TYPE(format)) {
    case PPM_TYPE:
        PPM_ASSIGN(outputPixel,
                   PNM_GETR(inputXel),
                   PNM_GETG(inputXel),
                   PNM_GETB(inputXel));
        break;
    case PGM_TYPE:
    case PBM_TYPE:
        PPM_ASSIGN(outputPixel,
                   PNM_GET1(inputXel),
                   PNM_GET1(inputXel),
                   PNM_GET1(inputXel));
        break;
    default:
        pm_error("Invalid format code %d passed to pnm_xeltopixel()",
                 format);
    }

    return outputPixel;
}



xel
pnm_pixeltoxel(pixel const inputPixel) {

    return inputPixel;
}



xel
pnm_graytoxel(gray const inputGray) {

    xel outputXel;

    PNM_ASSIGN1(outputXel, inputGray);

    return outputXel;
}


xel
pnm_bittoxel(bit    const inputBit,
             xelval const maxval) {

    switch (inputBit) {
    case PBM_BLACK: return pnm_blackxel(maxval, PBM_TYPE); break;
    case PBM_WHITE: return pnm_whitexel(maxval, PBM_TYPE); break;
    default:
        assert(false);
    }
}



xel
pnm_parsecolorxel(const char * const colorName,
                  xelval       const maxval,
                  int          const format) {

    pixel const bgColor = ppm_parsecolor(colorName, maxval);

    xel retval;

    switch(PNM_FORMAT_TYPE(format)) {
    case PPM_TYPE:
        PNM_ASSIGN(retval,
                   PPM_GETR(bgColor), PPM_GETG(bgColor), PPM_GETB(bgColor));
        break;
    case PGM_TYPE:
        if (PPM_ISGRAY(bgColor))
            PNM_ASSIGN1(retval, PPM_GETB(bgColor));
        else
            pm_error("Non-gray color '%s' specified for a "
                     "grayscale (PGM) image",
                     colorName);
                   break;
    case PBM_TYPE:
        if (PPM_EQUAL(bgColor, ppm_whitepixel(maxval)))
            PNM_ASSIGN1(retval, maxval);
        else if (PPM_EQUAL(bgColor, ppm_blackpixel()))
            PNM_ASSIGN1(retval, 0);
        else
            pm_error ("Color '%s', which is neither black nor white, "
                      "specified for a black and white (PBM) image",
                      colorName);
        break;
    default:
        pm_error("Invalid format code %d passed to pnm_parsecolorxel()",
                 format);
    }

    return retval;
}
