/* ppmtolj.c - convert a portable pixmap to an HP PCL 5 color image
**
** Copyright (C) 2000 by Jonathan Melvin (jonathan.melvin@heywood.co.uk)
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

#include <stdbool.h>
#include <string.h>

#include "ppm.h"

#define C_RESET                 "\033E"
#define C_PRESENTATION          "\033*r%dF"
#define C_PRESENTATION_LOGICAL  0
#define C_PRESENTATION_PHYSICAL 3
#define C_GAMMA                 "\033*t%dI"
#define C_IMAGE_WIDTH           "\033*r%dS"
#define C_IMAGE_HEIGHT          "\033*r%dT"
#define C_DATA_PLANES           "\033*r%dU"
#define C_TRANS_MODE            "\033*b%dM"
#define C_TRANS_MODE_STD        0 /*compression modes*/
#define C_TRANS_MODE_RLE        1 /*no good for rgb*/
#define C_TRANS_MODE_TIFF       2 /*no good for rgb*/
#define C_TRANS_MODE_DELTA      3 /*only on to use for rgb values*/
#define C_CONFIG_IMAGE_DATA     "\033*v6W"
#define C_SEND_ROW              "\033*b%dW"
#define C_BEGIN_RASTER          "\033*r%dA"
#define C_BEGIN_RASTER_CUR      1
#define C_END_RASTER            "\033*r%dC"
#define C_END_RASTER_UNUSED     0
#define C_RESOLUTION            "\033*t%dR"
#define C_RESOLUTION_300DPI     300
#define C_MOVE_X                "\033*p+%dX"
#define C_MOVE_Y                "\033*p+%dY"
#define C_LEFT_MARGIN           "\033*r%dA"
#define C_Y_OFFSET              "\033*b%dY"



static void
printHeader(FILE *       const ofP,
            int          const resets,
            bool         const floating,
            int          const resolution,
            unsigned int const rows,
            unsigned int const cols,
            int          const mode,
            int          const gamma) {

    char const CID[6] =  { 0, 3, 0, 8, 8, 8 };
        /*data for the configure image data command*/

    if (resets & 0x1) {
        /* Printer reset. */
        fprintf(ofP, C_RESET);
    }

    if (!floating) {
        /* Ensure top margin is zero */
        fprintf(ofP, "\033&l0E");
    }

    /*Set Presentation mode*/
    fprintf(ofP, C_PRESENTATION, C_PRESENTATION_PHYSICAL);
    /* Set the resolution */
    fprintf(ofP, C_RESOLUTION, resolution);
    /* Set raster height*/
    fprintf(ofP, C_IMAGE_HEIGHT, rows);
    /* Set raster width*/
    fprintf(ofP, C_IMAGE_WIDTH, cols);
    /* set left margin to current x pos*/
    /*(void) fprintf(ofP, C_LEFT_MARGIN, 1);*/
    /* set the correct color mode */
    fprintf(ofP, C_CONFIG_IMAGE_DATA);
    fwrite(CID, 1, 6, ofP);
    /* Start raster graphics */
    fprintf(ofP, C_BEGIN_RASTER, C_BEGIN_RASTER_CUR);  /*posscale);*/
    /* set Y offset to 0 */
    fprintf(ofP, C_Y_OFFSET, 0);
/*
    if (xoff)
        fprintf(ofP, C_MOVE_X, xoff);
    if (yoff)
        fprintf(ofP, C_MOVE_Y, yoff);
*/
    /* Set raster compression */
    fprintf(ofP, C_TRANS_MODE, mode);

    if (gamma)
        fprintf(ofP, C_GAMMA,   gamma);
}



static int
compressRowDelta(unsigned char * const op,
                 unsigned char * const prevOp,
                 unsigned char * const cp,
                 unsigned int    const bufsize) {
/*----------------------------------------------------------------------------

  delta encoding.


  op:      row buffer
  prevOp:  previous row buffer
  bufsize: length of row
  cp:       buffer for compressed data
-----------------------------------------------------------------------------*/
    int burstStart, burstEnd, burstCode, ptr, skipped, code;
    bool mustBurst;
    int deltaBufferIndex;

    if (memcmp(op, prevOp , bufsize/*rowBufferIndex*/) == 0)
        return 0; /* exact match, no deltas required */

    deltaBufferIndex = 0;  /* initial value */
    ptr = 0;               /* initial value */
    skipped = 0;           /* initial value */
    burstStart = -1;       /* initial value */
    burstEnd = -1;         /* initial value */
    mustBurst = false;     /* initial value */

    while (ptr < bufsize/*rowBufferIndex*/) {
        bool mustSkip;

        mustSkip = false;  /* initial assumption */

        if (ptr == 0 || skipped == 30 || op[ptr] != prevOp[ptr] ||
            (burstStart != -1 && ptr == bufsize - 1)) {
            /* we want to output this byte... */
            if (burstStart == -1) {
                burstStart = ptr;
            }
            if (ptr - burstStart == 7 || ptr == bufsize - 1) {
                /* we have to output it now... */
                burstEnd = ptr;
                mustBurst = true;
            }
        } else {
            /* duplicate byte, we can skip it */
            if (burstStart != -1)
            {
                burstEnd = ptr - 1;
                mustBurst = true;
            }
            mustSkip = true;
        }
        if (mustBurst) {
            burstCode = burstEnd - burstStart; /* 0-7 means 1-8 bytes follow */
            code = (burstCode << 5) | skipped;
            cp[deltaBufferIndex++] = (char) code;
            memcpy(cp + deltaBufferIndex, op + burstStart, burstCode + 1);
            deltaBufferIndex += burstCode + 1;
            burstStart = -1;
            burstEnd = -1;
            mustBurst = false;
            skipped = 0;
        }
        if (mustSkip)
            ++skipped;
        ++ptr;
    }
    return deltaBufferIndex;
}



static void
printTrailer(FILE * const ofP,
             int    const resets) {

    fprintf(ofP, C_END_RASTER, C_END_RASTER_UNUSED);

    if (resets & 0x2) {
        /* Printer reset. */
        fprintf(ofP, C_RESET);
    }
}



static void
printRaster(FILE *       const ifP,
            unsigned int const rows,
            unsigned int const cols,
            int          const maxval,
            int          const format,
            int          const mode,
            FILE *       const ofP) {

    unsigned char * obuf; /* malloc'ed */
    unsigned char * cbuf; /* malloc'ed */
    unsigned char * previousObuf;  /* malloc'ed */
    unsigned int uncompOutRowSz;
    unsigned int outRowSz;
    int currentmode;
    pixel * pixelrow;
    unsigned int row;

    pixelrow = ppm_allocrow(cols);

    obuf = (unsigned char *) pm_allocrow(cols * 3, sizeof(unsigned char));
    cbuf = (unsigned char *) pm_allocrow(cols * 6, sizeof(unsigned char));

    if (mode == C_TRANS_MODE_DELTA) {
        previousObuf =
            (unsigned char *) pm_allocrow(cols * 3, sizeof(unsigned char));
        memset(previousObuf, 0, cols * 3);
    } else
        previousObuf = NULL;

    currentmode = mode;  /* initial value */

    for (row = 0; row < rows; ++row) {
        unsigned int col;
        const unsigned char * op;

        ppm_readppmrow(ifP, pixelrow, cols, maxval, format);

        /* get a row of data with 3 bytes per pixel */
        for (col = 0; col < cols; ++col) {
            obuf[3*col + 0] = (PPM_GETR(pixelrow[col])*255)/maxval;
            obuf[3*col + 1] = (PPM_GETG(pixelrow[col])*255)/maxval;
            obuf[3*col + 2] = (PPM_GETB(pixelrow[col])*255)/maxval;
        }
        uncompOutRowSz = 3 * cols;

        /*compress the row if required*/
        switch (mode) {
        case C_TRANS_MODE_STD: /*no compression*/
            op = obuf;
            outRowSz = uncompOutRowSz;
            break;
        case C_TRANS_MODE_DELTA: {   /*delta compression*/
            int const deltasize =
                compressRowDelta(obuf, previousObuf, cbuf, uncompOutRowSz);

            int newmode;

            newmode = 0;  /* initial value */

            if (deltasize >= uncompOutRowSz) {
                /*normal is best?*/
                op = obuf;
                outRowSz = uncompOutRowSz;
            } else {
                /*delta is best*/
                op = cbuf;
                outRowSz = deltasize;
                newmode = C_TRANS_MODE_DELTA;
            }
            memcpy(previousObuf, obuf, cols*3);

            if (currentmode != newmode) {
                fprintf(ofP, C_TRANS_MODE, newmode);
                currentmode = newmode;
            }
        }
        }
        fprintf(ofP, C_SEND_ROW, outRowSz);
        fwrite(op, 1, outRowSz, ofP);
    }
    free(cbuf);
    free(obuf);
    if (previousObuf)
        free(previousObuf);
    ppm_freerow(pixelrow);
}



int
main(int argc, const char **argv) {
    FILE * ifP;
    int argn, rows, cols;
    pixval maxval;
    int format;
    int gamma;
    int mode;
    bool floating;
        /* suppress the ``ESC & l 0 E'' ? */
    int resets;
        /* bit mask for when to emit printer reset seq */
    int resolution;

    const char * const usage = "[-noreset][-float][-delta][-gamma <val>] [-resolution N] "
        "[ppmfile]\n\tresolution = [75|100|150|300|600] (dpi)";

    pm_proginit(&argc, argv);

    gamma = 0; /* initial value */
    mode = C_TRANS_MODE_STD;  /* initial value */
    resolution = C_RESOLUTION_300DPI;  /* initial value */
    floating = false;  /* initial value */
    resets = 0x3;  /* initial value */
    argn = 1;
    while ( argn < argc && argv[argn][0] == '-' && argv[argn][1] != '\0' )
    {
        if( pm_keymatch( argv[argn], "-resolution", 2 ) && argn + 1 < argc )
        {
            ++argn;
            if ( argn == argc || sscanf( argv[argn], "%d", &resolution ) != 1 )
                pm_usage( usage );
        }
        else if ( pm_keymatch(argv[argn],"-gamma",2) && argn + 1 < argc )
        {
            ++argn;
            if ( sscanf( argv[argn], "%d",&gamma ) != 1 )
                pm_usage( usage );
        }
        else if (pm_keymatch(argv[argn],"-delta",2))
            mode = C_TRANS_MODE_DELTA;
        else if (pm_keymatch(argv[argn],"-float",2))
            floating = true;
        else if (pm_keymatch(argv[argn],"-noreset",2))
            resets = 0x0;

        else
            pm_usage( usage );
        ++argn;
    }

    if ( argn < argc )
    {
        ifP = pm_openr( argv[argn] );
        ++argn;
    }
    else
        ifP = stdin;

    if ( argn != argc )
        pm_usage( usage );

    ppm_readppminit(ifP, &cols, &rows, &maxval, &format);

    printHeader(stdout, resets, floating, resolution, rows, cols,
                mode, gamma);

    printRaster(ifP, rows, cols, maxval, format, mode, stdout);

    printTrailer(stdout, resets);

    pm_close(ifP);

    return 0;
}



