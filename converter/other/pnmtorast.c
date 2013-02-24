/* pnmtorast.c - read a portable anymap and produce a Sun rasterfile
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

#include "pnm.h"
#include "rast.h"
#include "mallocvar.h"

#define MAXCOLORS 256



static colormap_t *
allocPrColormap(void) {

    colormap_t * prColormapP;

    MALLOCVAR(prColormapP);
    if (prColormapP == NULL)
        pm_error("out of memory");
    prColormapP->type = RMT_EQUAL_RGB;
    prColormapP->length = MAXCOLORS;
    MALLOCARRAY(prColormapP->map[0], MAXCOLORS);
    MALLOCARRAY(prColormapP->map[1], MAXCOLORS);
    MALLOCARRAY(prColormapP->map[2], MAXCOLORS);
    if (prColormapP->map[0] == NULL || 
        prColormapP->map[1] == NULL ||
        prColormapP->map[2] == NULL)
        pm_error("out of memory");

    return prColormapP;
}



static colormap_t *
makePrColormap(colorhist_vector const chv,
               unsigned int     const colors) {

    colormap_t * prColormapP;
    unsigned int i;

    prColormapP = allocPrColormap();

    for (i = 0; i < colors; ++i) {
        prColormapP->map[0][i] = PPM_GETR(chv[i].color);
        prColormapP->map[1][i] = PPM_GETG(chv[i].color);
        prColormapP->map[2][i] = PPM_GETB(chv[i].color);
    }
    for ( ; i < MAXCOLORS; ++i) {
        prColormapP->map[0][i] = 0;
        prColormapP->map[1][i] = 0;
        prColormapP->map[2][i] = 0;
    }
    return prColormapP;
}



static colormap_t *
makeGrayPrColormap(void) {

    colormap_t * prColormapP;
    unsigned int i;

    prColormapP = allocPrColormap();

    for (i = 0; i < MAXCOLORS; ++i) {
        prColormapP->map[0][i] = i;
        prColormapP->map[1][i] = i;
        prColormapP->map[2][i] = i;
    }

    return prColormapP;
}



static void
doRowDepth1(const xel *     const xelrow,
            unsigned char * const rastRow,
            unsigned int    const cols,
            int             const format,
            xelval          const maxval,
            colorhash_table const cht) {
                
    unsigned int col;
    int bitcount;
    unsigned char * byteP;

    byteP = rastRow;

    *byteP = 0;
    bitcount = 7;

    for (col = 0; col < cols; ++col) {
        switch (PNM_FORMAT_TYPE(format)) {
        case PPM_TYPE: {
            xel adjustedXel;
            int color;
            if (maxval != 255)
                PPM_DEPTH(adjustedXel, xelrow[col], maxval, 255 );
            color = ppm_lookupcolor(cht, &adjustedXel);
            if (color == -1)
                pm_error("color not found?!?  "
                         "col=%u  r=%u g=%u b=%u",
                         col,
                         PPM_GETR(adjustedXel),
                         PPM_GETG(adjustedXel),
                         PPM_GETB(adjustedXel));
            if (color)
                *byteP |= 1 << bitcount;
        } break;

        default: {
            int const color = PNM_GET1(xelrow[col]);
            if (!color)
                *byteP |= 1 << bitcount;
            break;
        }
        }
        --bitcount;
        if (bitcount < 0) {
            ++byteP;
            *byteP = 0;
            bitcount = 7;
        }
    }
}



static void
doRowDepth8(const xel *     const xelrow,
            unsigned char * const rastRow,
            unsigned int    const cols,
            int             const format,
            xelval          const maxval,
            colorhash_table const cht) {

    unsigned int col;
    unsigned char * byteP;

    byteP = rastRow;

    for (col = 0; col < cols; ++col) {
        int color;

        switch (PNM_FORMAT_TYPE(format)) {
        case PPM_TYPE: {
            xel adjustedXel;

            if (maxval == 255)
                adjustedXel = xelrow[col];
            else
                PPM_DEPTH(adjustedXel, xelrow[col], maxval, 255);

            color = ppm_lookupcolor(cht, &adjustedXel);
            if (color == -1)
                pm_error("color not found?!?  "
                         "col=%u  r=%u g=%u b=%u",
                         col,
                         PPM_GETR(adjustedXel),
                         PPM_GETG(adjustedXel),
                         PPM_GETB(adjustedXel));
        } break;

        case PGM_TYPE: {
            int const rawColor = PNM_GET1(xelrow[col]);

            color = maxval == 255 ? rawColor : rawColor * 255 / maxval;

        } break;

        default:
            color = PNM_GET1(xelrow[col]);
        }
        *byteP++ = color;
    }
}




static void
doRowDepth24(const xel *     const xelrow,
             unsigned char * const rastRow,
             unsigned int    const cols,
             int             const format,
             xelval          const maxval) {

    /* Since depth is 24, we do NOT have a valid cht. */

    unsigned char * byteP;
    unsigned int col;

    byteP = rastRow;

    for (col = 0; col < cols; ++col) {
        xel adjustedXel;

        if (maxval == 255)
            adjustedXel = xelrow[col];
        else
            PPM_DEPTH(adjustedXel, xelrow[col], maxval, 255);

        *byteP++ = PPM_GETB(adjustedXel);
        *byteP++ = PPM_GETG(adjustedXel);
        *byteP++ = PPM_GETR(adjustedXel);
    }
}



int
main(int argc, const char ** argv) {

    FILE * ifP;
    xel ** xels;
    xel * xelrow;
    xel p;
    colorhist_vector chv;
    colorhash_table cht;
    colormap_t * prColormapP;
    int argn;
    int prType;
    int rows, cols;
    int format;
    unsigned int depth;
    int colorCt;
    unsigned int row;
    xelval maxval;
    struct pixrect * prP;
    unsigned char * rastRow;
    const char * const usage = "[-standard|-rle] [pnmfile]";

    pm_proginit(&argc, argv);

    argn = 1;
    prType = RT_BYTE_ENCODED;

    while ( argn < argc && argv[argn][0] == '-' && argv[argn][1] != '\0' )
    {
        if ( pm_keymatch( argv[argn], "-standard", 2 ) )
            prType = RT_STANDARD;
        else if ( pm_keymatch( argv[argn], "-rle", 2 ) )
            prType = RT_BYTE_ENCODED;
        else
            pm_usage( usage );
        ++argn;
    }

    if ( argn != argc )
    {
        ifP = pm_openr( argv[argn] );
        ++argn;
    }
    else
        ifP = stdin;

    if ( argn != argc )
        pm_usage( usage );

    xels = pnm_readpnm(ifP, &cols, &rows, &maxval, &format);

    pm_close(ifP);

    /* Figure out the proper depth and colormap. */
    switch (PNM_FORMAT_TYPE(format)) {
    case PPM_TYPE:
        pm_message("computing colormap...");
        chv = ppm_computecolorhist(xels, cols, rows, MAXCOLORS, &colorCt);
        if (!chv) {
            pm_message(
                "Too many colors - proceeding to write a 24-bit non-mapped");
            pm_message(
                "rasterfile.  If you want 8 bits, try doing a 'pnmquant %d'.",
                MAXCOLORS);
            depth = 24;
            prType = RT_STANDARD;
            prColormapP = NULL;
        } else {
            pm_message("%u colors found", colorCt);

            if (maxval != 255) {
                unsigned int i;
                for (i = 0; i < colorCt; ++i)
                    PPM_DEPTH(chv[i].color, chv[i].color, maxval, 255);
            }
            /* Force white to slot 0 and black to slot 1, if possible. */
            PPM_ASSIGN(p, 255, 255, 255);
            ppm_addtocolorhist(chv, &colorCt, MAXCOLORS, &p, 0, 0);
            PPM_ASSIGN(p, 0, 0, 0);
            ppm_addtocolorhist(chv, &colorCt, MAXCOLORS, &p, 0, 1);

            if (colorCt == 2) {
                /* Monochrome */
                depth = 1;
                prColormapP = NULL;
            } else {
                /* Turn the ppm colormap into the appropriate Sun colormap. */
                depth = 8;
                prColormapP = makePrColormap(chv, colorCt);
            }
            cht = ppm_colorhisttocolorhash(chv, colorCt);
            ppm_freecolorhist(chv);
        }

        break;

    case PGM_TYPE:
        depth = 8;
        prColormapP = makeGrayPrColormap();
        break;

    default:
        depth = 1;
        prColormapP = NULL;
        break;
    }

    if (maxval > 255 && depth != 1)
        pm_message(
            "maxval is not 255 - automatically rescaling colors");
    
    /* Allocate space for the Sun-format image. */
    prP = mem_create(cols, rows, depth);
    if (!prP)
        pm_error("unable to create new pixrect");

    /* And compute the Sun image.  The variables at this point are:
       cht is null or not.  depth is 1, 8, or 24
    */
    for (row = 0, rastRow = prP->pr_data->md_image; row < rows; ++row) {
        xelrow = xels[row];
        switch (depth) {
        case 1:
            doRowDepth1(xelrow, rastRow, cols, format, maxval, cht);
            break;
        case 8:
            doRowDepth8(xelrow, rastRow, cols, format, maxval, cht);
            break;
        case 24:
            doRowDepth24(xelrow, rastRow, cols, format, maxval);
            break;
        default:
            pm_error("INTERNAL ERROR: impossible depth %u", depth);
        }
        rastRow += prP->pr_data->md_linebytes;
    }
    pnm_freearray(xels, rows);

    {
        int rc;

        rc = pr_dump(prP, stdout, prColormapP, prType, 0);
        if (rc == PIX_ERR )
            pm_error("error writing rasterfile");
    }
    return 0;
}

