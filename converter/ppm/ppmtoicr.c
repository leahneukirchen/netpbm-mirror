/* ppmtoicr.c - convert a portable pixmap to NCSA ICR protocol
**
** Copyright (C) 1990 by Kanthan Pillay (svpillay@Princeton.EDU)
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
#include "ppm.h"

#define MAXCOLORCT 256
#define CLUTCOLORCT 768



static void
makeIcrColormap(colorhist_vector const chv,
                unsigned int     const colorCt,
                pixval           const maxval,
                char *           const rgb) {

    unsigned int i;

    if (maxval > 255)
        pm_message("Maxval is not 255 - automatically rescaling colors" );

    for (i = 0; i < CLUTCOLORCT; ++i)
        rgb[i] = 0;

    for (i = 0; i < colorCt; ++i) {
        unsigned int j;

        j = (3 * i);

        if (maxval == 255) {
            rgb[j++] = PPM_GETR(chv[i].color) ;
            rgb[j++] = PPM_GETG(chv[i].color) ;
            rgb[j++] = PPM_GETB(chv[i].color) ;
        } else {
            rgb[j++] = (unsigned int) PPM_GETR(chv[i].color) * 255 / maxval;
            rgb[j++] = (unsigned int) PPM_GETG(chv[i].color) * 255 / maxval;
            rgb[j++] = (unsigned int) PPM_GETB(chv[i].color) * 255 / maxval;
        }
    }
}



static int
colorIndexAtPosition(unsigned int    const x,
                     unsigned int    const y,
                     pixel **        const pixels,
                     colorhash_table const cht) {

    int rc;

    rc = ppm_lookupcolor(cht, &pixels[y][x]);

    /* Every color in the image is in the palette */
    assert(rc >= 0);

    return rc;
}



static void
downloadColormap(char         const rgb[CLUTCOLORCT],
                 const char * const windowName) {

    unsigned int i;

    pm_message("Downloading colormap for %s ...", windowName);

    printf("\033^M;%d;%d;%d;%s^",
           0, MAXCOLORCT, CLUTCOLORCT, windowName);

    for (i = 0; i < CLUTCOLORCT; ++i) {
        unsigned char const c = rgb[i];

        if (c > 31 && c < 123) {
            /* printable ASCII */
            putchar(c);
        } else {
            /* non-printable, so encode it */
            putchar((c >> 6) + 123);
            putchar((c & 0x3f) + 32);
        }
    }
    fflush(stdout);
}



static void
sendOutPicture(pixel **        const pixels,
               unsigned int    const rows,
               unsigned int    const cols,
               colorhash_table const cht,
               int             const expand,
               const char *    const windowName) {

    unsigned int row;

    pm_message("Sending picture data ..." );

    for (row = 0; row < rows; ++row) {
        unsigned int col;
        printf("\033^P;%d;%d;%d;%d;%s^",
               0, row * expand, expand, cols, windowName);
        for (col = 0; col < cols; ++col) {
            unsigned char const c =
                colorIndexAtPosition(col, row, pixels, cht);
            if (c > 31 && c < 123) {
                putchar(c);
            } else {
                putchar((c >> 6) + 123);
                putchar((c & 0x3f) + 32);
            }
        }
    }
    fflush(stdout);
}



int
main(int argc, const char ** const argv) {

    FILE * ifP;
    int rows, cols;
    int colorCt;
    int argn;
    pixval maxval;
    colorhist_vector chv;
    char rgb[CLUTCOLORCT];
    const char * windowName;
    int display, expand;
    int winflag;
    const char* const usage = "[-windowname windowname] [-expand expand] [-display display] [ppmfile]";
    pixel** pixels;
    colorhash_table cht;

    pm_proginit(&argc, argv);

    argn = 1;
    windowName = "untitled";
    winflag = 0;
    expand = 1;
    display = 0;

    while ( argn < argc && argv[argn][0] == '-' && argv[argn][1] != '\0' )
    {
        if ( pm_keymatch(argv[argn],"-windowname",2) && argn + 1 < argc )
        {
            ++argn;
            windowName = argv[argn];
            winflag = 1;
        }
        else if ( pm_keymatch(argv[argn],"-expand",2) && argn + 1 < argc )
        {
            ++argn;
            if ( sscanf( argv[argn], "%d",&expand ) != 1 )
                pm_usage( usage );
        }
        else if ( pm_keymatch(argv[argn],"-display",2) && argn + 1 < argc )
        {
            ++argn;
            if ( sscanf( argv[argn], "%d",&display ) != 1 )
                pm_usage( usage );
        }
        else
            pm_usage( usage );
    }

    if ( argn < argc )
    {
        ifP = pm_openr( argv[argn] );
        if ( ! winflag )
            windowName = argv[argn];
        ++argn;
    }
    else
        ifP = stdin;

    if ( argn != argc )
        pm_usage( usage );

    pixels = ppm_readppm(ifP, &cols, &rows, &maxval);

    pm_close(ifP);

    /* Figure out the colormap. */
    pm_message("Computing colormap..." );
    chv = ppm_computecolorhist(pixels, cols, rows, MAXCOLORCT, &colorCt);
    if (!chv)
        pm_error("Too many colors - try doing a 'pnmquant %u'", MAXCOLORCT);
    pm_message("%u colors found", colorCt );

    makeIcrColormap(chv, colorCt, maxval, rgb);

    /* And make a hash table for fast lookup. */
    cht = ppm_colorhisttocolorhash(chv, colorCt);

    ppm_freecolorhist(chv);

    /************** Create a new window using ICR protocol *********/
    /* Format is "ESC^W;left;top;width;height;display;windowname"  */

    pm_message("Creating window %s ...", windowName);

    printf("\033^W;%d;%d;%d;%d;%d;%s^",
           0, 0, cols * expand, rows * expand, display, windowName);
    fflush(stdout);

    /****************** Download the colormap.  ********************/

    downloadColormap(rgb, windowName);

    sendOutPicture(pixels, rows, cols, cht, expand, windowName);

    return 0;
}



