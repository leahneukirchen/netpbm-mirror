/* ppmtosix.c - read a PPM and produce a color sixel file
**
** Copyright (C) 1991 by Rick Vinci.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
**
** "-7bit" option added August 2023 by Scott Pakin <scott+pbm@pakin.org>.
*/

#include "ppm.h"

static unsigned int const SIXEL_MAXVAL = 100;
static unsigned int const MAXCOLORCT = 256;

static const char * const sixel = "@ACGO_";

/* Named escape sequences */
static struct {
  const char * DCS;   /* Device Control String */
  const char * ST;    /* String Terminator */
  const char * CSI;   /* Control String Introducer */
  const char * ESC;   /* Escape character */
} eseqs;



static pixel** pixels;   /* stored ppm pixmap input */
static colorhash_table cht;


int margin;



static void
initEscapeSequences(const int nbits) {

    if (nbits == 8) {
        eseqs.DCS = "\220";
        eseqs.ST  = "\234";
        eseqs.CSI = "\233";
        eseqs.ESC = "\033";
    } else if (nbits == 7) {
        eseqs.DCS = "\033P";
        eseqs.ST  = "\033\\";
        eseqs.CSI = "\033[";
        eseqs.ESC = "\033";
    } else
        pm_error("internal error: bad bit count");
}



static void
writePackedImage(colorhash_table const cht,
                 unsigned int    const rows,
                 unsigned int    const cols) {

    unsigned int row;

    for (row = 0; row < rows; ++row) {

        unsigned int const b = row % 6;

        unsigned int repeatCt;
        unsigned int col;

        repeatCt = 1;

        for (col = 0; col < cols; ++col) {
            const pixel * const thisRow = pixels[row];

            unsigned int const thisColorIdx =
                ppm_lookupcolor(cht, &thisRow[col]);

            if (col == cols - 1)   /* last pixel in row */
                if (repeatCt == 1)
                    printf("#%d%c", thisColorIdx, sixel[b]);
                else
                    printf("#%d!%d%c", thisColorIdx, repeatCt, sixel[b]);
            else {  /* not last pixel in row */
                unsigned int const nextColorIdx =
                    ppm_lookupcolor(cht, &thisRow[col+1]);
                if (thisColorIdx == nextColorIdx)
                    ++repeatCt;
                else {
                    if (repeatCt == 1)
                        printf("#%d%c", thisColorIdx, sixel[b]);
                    else {
                        printf("#%d!%d%c", thisColorIdx, repeatCt, sixel[b]);
                        repeatCt = 1;
                    }
                }
            }
        }
        printf( "$\n" );   /* Carriage Return */

        if (b == 5)
            printf("-\n");   /* Line Feed (one sixel height) */
    }
}



static void
writeHeader() {

    if (margin == 1)
        printf("%s%d;%ds", eseqs.CSI, 14, 72);

    printf("%s", eseqs.DCS);  /* start with Device Control String */

    printf("0;0;8q");   /* Horizontal Grid Size at 1/90" and graphics On */

    printf("\"1;1\n");  /* set aspect ratio 1:1 */
}



static void
writeColorMap(colorhist_vector const chv,
              unsigned int     const colorCt,
              pixval           const maxval) {

    unsigned int colorIdx;

    for (colorIdx = 0; colorIdx < colorCt ; ++colorIdx) {
        pixel const p = chv[colorIdx].color;

        pixel scaledColor;

        if (maxval == SIXEL_MAXVAL)
            scaledColor = p;
        else
            PPM_DEPTH(scaledColor, p, maxval, SIXEL_MAXVAL);

        printf("#%d;2;%d;%d;%d", colorIdx,
               PPM_GETR(scaledColor),
               PPM_GETG(scaledColor),
               PPM_GETB(scaledColor));
    }
    printf("\n");
}



static void
writeRawImage(colorhash_table const cht,
              unsigned int    const rows,
              unsigned int    const cols) {

    unsigned int row;

    for (row = 0; row < rows; ++row) {
        pixel * const thisRow = pixels[row];
        unsigned int const b = row % 6;

        unsigned int col;

        for (col = 0; col < cols; ++col)
            printf("#%d%c", ppm_lookupcolor(cht, &thisRow[col]), sixel[b]);

        printf("$\n");   /* Carriage Return */

        if (b == 5)
            printf("-\n");   /* Line Feed (one sixel height) */
    }
}



static void
writeEnd() {

    if (margin == 1)
        printf ("%s%d;%ds", eseqs.CSI, 1, 80);

    printf("%s\n", eseqs.ST);
}



int
main(int argc, const char ** argv) {

    FILE * ifp;
    int colorCt;
    int argn, rows, cols;
    int raw;
    int nbits;
    pixval maxval;
    colorhist_vector chv;
    const char* const usage = "[-raw] [-margin] [ppmfile]";

    pm_proginit(&argc, argv);

    argn = 1;
    raw = 0;
    margin = 0;
    nbits = 8;

    /* Parse args. */
    while ( argn < argc && argv[argn][0] == '-' && argv[argn][1] != '\0' )
        {
        if ( pm_keymatch( argv[argn], "-raw", 2 ) )
            raw = 1;
        else if ( pm_keymatch( argv[argn], "-margin", 2 ) )
            margin = 1;
        else if ( pm_keymatch( argv[argn], "-7bit", 2 ) )
            nbits = 7;
        else
            pm_usage( usage );
        ++argn;
        }

    if ( argn < argc )
        {
        ifp = pm_openr( argv[argn] );
        ++argn;
        }
    else
        ifp = stdin;

    if ( argn != argc )
        pm_usage( usage );

    /* Read in the whole ppmfile. */
    pixels = ppm_readppm(ifp, &cols, &rows, &maxval);
    pm_close(ifp);

    /* Print a warning if we could lose accuracy when rescaling colors. */
    if (maxval > SIXEL_MAXVAL)
        pm_message(
            "maxval of input is not the sixel maxval (%u) - "
            "automatically rescaling colors", SIXEL_MAXVAL);

    /* Figure out the colormap. */
    pm_message("computing colormap...");
    chv = ppm_computecolorhist( pixels, cols, rows, MAXCOLORCT, &colorCt);
    if (chv == (colorhist_vector) 0)
        pm_error("too many colors - try 'pnmquant %u'", MAXCOLORCT);

    pm_message("%d colors found", colorCt);

    /* Make a hash table for fast color lookup. */
    cht = ppm_colorhisttocolorhash(chv, colorCt);

    initEscapeSequences(nbits);
    writeHeader();
    writeColorMap(chv, colorCt, maxval);
    if (raw == 1)
        writeRawImage(cht, rows, cols);
    else
        writePackedImage(cht, rows, cols);
    writeEnd();

    /* If the program failed, it previously aborted with nonzero completion
       code, via various function calls.
    */
    return 0;
}



