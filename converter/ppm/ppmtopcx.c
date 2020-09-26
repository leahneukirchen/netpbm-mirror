/* ppmtopcx.c - convert a portable pixmap to PCX
**
** Copyright (C) 1994 by Ingo Wilken (Ingo.Wilken@informatik.uni-oldenburg.de)
** based on ppmtopcx.c by Michael Davidson
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
**
** 11/Dec/94: first version
** 12/Dec/94: added handling of "packed" format (16 colors or less)
**
** ZSoft PCX File Format Technical Reference Manual
** http://bespin.org/~qz/pc-gpe/pcx.txt
** http://web.archive.org/web/20100206055706/http://www.qzx.com/pc-gpe/pcx.txt
*/
#include <assert.h>

#include "pm_c_util.h"
#include "ppm.h"
#include "shhopt.h"
#include "mallocvar.h"

#define MAXCOLORS       256

#define PCX_MAGIC       0x0a            /* PCX magic number             */
#define PCX_256_COLORS  0x0c            /* magic number for 256 colors  */
#define PCX_MAXVAL      (pixval)255


struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFilespec;  /* '-' if stdin */
    unsigned int truecolor;   /* -24bit option */
    unsigned int use8Bit; /* -8bit option */
    unsigned int planes;    /* zero means minimum */
    unsigned int packed;
    unsigned int verbose;
    unsigned int stdpalette;
    const char * palette;   /* NULL means none */
    int xpos;
    int ypos;
};



struct PcxCmapEntry {
    unsigned char r;
    unsigned char g;
    unsigned char b;
};

static struct PcxCmapEntry
pcxCmapEntryFromPixel(pixel const colorPixel) {

    struct PcxCmapEntry retval;

    retval.r = PPM_GETR(colorPixel);
    retval.g = PPM_GETG(colorPixel);
    retval.b = PPM_GETB(colorPixel);

    return retval;
}



static void
parseCommandLine(int argc, const char ** argv,
                 struct CmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   parse program command line described in Unix standard form by argc
   and argv.  Return the information in the options as *cmdlineP.

   If command line is internally inconsistent (invalid options, etc.),
   issue error message to stderr and abort program.

   Note that the strings we return are stored in the storage that
   was passed to us as the argv array.  We also trash *argv.
-----------------------------------------------------------------------------*/
    optEntry * option_def;
        /* Instructions to pm_optParseOptions3 on how to parse our options.
         */
    optStruct3 opt;

    unsigned int option_def_index;

    unsigned int planesSpec, xposSpec, yposSpec, paletteSpec;

    MALLOCARRAY(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0, "24bit",     OPT_FLAG,   NULL,
            &cmdlineP->truecolor,    0);
    OPTENT3(0, "8bit",      OPT_FLAG,   NULL,
            &cmdlineP->use8Bit,    0);
    OPTENT3(0, "planes",    OPT_UINT,   &cmdlineP->planes,
            &planesSpec,             0);
    OPTENT3(0, "packed",    OPT_FLAG,   NULL,
            &cmdlineP->packed,       0);
    OPTENT3(0, "verbose",   OPT_FLAG,   NULL,
            &cmdlineP->verbose,      0);
    OPTENT3(0, "stdpalette", OPT_FLAG,  NULL,
            &cmdlineP->stdpalette,   0);
    OPTENT3(0, "palette",    OPT_STRING, &cmdlineP->palette,
            &paletteSpec,   0);
    OPTENT3(0, "xpos",  OPT_INT, &cmdlineP->xpos, &xposSpec,   0);
    OPTENT3(0, "ypos",  OPT_INT, &cmdlineP->ypos, &yposSpec,   0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We have no parms that are negative numbers */

    pm_optParseOptions3( &argc, (char **)argv, opt, sizeof(opt), 0 );
        /* Uses and sets argc, argv, and some of *cmdline_p and others. */

    if (!xposSpec)
        cmdlineP->xpos = 0;
    else if (cmdlineP->xpos < -32767 || cmdlineP->xpos > 32768)
        pm_error("-xpos value (%d) is outside acceptable range "
                 "(-32767, 32768)", cmdlineP->xpos);

    if (!yposSpec)
        cmdlineP->ypos = 0;
    else if (cmdlineP->ypos < -32767 || cmdlineP->ypos > 32768)
        pm_error("-ypos value (%d) is outside acceptable range "
                 "(-32767, 32768)", cmdlineP->ypos);

    if (!planesSpec)
        cmdlineP->planes = 0;  /* 0 means minimum */

    if (planesSpec) {
        if (cmdlineP->planes > 4 || cmdlineP->planes < 1)
            pm_error("The only possible numbers of planes are 1-4.  "
                     "You specified %u", cmdlineP->planes);
        if (cmdlineP->packed)
            pm_error("-planes is meaningless with -packed.");
        if (cmdlineP->truecolor)
            pm_error("-planes is meaningless with -24bit");
        if (cmdlineP->use8Bit)
            pm_error("-planes is meaningless with -8bit");
    }

    if (paletteSpec && cmdlineP->stdpalette)
        pm_error("You can't specify both -palette and -stdpalette");

    if (!paletteSpec)
        cmdlineP->palette = NULL;

    if (cmdlineP->use8Bit && cmdlineP->truecolor)
        pm_error("You cannot specify both -8bit and -truecolor");

    if (argc-1 < 1)
        cmdlineP->inputFilespec = "-";
    else if (argc-1 == 1)
        cmdlineP->inputFilespec = argv[1];
    else
        pm_error("Program takes at most one argument "
                 "(input file specification).  You specified %d",
                 argc-1);

    free(option_def);
}



/*
 * Write out a two-byte little-endian word to the PCX file
 */
static void
putword(unsigned int const w,
        FILE *       const fp) {

    int rc;

    rc = pm_writelittleshort(fp, w);

    if (rc != 0)
        pm_error("Error writing integer to output file");
}



/*
 * Write out a byte to the PCX file
 */
static void
putbyte(unsigned int const b,
        FILE *       const ofP) {

    int rc;

    rc = fputc(b & 0xff, ofP);

    if (rc == EOF)
        pm_error("Error writing byte to output file.");
}



static void
extractPlane(unsigned char * const rawrow,
             unsigned int    const cols,
             unsigned char * const buf,
             unsigned int    const plane) {
/*----------------------------------------------------------------------------
   From the image row 'rawrow', which is an array of 'cols' palette indices
   (as unsigned 8 bit integers), extract plane number 'plane' and return
   it at 'buf'.

   E.g. Plane 2 is all the 2nd bits from the palette indices, packed so
   that each byte represents 8 columns.
-----------------------------------------------------------------------------*/
    unsigned int const planeMask = 1 << plane;

    unsigned int col;
    int cbit;  /* Significance of bit representing current column in output */
    unsigned char *cp;  /* Ptr to next output byte to fill */
    unsigned char byteUnderConstruction;

    cp = buf;  /* initial value */

    for (col = 0, cbit = 7, byteUnderConstruction = 0x00; col < cols; ++col) {
        if (rawrow[col] & planeMask)
            byteUnderConstruction |= (1 << cbit);

        --cbit;
        if (cbit < 0) {
            /* We've filled a byte.  Output it and start the next */
            *cp++ = byteUnderConstruction;
            cbit = 7;
            byteUnderConstruction = 0x00;
        }
    }
    if (cbit < 7)
        /* A byte was partially built when we ran out of columns (the number
           of columns is not a multiple of 8.  Output the partial byte.
        */
        *cp++ = byteUnderConstruction;
}



static void
packBits(unsigned char * const rawrow,
         unsigned int    const width,
         unsigned char * const buf,
         unsigned int    const bits) {

    unsigned int x;
    int i;
    int shift;

    shift = -1;
    i = -1;

    for (x = 0; x < width; ++x) {
        if (shift < 0) {
            shift = 8 - bits;
            buf[++i] = 0;
        }

        buf[i] |= (rawrow[x] << shift);
        shift -= bits;
    }
}



static void
writeHeader(FILE *              const ofP,
            unsigned int        const cols,
            unsigned int        const rows,
            unsigned int        const bitsPerPixel,
            unsigned int        const planes,
            struct PcxCmapEntry const cmap16[],
            unsigned int        const xPos,
            unsigned int        const yPos) {

    unsigned int bytesPerLine;

    putbyte(PCX_MAGIC, ofP);        /* .PCX magic number            */
    putbyte(0x05, ofP);             /* PC Paintbrush version        */
    putbyte(0x01, ofP);             /* .PCX run length encoding     */
    putbyte(bitsPerPixel, ofP);     /* bits per pixel               */

    putword(xPos, ofP);             /* x1   - image left            */
    putword(yPos, ofP);             /* y1   - image top             */
    putword(xPos+cols-1, ofP);      /* x2   - image right           */
    putword(yPos+rows-1, ofP);      /* y2   - image bottom          */

    putword(cols, ofP);             /* horizontal resolution        */
    putword(rows, ofP);             /* vertical resolution          */

    /* Write out the Color Map for images with 16 colors or less */
    if (cmap16) {
        unsigned int i;
        for (i = 0; i < 16; ++i) {
            putbyte(cmap16[i].r, ofP);
            putbyte(cmap16[i].g, ofP);
            putbyte(cmap16[i].b, ofP);
        }
    } else {
        unsigned int i;
        for (i = 0; i < 16; ++i) {
            putbyte(0, ofP);
            putbyte(0, ofP);
            putbyte(0, ofP);
        }
    }
    putbyte(0, ofP);                /* reserved byte                */
    putbyte(planes, ofP);           /* number of color planes       */

    bytesPerLine = ((cols * bitsPerPixel) + 7) / 8;
    putword(bytesPerLine, ofP);    /* number of bytes per scanline */

    putword(1, ofP);                /* palette info                 */

    {
        unsigned int i;
        for (i = 0; i < 58; ++i)        /* fill to end of header        */
            putbyte(0, ofP);
    }
}



static void
pcxEncode(FILE *                const ofP,
          const unsigned char * const buf,
          unsigned int          const size) {

    const unsigned char * const end = buf + size;

    const unsigned char * currentP;
    unsigned int          previous;
    unsigned int          count;

    currentP = buf;
    previous = *currentP++;
    count    = 1;

    while (currentP < end) {
        unsigned char const c = *currentP++;
        if (c == previous && count < 63)
            ++count;
        else {
            if (count > 1 || (previous & 0xc0) == 0xc0) {
                count |= 0xc0;
                putbyte ( count , ofP );
            }
            putbyte(previous, ofP);
            previous = c;
            count = 1;
        }
    }

    if (count > 1 || (previous & 0xc0) == 0xc0) {
        count |= 0xc0;
        putbyte(count, ofP);
    }
    putbyte(previous, ofP);
}



static unsigned int
indexOfColor(colorhash_table const cht,
             pixel           const color) {
/*----------------------------------------------------------------------------
   Return the index in the palette described by 'cht' of the color 'color'.

   Abort program with error message if the color is not in the palette.
-----------------------------------------------------------------------------*/

    int const rc = ppm_lookupcolor(cht, &color);

    if (rc < 0)
        pm_error("Image contains color which is not "
                 "in the palette: %u/%u/%u",
                 PPM_GETR(color), PPM_GETG(color), PPM_GETB(color));

    return rc;
}



static void
writeRaster16Color(FILE * const ofP,
                   pixel **            const pixels,
                   unsigned int        const cols,
                   unsigned int        const rows,
                   unsigned int        const planes,
                   colorhash_table     const cht,
                   bool                const packbits,
                   unsigned int        const bitsPerPixel) {

    unsigned int const bytesPerLine = ((cols * bitsPerPixel) + 7) / 8;

    unsigned char * indexRow;  /* malloc'ed */
    /* indexRow[x] is the palette index of the pixel at column x of
       the row currently being processed
    */
    unsigned char * planesrow; /* malloc'ed */
    /* This is the input for a single row to the compressor */

    unsigned int row;

    MALLOCARRAY_NOFAIL(indexRow, cols);
    MALLOCARRAY(planesrow, bytesPerLine);

    if (!planesrow)
        pm_error("Failed to allocate buffer for a line of %u bytes",
                 bytesPerLine);

    for (row = 0; row < rows; ++row) {
        unsigned int col;
        for (col = 0; col < cols; ++col)
            indexRow[col] = indexOfColor(cht, pixels[row][col]);

        if (packbits) {
            packBits(indexRow, cols, planesrow, bitsPerPixel);
            pcxEncode(ofP, planesrow, bytesPerLine);
        } else {
            unsigned int plane;
            for (plane = 0; plane < planes; ++plane) {
                extractPlane(indexRow, cols, planesrow, plane);
                pcxEncode(stdout, planesrow, bytesPerLine);
            }
        }
    }
    free(planesrow);
    free(indexRow);
}



static void
ppmTo16ColorPcx(pixel **            const pixels,
                unsigned int        const cols,
                unsigned int        const rows,
                struct PcxCmapEntry const pcxcmap[],
                unsigned int        const colorCt,
                colorhash_table     const cht,
                bool                const packbits,
                unsigned int        const planesRequested,
                unsigned int        const xPos,
                unsigned int        const yPos) {

    unsigned int planes;
    unsigned int bitsPerPixel;

    if (packbits) {
        planes = 1;
        if (colorCt > 4)        bitsPerPixel = 4;
        else if (colorCt > 2)   bitsPerPixel = 2;
        else                    bitsPerPixel = 1;
    } else {
        bitsPerPixel = 1;
        if (planesRequested)
            planes = planesRequested;
        else {
            if (colorCt > 8)        planes = 4;
            else if (colorCt > 4)   planes = 3;
            else if (colorCt > 2)   planes = 2;
            else                   planes = 1;
        }
    }

    writeHeader(stdout, cols, rows, bitsPerPixel, planes, pcxcmap,
                xPos, yPos);

    writeRaster16Color(stdout, pixels, cols, rows, planes, cht, packbits,
                       bitsPerPixel);
}



static void
ppmTo256ColorPcx(pixel **            const pixels,
                 unsigned int        const cols,
                 unsigned int        const rows,
                 struct PcxCmapEntry const pcxcmap[],
                 unsigned int        const colorCt,
                 colorhash_table     const cht,
                 unsigned int        const xPos,
                 unsigned int        const yPos) {

    unsigned char * rawrow;
    unsigned int    row;

    MALLOCARRAY(rawrow, cols);

    if (!rawrow)
        pm_error("Failed to allocate a buffer for %u columns", cols);

    /* 8 bits per pixel, 1 plane */
    writeHeader(stdout, cols, rows, 8, 1, NULL, xPos, yPos);
    for (row = 0; row < rows; ++row) {
        unsigned int col;
        for (col = 0; col < cols; ++col)
            rawrow[col] = indexOfColor(cht, pixels[row][col]);
        pcxEncode(stdout, rawrow, cols);

    }
    putbyte(PCX_256_COLORS, stdout);

    {
        unsigned int i;

        for (i = 0; i < MAXCOLORS; ++i) {
            putbyte(pcxcmap[i].r, stdout);
            putbyte(pcxcmap[i].g, stdout);
            putbyte(pcxcmap[i].b, stdout);
        }
    }
    free(rawrow);
}



static void
ppmToTruecolorPcx(pixel **     const pixels,
                  unsigned int const cols,
                  unsigned int const rows,
                  pixval       const maxval,
                  unsigned int const xPos,
                  unsigned int const yPos) {

    unsigned char * redrow;
    unsigned char * grnrow;
    unsigned char * blurow;
    unsigned int    row;

    MALLOCARRAY(redrow, cols);
    MALLOCARRAY(grnrow, cols);
    MALLOCARRAY(blurow, cols);

    if (!redrow || !grnrow || !blurow)
        pm_error("Unable to allocate buffer for a row of %u pixels", cols);

    /* 8 bits per pixel, 3 planes */
    writeHeader(stdout, cols, rows, 8, 3, NULL, xPos, yPos);

    for (row = 0; row < rows; ++row) {
        pixel * const pixrow = pixels[row];

        unsigned int col;

        for (col = 0; col < cols; ++col) {
            pixel const pix = pixrow[col];

            if (maxval != PCX_MAXVAL) {
                redrow[col] = (long)PPM_GETR(pix) * PCX_MAXVAL / maxval;
                grnrow[col] = (long)PPM_GETG(pix) * PCX_MAXVAL / maxval;
                blurow[col] = (long)PPM_GETB(pix) * PCX_MAXVAL / maxval;
            } else {
                redrow[col] = PPM_GETR(pix);
                grnrow[col] = PPM_GETG(pix);
                blurow[col] = PPM_GETB(pix);
            }
        }
        pcxEncode(stdout, redrow, cols);
        pcxEncode(stdout, grnrow, cols);
        pcxEncode(stdout, blurow, cols);
    }
    free(blurow);
    free(grnrow);
    free(redrow);
}



static const struct PcxCmapEntry
stdPalette[] = {
    {   0,   0,   0 },
    {   0,   0, 170 },
    {   0, 170,   0 },
    {   0, 170, 170 },
    { 170,   0,   0 },
    { 170,   0, 170 },
    { 170, 170,   0 },
    { 170, 170, 170 },
    {  85,  85,  85 },
    {  85,  85, 255 },
    {  85, 255,  85 },
    {  85, 255, 255 },
    { 255,  85,  85 },
    { 255,  85, 255 },
    { 255, 255,  85 },
    { 255, 255, 255 }
};



static void
putPcxColorInHash(colorhash_table const cht,
                  pixel           const newPcxColor,
                  unsigned int    const newColorIndex,
                  pixval          const maxval) {

    pixel ppmColor;
        /* Same color as 'newPcxColor', but at the PPM image's color
           resolution: 'maxval'
        */
    int rc;

    PPM_DEPTH(ppmColor, newPcxColor, PCX_MAXVAL, maxval);

    rc = ppm_lookupcolor(cht, &ppmColor);

    if (rc == -1)
        /* This color is not in the hash yet, so we just add it */
        ppm_addtocolorhash(cht, &ppmColor, newColorIndex);
    else {
        /* This color is already in the hash.  That's because the
           subject image has less color resolution than PCX (i.e.
           'maxval' is less than PCX_MAXVAL), and two distinct
           colors in the standard palette are indistinguishable at
           subject image color resolution.

           So we have to figure out whether color 'newPcxColor' or
           'existingPcxColor' is a better match for 'ppmColor'.
        */

        unsigned int const existingColorIndex = rc;

        pixel idealPcxColor;
        pixel existingPcxColor;

        PPM_DEPTH(idealPcxColor, ppmColor, maxval, PCX_MAXVAL);

        PPM_ASSIGN(existingPcxColor,
                   stdPalette[existingColorIndex].r,
                   stdPalette[existingColorIndex].g,
                   stdPalette[existingColorIndex].b);

        if (PPM_DISTANCE(newPcxColor, idealPcxColor) <
            PPM_DISTANCE(existingPcxColor, idealPcxColor)) {
            /* The new PCX color is a better match.  Make it the new
               translation of image color 'ppmColor'.
            */
            ppm_delfromcolorhash(cht, &ppmColor);
            ppm_addtocolorhash(cht, &ppmColor, newColorIndex);
        }
    }
}



static void
generateStandardPalette(struct PcxCmapEntry ** const pcxcmapP,
                        pixval                 const maxval,
                        colorhash_table *      const chtP,
                        unsigned int *         const colorsP) {

    unsigned int const stdPaletteSize = 16;

    unsigned int          colorIndex;
    struct PcxCmapEntry * pcxcmap;
    colorhash_table       cht;

    MALLOCARRAY_NOFAIL(pcxcmap, MAXCOLORS);

    cht = ppm_alloccolorhash();

    for (colorIndex = 0; colorIndex < stdPaletteSize; ++colorIndex) {
        pixel pcxColor;
            /* The color of this colormap entry, in PCX resolution */

        pcxcmap[colorIndex] = stdPalette[colorIndex];

        PPM_ASSIGN(pcxColor,
                   stdPalette[colorIndex].r,
                   stdPalette[colorIndex].g,
                   stdPalette[colorIndex].b);

        putPcxColorInHash(cht, pcxColor, colorIndex, maxval);
    }

    /* Set remaining slots in palette to black.  The values are not
       meaningful, but this suppresses a Valgrind warning about our writing
       undefined values to the file and makes our output constant with input.
    */
    for ( ; colorIndex < MAXCOLORS; ++colorIndex) {
        pcxcmap[colorIndex].r = 0;
        pcxcmap[colorIndex].g = 0;
        pcxcmap[colorIndex].b = 0;
    }

    *pcxcmapP = pcxcmap;
    *chtP = cht;
    *colorsP = stdPaletteSize;
}



static void
readPpmPalette(const char *   const paletteFileName,
               pixel       (* const ppmPaletteP)[],
               unsigned int * const paletteSizeP) {

    FILE * pfP;
    pixel ** pixels;
    int cols, rows;
    pixval maxval;

    pfP = pm_openr(paletteFileName);

    pixels = ppm_readppm(pfP, &cols, &rows, &maxval);

    pm_close(pfP);

    *paletteSizeP = rows * cols;
    if (*paletteSizeP > MAXCOLORS)
        pm_error("ordered palette image contains %d pixels.  Maximum is %d",
                 *paletteSizeP, MAXCOLORS);

    {
        unsigned int j;
        unsigned int row;
        for (row = 0, j = 0; row < rows; ++row) {
            unsigned int col;
            for (col = 0; col < cols; ++col)
                (*ppmPaletteP)[j++] = pixels[row][col];
        }
    }
    ppm_freearray(pixels, rows);
}



static void
readPaletteFromFile(struct PcxCmapEntry ** const pcxcmapP,
                    const char *           const paletteFileName,
                    pixval                 const maxval,
                    colorhash_table *      const chtP,
                    unsigned int *         const colorsP) {

    unsigned int colorIndex;
    pixel ppmPalette[MAXCOLORS];
    unsigned int paletteSize;
    struct PcxCmapEntry * pcxcmap;
    colorhash_table cht;

    readPpmPalette(paletteFileName, &ppmPalette, &paletteSize);

    MALLOCARRAY_NOFAIL(pcxcmap, MAXCOLORS);

    *pcxcmapP = pcxcmap;

    cht = ppm_alloccolorhash();

    for (colorIndex = 0; colorIndex < paletteSize; ++colorIndex) {
        pixel pcxColor;
            /* The color of this colormap entry, in PCX resolution */

        pcxcmap[colorIndex] = pcxCmapEntryFromPixel(ppmPalette[colorIndex]);

        PPM_ASSIGN(pcxColor,
                   ppmPalette[colorIndex].r,
                   ppmPalette[colorIndex].g,
                   ppmPalette[colorIndex].b);

        putPcxColorInHash(cht, pcxColor, colorIndex, maxval);
    }

    *chtP = cht;
    *colorsP = paletteSize;
}



static void
moveBlackToIndex0(colorhist_vector const chv,
                  unsigned int     const colorCt) {
/*----------------------------------------------------------------------------
   If black is in the palette, make it at Index 0.
-----------------------------------------------------------------------------*/
    pixel blackPixel;
    unsigned int i;
    bool blackPresent;

    PPM_ASSIGN(blackPixel, 0, 0, 0);

    blackPresent = FALSE;  /* initial assumption */

    for (i = 0; i < colorCt; ++i)
        if (PPM_EQUAL(chv[i].color, blackPixel))
            blackPresent = TRUE;

    if (blackPresent) {
        /* We use a trick here.  ppm_addtocolorhist() always adds to the
           beginning of the table and if the color is already elsewhere in
           the table, removes it.
        */
        int colorCt2;

        colorCt2 = colorCt;
        ppm_addtocolorhist(chv, &colorCt2, MAXCOLORS, &blackPixel, 0, 0);
        assert(colorCt2 == colorCt);
    }
}



static void
makePcxColormapFromImage(pixel **               const pixels,
                         unsigned int           const cols,
                         unsigned int           const rows,
                         pixval                 const maxval,
                         struct PcxCmapEntry ** const pcxcmapP,
                         colorhash_table *      const chtP,
                         unsigned int *         const colorCtP,
                         bool *                 const tooManyColorsP) {
/*----------------------------------------------------------------------------
   Make a colormap (palette) for the PCX header that can be used
   for the image described by 'pixels', 'cols', 'rows', and 'maxval'.

   Return it in newly malloc'ed storage and return its address as
   *pcxcmapP.

   Also return a lookup hash to relate a color in the image to the
   appropriate index in *pcxcmapP.  Return that in newly malloc'ed
   storage as *chtP.

   Iff there are too many colors to do that (i.e. more than 256),
   return *tooManyColorsP == TRUE.
-----------------------------------------------------------------------------*/
    int colorCt;
    colorhist_vector chv;

    pm_message("computing colormap...");

    chv = ppm_computecolorhist(pixels, cols, rows, MAXCOLORS, &colorCt);
    if (chv == NULL)
        *tooManyColorsP = TRUE;
    else {
        unsigned int i;
        struct PcxCmapEntry * pcxcmap;

        *tooManyColorsP = FALSE;

        pm_message("%d colors found", colorCt);

        moveBlackToIndex0(chv, colorCt);

        MALLOCARRAY_NOFAIL(pcxcmap, MAXCOLORS);

        *pcxcmapP = pcxcmap;

        for (i = 0; i < colorCt; ++i) {
            pixel p;

            PPM_DEPTH(p, chv[i].color, maxval, PCX_MAXVAL);

            pcxcmap[i].r = PPM_GETR(p);
            pcxcmap[i].g = PPM_GETG(p);
            pcxcmap[i].b = PPM_GETB(p);
        }

        /* Fill it out with black */
        for ( ; i < MAXCOLORS; ++i) {
            pcxcmap[i].r = 0;
            pcxcmap[i].g = 0;
            pcxcmap[i].b = 0;
        }

        *chtP = ppm_colorhisttocolorhash(chv, colorCt);

        *colorCtP = colorCt;

        ppm_freecolorhist(chv);
    }
}



static void
ppmToPalettePcx(pixel **            const pixels,
                unsigned int        const cols,
                unsigned int        const rows,
                pixval              const maxval,
                unsigned int        const xPos,
                unsigned int        const yPos,
                struct PcxCmapEntry const pcxcmap[],
                colorhash_table     const cht,
                unsigned int        const colorCt,
                bool                const packbits,
                unsigned int        const planes,
                bool                const use8Bit) {

    /* convert image */
    if (colorCt <= 16 && !use8Bit )
        ppmTo16ColorPcx(pixels, cols, rows, pcxcmap, colorCt, cht,
                        packbits, planes, xPos, yPos);
    else
        ppmTo256ColorPcx(pixels, cols, rows, pcxcmap, colorCt, cht,
                         xPos, yPos);
}



int
main(int argc, const char *argv[]) {

    struct CmdlineInfo cmdline;
    FILE * ifP;
    int rows, cols;
    pixval maxval;
    pixel **pixels;
    struct PcxCmapEntry * pcxcmap;
    colorhash_table cht;
    bool truecolor;
    unsigned int colorCt;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFilespec);
    pixels = ppm_readppm(ifP, &cols, &rows, &maxval);
    pm_close(ifP);

    if (cmdline.truecolor)
        truecolor = TRUE;
    else {
        if (cmdline.stdpalette) {
            truecolor = FALSE;
            generateStandardPalette(&pcxcmap, maxval, &cht, &colorCt);
        } else if (cmdline.palette) {
            truecolor = FALSE;
            readPaletteFromFile(&pcxcmap, cmdline.palette, maxval,
                                &cht, &colorCt);
        } else {
            bool tooManyColors;
            makePcxColormapFromImage(pixels, cols, rows, maxval,
                                     &pcxcmap, &cht, &colorCt,
                                     &tooManyColors);

            if (tooManyColors) {
                pm_message("too many colors - writing a 24bit PCX file");
                pm_message("if you want a non-24bit file, "
                           " a 'pnmquant %d'", MAXCOLORS);
                truecolor = TRUE;
            } else
                truecolor = FALSE;
        }
    }

    if (truecolor)
        ppmToTruecolorPcx(pixels, cols, rows, maxval,
                          cmdline.xpos, cmdline.ypos);
    else {
        ppmToPalettePcx(pixels, cols, rows, maxval,
                        cmdline.xpos, cmdline.ypos,
                        pcxcmap, cht, colorCt, cmdline.packed,
                        cmdline.planes, cmdline.use8Bit);

        ppm_freecolorhash(cht);
        free(pcxcmap);
    }
    return 0;
}



