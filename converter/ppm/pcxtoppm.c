/*
 * pcxtoppm.c - Converts from a PC Paintbrush PCX file to a PPM file.
 *
 * Copyright (c) 1990 by Michael Davidson
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose and without fee is hereby granted,
 * provided that the above copyright notice appear in all copies and that
 * both that copyright notice and this permission notice appear in
 * supporting documentation.
 *
 * This file is provided AS IS with no warranties of any kind.  The author
 * shall have no liability with respect to the infringement of copyrights,
 * trade secrets or any patents by this file or any part thereof.  In no
 * event will the author be liable for any lost revenue or profits or
 * other special, indirect and consequential damages.
 *
 * Modifications by Ingo Wilken (Ingo.Wilken@informatik.uni-oldenburg.de)
 * 20/Apr/94:
 *  - checks if 16-color-palette is completely black -> use standard palette
 *  - "-stdpalette" option to enforce this
 *  - row-by-row operation (PPM output)
 *  11/Dec/94:
 *  - capability for 24bit and 32bit (24bit + 8bit intensity) images
 *  - row-by-row operation (PCX input, for 16-color and truecolor images)
 *  - some code restructuring
 *  15/Feb/95:
 *  - bugfix for 16 color-images: few bytes allocated for rawrow in some cases
 *  - added sanity checks for cols<->BytesPerLine
 *  17/Jul/95:
 *  - moved check of 16-color-palette into pcx_16col_to_ppm(),
 *    now checks if it contains only a single color
 */
#include <stdbool.h>
#include <limits.h>
#include <assert.h>

#include "pm_c_util.h"
#include "mallocvar.h"
#include "shhopt.h"
#include "ppm.h"

#define PCX_MAGIC       0x0a            /* PCX magic number             */
#define PCX_HDR_SIZE    128             /* size of PCX header           */
#define PCX_256_COLORS  0x0c            /* magic number for 256 colors  */

#define PCX_MAXVAL      (pixval)255

/* standard palette */
static unsigned char const StdRed[]   = { 0, 255,   0,   0, 170, 170, 170, 170, 85,  85,  85,  85, 255, 255, 255, 255 };
static unsigned char const StdGreen[] = { 0, 255, 170, 170,   0,   0, 170, 170, 85,  85, 255, 255,  85,  85, 255, 255 };
static unsigned char const StdBlue[]  = { 0, 255,   0, 170,   0, 170,   0, 170, 85, 255,  85, 255,  85, 255,  85, 255 };

static pixel stdPalette[16];



static void
generateStdPalette(void) {

    unsigned int i;

    for (i = 0; i < 16; ++i)
        PPM_ASSIGN(stdPalette[i], StdRed[i], StdGreen[i], StdBlue[i]);
}



struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFileNm;  /* '-' if stdin */
    unsigned int verbose;
    unsigned int stdpalette;
};



static void
parseCommandLine(int argc, const char ** argv,
                 struct CmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   Parse program command line described in Unix standard form by argc
   and argv.  Return the information in the options as *cmdlineP.

   If command line is internally inconsistent (invalid options, etc.),
   issue error message to stderr and abort program.

   Note that the strings we return are stored in the storage that
   was passed to us as the argv array.  We also trash *argv.
-----------------------------------------------------------------------------*/
    optEntry * option_def;
    optStruct3 opt;

    unsigned int option_def_index;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0, "stdpalette",     OPT_FLAG,   NULL,
            &cmdlineP->stdpalette,    0 );
    OPTENT3(0, "verbose",        OPT_FLAG,   NULL,
            &cmdlineP->verbose,       0 );

    opt.opt_table = option_def;
    opt.short_allowed = false;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = false;  /* We have no parms that are negative numbers */

    pm_optParseOptions4(&argc, argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdline_p and others. */

    if (argc-1 < 1)
        cmdlineP->inputFileNm = "-";
    else if (argc-1 == 1)
        cmdlineP->inputFileNm = argv[1];
    else
        pm_error("Program takes at most one argument "
                 "(input file specification).  You specified %d",
                 argc-1);
}



struct PcxHeader {
    int version;
    /* Xmin, Ymin, Xmax, and Ymax are positions in some field (in units of
       pixels) of the edges of the image.  They may be negative.  You can
       derive the image width and height from these.
    */
    short xmin;
    short ymin;
    short xmax;
    short ymax;
    short encoding;
    short planeCt;
    short bitsPerPixel;
    short bytesPerLine;
        /* Number of decompressed bytes each plane of each row of the image
           takes.  Because of padding (this is always an even number), there
           may be garbage on the right end that isn't part of the image.
        */
    short paletteInfo;
    short horizontalResolution;
    short verticalResolution;
    bool  hasCmap16;
    pixel cmap16[16];
        /* This is the palette used if it is a 16-color PCX image, except that
           if it is all black, that means to use the standard palette.  It
           is meaningless for a 256-color PCX image (in which the palette is
           at the end of the image) and a true color image (which does not use
           a palette).

           It is a common error for a 16-color image that is supposed to use
           the standard palette to have junk here (maybe because there was
           a time when only this field didn't exist?)
        */
};



static int
getByte(FILE * const fp) {

    int    c;

    if ((c = fgetc(fp)) == EOF)
        pm_error("unexpected end of file" );

    return c;
}



static bool
allBlackPalette(pixel const cmap16[]) {

    unsigned int colorIndex;
    bool allBlack;

    for (colorIndex = 0, allBlack = true; colorIndex < 16; ++colorIndex) {
        pixel const p = cmap16[colorIndex];

        if (PPM_GETR(p) != 0 ||
            PPM_GETG(p) != 0 ||
            PPM_GETB(p) != 0)

            allBlack = false;
    }
    return allBlack;
}



static void
readPcxHeader(FILE *             const ifP,
              struct PcxHeader * const pcxHeaderP) {
/*----------------------------------------------------------------------------
   Read the PCX header
-----------------------------------------------------------------------------*/
    if (getByte(ifP) != PCX_MAGIC)
        pm_error("bad magic number - not a PCX file");

    pcxHeaderP->version = getByte(ifP);  /* get version # */

    pcxHeaderP->encoding = getByte(ifP);
    if (pcxHeaderP->encoding != 1)    /* check for PCX run length encoding   */
        pm_error("unknown encoding scheme: %d", pcxHeaderP->encoding);

    pcxHeaderP->bitsPerPixel= getByte(ifP);
    pm_readlittleshort(ifP, &pcxHeaderP->xmin);
    pm_readlittleshort(ifP, &pcxHeaderP->ymin);
    pm_readlittleshort(ifP, &pcxHeaderP->xmax);
    pm_readlittleshort(ifP, &pcxHeaderP->ymax);

    if (pcxHeaderP->xmax < pcxHeaderP->xmin)
        pm_error("Invalid PCX input:  minimum X value (%d) is greater than "
                 "maximum X value (%d).",
                 pcxHeaderP->xmin, pcxHeaderP->xmax);
    if (pcxHeaderP->ymax < pcxHeaderP->ymin)
        pm_error("Invalid PCX input:  minimum Y value (%d) is greater than "
                 "maximum Y value (%d).",
                 pcxHeaderP->ymin, pcxHeaderP->ymax);

    pm_readlittleshort(ifP, &pcxHeaderP->horizontalResolution);
    pm_readlittleshort(ifP, &pcxHeaderP->verticalResolution);

    {
        unsigned int i;
        /*
         * get the 16-color color map
         */
        for (i = 0; i < 16; ++i) {
            unsigned int const r = getByte(ifP);
            unsigned int const g = getByte(ifP);
            unsigned int const b = getByte(ifP);
            PPM_ASSIGN(pcxHeaderP->cmap16[i], r, g, b);
        }
        pcxHeaderP->hasCmap16 = !allBlackPalette(pcxHeaderP->cmap16);
    }

    getByte(ifP);                /* skip reserved byte       */
    pcxHeaderP->planeCt = getByte(ifP);     /* # of color planes        */
    pm_readlittleshort(ifP, &pcxHeaderP->bytesPerLine);
    pm_readlittleshort(ifP, &pcxHeaderP->paletteInfo);

    /* Read past a bunch of reserved space in the header.  We have read
       70 bytes of the header so far.  We would just seek here, except that
       we want to work with unseekable (e.g. pipe input).
    */
    {
        unsigned int pos;

        for (pos = 70; pos < PCX_HDR_SIZE; ++pos)
            getByte(ifP);
    }
}



static void
reportPcxHeader(struct PcxHeader const pcxHeader) {

    pm_message("Version: %d", pcxHeader.version);
    pm_message("BitsPerPixel: %d", pcxHeader.bitsPerPixel);
    pm_message("Xmin: %d   Ymin: %d   Xmax: %d   Ymax: %d",
               pcxHeader.xmin, pcxHeader.ymin, pcxHeader.xmax, pcxHeader.ymax);
    pm_message("Planes: %d    BytesPerLine: %d    PaletteInfo: %d",
               pcxHeader.planeCt, pcxHeader.bytesPerLine,
               pcxHeader.paletteInfo);
    pm_message("Color map in image:  (index: r/g/b)");

    if (pcxHeader.bitsPerPixel < 8) {
        unsigned int colorIndex;
        for (colorIndex = 0; colorIndex < 16; ++colorIndex) {
            pixel const p = pcxHeader.cmap16[colorIndex];
            pm_message("  %u: %u/%u/%u", colorIndex,
                       PPM_GETR(p), PPM_GETG(p), PPM_GETB(p));
        }
    }
}



static void
getPCXRow(FILE *          const ifP,
          unsigned char * const pcxrow,
          unsigned int    const bytesperline) {
/*----------------------------------------------------------------------------
   Read one row from the PCX raster.

   The PCX raster is run length encoded as follows: If the upper two bits of a
   byte are 11, the lower 6 bits are a repetition count for the raster byte
   that follows.  If the upper two bits are not 11, the byte _is_ a raster
   byte, with repetition count 1.

   A run can't span rows, but it can span planes within a row.  That's why
   'repetitionsLeft' and 'c' are static variables in this subroutine.
-----------------------------------------------------------------------------*/
    static int repetitionsLeft = 0;
    static int c;
    unsigned int bytesGenerated;

    bytesGenerated = 0;
    while(bytesGenerated < bytesperline) {
        if (repetitionsLeft > 0) {
            pcxrow[bytesGenerated++] = c;
            --repetitionsLeft;
        } else {
            c = getByte(ifP);
            if ((c & 0xc0) != 0xc0)
                /* This is a 1-shot byte, not a repetition count */
                pcxrow[bytesGenerated++] = c;
            else {
                /* This is a repetition count for the following byte */
                repetitionsLeft = c & 0x3f;
                c = getByte(ifP);
            }
        }
    }
}



static void
pcxUnpackPixels(unsigned char * const pixels,
                unsigned char * const bitplanes,
                unsigned int    const bytesperline,
                unsigned int    const planeCt,
                unsigned int    const bitsperpixel) {
/*----------------------------------------------------------------------------
   Convert packed pixel format in bitplanes[] into 1 pixel per byte
   in pixels[].
-----------------------------------------------------------------------------*/
    unsigned int i;

    if (planeCt != 1)
        pm_error("can't handle packed pixels with more than 1 plane" );

    for (i = 0; i < bytesperline; ++i) {
        unsigned int const bits = bitplanes[i];

        switch (bitsperpixel) {
        case 4:
            pixels[2*i + 0] = (bits >> 4) & 0x0f;
            pixels[2*i + 1] = (bits     ) & 0x0f;
            break;
        case 2:
            pixels[i*4 + 0] = (bits >> 6) & 0x03;
            pixels[i*4 + 1] = (bits >> 4) & 0x03;
            pixels[i*4 + 2] = (bits >> 2) & 0x03;
            pixels[i*4 + 3] = (bits     ) & 0x03;
            break;
        case 1:
            pixels[i*8 + 0]  = ((bits & 0x80) != 0);
            pixels[i*8 + 1]  = ((bits & 0x40) != 0);
            pixels[i*8 + 2]  = ((bits & 0x20) != 0);
            pixels[i*8 + 3]  = ((bits & 0x10) != 0);
            pixels[i*8 + 4]  = ((bits & 0x08) != 0);
            pixels[i*8 + 5]  = ((bits & 0x04) != 0);
            pixels[i*8 + 6]  = ((bits & 0x02) != 0);
            pixels[i*8 + 7]  = ((bits & 0x01) != 0);
            break;
        default:
        pm_error("pcxUnpackPixels - can't handle %u bits per pixel",
                 bitsperpixel);
        }
    }
}



static void
pcxPlanesToPixels(unsigned char * const pixels,
                  unsigned char * const bitPlanes,
                  unsigned int    const bytesPerLine,
                  unsigned int    const planeCt,
                  unsigned int    const bitsPerPixel) {
/*----------------------------------------------------------------------------
   Unpack sub-byte sample values to yield one byte per pixel.

   As input, 'bitPlanes' is a row of the image.  Each byte contains 8 samples,
   one per bit.  They are arranged in plane major, column minor order, e.g.
   in a 100-column image, the Plane 1 samples of the Columns 0-8 are in
   bitPlanes[100].

   As output, each byte of 'pixels' is one pixel, in major order by column and
   minor order by plane, e.g. in a 4-plane image, pixels[5] is the Plane 1 of
   Column 1.

   'bitsPerPixel' is the number of bits per sample, and must be 1.  It's easy
   to conceive how this subroutine should work with other numbers of bits per
   sample, but it doesn't.
-----------------------------------------------------------------------------*/
    unsigned int const pixelCt = bytesPerLine * 8;

    unsigned int bitPlanesIdx;
        /* Index into 'bitPlanes' of next byte to unpack */

    unsigned int  i;

    if (planeCt > 4)
        pm_error("can't handle more than 4 planes");
    if (bitsPerPixel != 1)
        pm_error("can't handle more than 1 bit per pixel");

    /* Clear the pixel buffer - initial value */
    for (i = 0; i < pixelCt; ++i)
        pixels[i] = 0;

    bitPlanesIdx = 0;  /* initial value */

    for (i = 0; i < planeCt; ++i) {
        unsigned int const pixbit = (1 << i);

        unsigned int pixelIdx;
            /* Index into 'pixels' of next pixel to output */

        unsigned int j;

        for (j = 0, pixelIdx = 0; j < bytesPerLine; ++j) {
            unsigned int const bits = bitPlanes[bitPlanesIdx++];

            unsigned int mask;

            for (mask = 0x80; mask != 0; mask >>= 1) {
                if (bits & mask)
                    pixels[pixelIdx] |= pixbit;
                ++pixelIdx;
            }
        }
    }
}



static bool
paletteIsAllOneColor(const pixel * const cmap,
                     unsigned int  const bitsPerPixel,
                     unsigned int  const planeCt) {
/*----------------------------------------------------------------------------
   Every color that can be indexed by 'bitsPerPixel' bits per plane across
   'planeCt' planes is the same.
-----------------------------------------------------------------------------*/
    unsigned int const maxColorIdx = (1 << (bitsPerPixel * planeCt)) - 1;
        /* The maximum color index that a pixel can have with 'planeCt'
           planes of 'bitsPerPixel' each
        */
    bool allOneColor;
    unsigned int i;

    assert(bitsPerPixel > 0);
    assert(bitsPerPixel <= 4);
    assert(planeCt > 0);
    assert(planeCt <= 4);
    assert(maxColorIdx > 0);
    assert(maxColorIdx < 16);

    for (i = 0, allOneColor = true; i <= maxColorIdx - 1; ++i) {
        if (!PPM_EQUAL(cmap[i], cmap[i+1])) {
            allOneColor = false;
        }
    }

    return allOneColor;
}



static void
pcx16ColToPpm(FILE *       const ifP,
              unsigned int const headerCols,
              unsigned int const rows,
              unsigned int const bytesPerLine,
              unsigned int const bitsPerPixel,
              unsigned int const planeCt,
              bool         const stdPaletteWanted,
              pixel *      const pcxCmap) {
/*----------------------------------------------------------------------------
   'pcxCmap' is the 16-color colormap in the PCX image header, or null if
   there isn't one.
-----------------------------------------------------------------------------*/
    pixel * cmap16;
    unsigned int cols;
    unsigned int row;
    unsigned int rawcols;
    unsigned char * pcxrow;
    unsigned char * rawrow;
    pixel * ppmrow;

    if (stdPaletteWanted || !pcxCmap)
        cmap16 = stdPalette;
    else if (pcxCmap) {
        if (paletteIsAllOneColor(pcxCmap, bitsPerPixel, planeCt)) {
            pm_message("warning - useless header palette, "
                       "using builtin standard palette");

            cmap16 = stdPalette;
        } else
            cmap16 = pcxCmap;
    } else
          cmap16 = stdPalette;

    if (bytesPerLine > UINT_MAX/8)
        pm_error("Image too wide to compute (%u bytes per line)",
                 bytesPerLine);

    /*  bytesPerLine should be >= bBitsPerPixel * cols / 8  */
    rawcols = bytesPerLine * 8 / bitsPerPixel;

    if (headerCols > rawcols) {
        pm_message("warning - BytesPerLine = %u, "
                   "truncating image to %u pixels",
                   bytesPerLine, rawcols);
        cols = rawcols;
    } else
        cols = headerCols;

    MALLOCARRAY(pcxrow, planeCt * bytesPerLine);
    if (pcxrow == NULL)
        pm_error("Can't get memory for %u planes, %u bytes per line",
                 planeCt, bytesPerLine);
    MALLOCARRAY(rawrow, rawcols);
    if (rawrow == NULL)
        pm_error("Can't get memory for %u columns", rawcols);

    ppmrow = ppm_allocrow(cols);

    for (row = 0; row < rows; ++row) {
        unsigned int col;

        getPCXRow(ifP, pcxrow, planeCt * bytesPerLine);

        if (planeCt == 1)
            pcxUnpackPixels(rawrow, pcxrow, bytesPerLine,
                            planeCt, bitsPerPixel);
        else
            pcxPlanesToPixels(rawrow, pcxrow, bytesPerLine,
                              planeCt, bitsPerPixel);

        for (col = 0; col < cols; ++col)
            ppmrow[col] = cmap16[rawrow[col]];

        ppm_writeppmrow(stdout, ppmrow, cols, PCX_MAXVAL, 0);
    }
    ppm_freerow(ppmrow);
    free(rawrow);
    free(pcxrow);
}



static void
pcx256ColToPpm(FILE *       const ifP,
               unsigned int const headerCols,
               unsigned int const rows,
               unsigned int const bytesPerLine) {

    unsigned int cols;
    pixel colormap[256];
    pixel * ppmrow;
    unsigned char ** image;
    unsigned char colormapSignature;
    unsigned int row;

    if (headerCols > bytesPerLine) {
        pm_message("warning - BytesPerLine = %u, "
                   "truncating image to %u pixels",
                   bytesPerLine,  bytesPerLine);
        cols = bytesPerLine;
    } else
        cols = headerCols;

    MALLOCARRAY2(image, rows, bytesPerLine);

    for (row = 0; row < rows; ++row)
        getPCXRow(ifP, image[row], bytesPerLine);

    /*
     * 256 color images have their color map at the end of the file
     * preceded by a magic byte
     */
    colormapSignature = getByte(ifP);
    if (colormapSignature != PCX_256_COLORS)
        pm_error("bad color map signature.  In a 1-plane PCX image "
                 "such as this, we expect a magic number of %u in the byte "
                 "following the raster, to introduce the color map.  "
                 "Instead, this image has %u.",
                 PCX_256_COLORS, colormapSignature);
    else {
        unsigned int colorIndex;

        for (colorIndex = 0; colorIndex < 256; ++colorIndex) {
            pixval const r = getByte(ifP);
            pixval const g = getByte(ifP);
            pixval const b = getByte(ifP);
            PPM_ASSIGN(colormap[colorIndex], r, g, b);
        }
    }

    ppmrow = ppm_allocrow(cols);
    for (row = 0; row < rows; ++row) {
        unsigned int col;
        for (col = 0; col < cols; ++col)
            ppmrow[col] = colormap[image[row][col]];
        ppm_writeppmrow(stdout, ppmrow, cols, PCX_MAXVAL, 0);
    }

    ppm_freerow(ppmrow);
    pm_freearray((void *)image, rows);
}



static void
pcxTruecolToPpm(FILE *       const ifP,
                unsigned int const headerCols,
                unsigned int const rows,
                unsigned int const bytesPerLine,
                unsigned int const planeCt) {

    unsigned int    cols;
    unsigned char * redrow;
    unsigned char * grnrow;
    unsigned char * blurow;
    unsigned char * intensityrow;
    pixel *         ppmrow;
    unsigned int    row;

    if (headerCols > bytesPerLine) {
        pm_message("warning - BytesPerLine = %u, "
                   "truncating image to %u pixels",
                   bytesPerLine, bytesPerLine);
        cols = bytesPerLine;
    } else
        cols = headerCols;

    MALLOCARRAY(redrow, bytesPerLine);
    MALLOCARRAY(grnrow, bytesPerLine);
    MALLOCARRAY(blurow, bytesPerLine);

    if (redrow == NULL || grnrow == NULL || blurow == NULL)
        pm_error("Can't get memory for %u-byte row buffer", bytesPerLine);

    if (planeCt == 4) {
        MALLOCARRAY(intensityrow, bytesPerLine);
        if (intensityrow == NULL)
            pm_error("Can't get memory for %u-byte row buffer", bytesPerLine);
    } else
        intensityrow = NULL;

    ppmrow = ppm_allocrow(cols);
    for (row = 0; row < rows; ++row) {
        unsigned int col;

        getPCXRow(ifP, redrow, bytesPerLine);
        getPCXRow(ifP, grnrow, bytesPerLine);
        getPCXRow(ifP, blurow, bytesPerLine);
        if (intensityrow)
            getPCXRow(ifP, intensityrow, bytesPerLine);

        for (col = 0; col < cols; ++col) {
            unsigned int const r = redrow[col];
            unsigned int const g = grnrow[col];
            unsigned int const b = blurow[col];
            unsigned int const i = intensityrow ? intensityrow[col] : 256;

            PPM_ASSIGN(ppmrow[col],
                       r * i / 256, g * i / 256, b * i / 256);
        }
        ppm_writeppmrow(stdout, ppmrow, cols, PCX_MAXVAL, 0);
    }

    ppm_freerow(ppmrow);
    if (intensityrow)
        free(intensityrow);
    free(blurow);
    free(grnrow);
    free(redrow);
}



int
main(int argc, const char *argv[]) {

    FILE * ifP;
    struct CmdlineInfo cmdline;
    struct PcxHeader pcxHeader;
    unsigned int width, height;

    pm_proginit(&argc, argv);

    generateStdPalette();

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFileNm);

    readPcxHeader(ifP, &pcxHeader);

    if (cmdline.verbose)
        reportPcxHeader(pcxHeader);

    width  = (pcxHeader.xmax - pcxHeader.xmin) + 1;
    height = (pcxHeader.ymax - pcxHeader.ymin) + 1;

    ppm_writeppminit(stdout, width, height, PCX_MAXVAL, 0);

    switch (pcxHeader.bitsPerPixel) {
    case 1:
        if(pcxHeader.planeCt >= 1 && pcxHeader.planeCt <= 4)
            pcx16ColToPpm(ifP, width, height, pcxHeader.bytesPerLine,
                          pcxHeader.bitsPerPixel, pcxHeader.planeCt,
                          cmdline.stdpalette,
                          pcxHeader.hasCmap16 ? pcxHeader.cmap16 : NULL);
        else
            pm_error("PCX image has 1 bit per plane and %d planes; "
                     "Maximum allowed with 1 bit per plane is 4 planes",
                     pcxHeader.planeCt);
        break;
    case 2:
    case 4:
        if (pcxHeader.planeCt == 1)
            pcx16ColToPpm(ifP, width, height, pcxHeader.bytesPerLine,
                          pcxHeader.bitsPerPixel, pcxHeader.planeCt,
                          cmdline.stdpalette,
                          pcxHeader.hasCmap16 ? pcxHeader.cmap16 : NULL);
        else
            pm_error("PCX image has %d planes with %d bits per plane.  "
                     "With this many bits per plane, there can be only 1 "
                     "plane",
                     pcxHeader.bitsPerPixel, pcxHeader.planeCt);
        break;
    case 8:
        switch (pcxHeader.planeCt) {
        case 1:
            pcx256ColToPpm(ifP, width, height, pcxHeader.bytesPerLine);
            break;
        case 3:
        case 4:
            pcxTruecolToPpm(ifP, width, height,
                            pcxHeader.bytesPerLine, pcxHeader.planeCt);
            break;
        default:
            pm_error("PCX image has 8 bits per plane and %d planes.  "
                     "With 8 bits per plane, only 1, 3, or 4 planes is "
                     "allowed.",
                     pcxHeader.planeCt);
        }
        break;
    default:
        pm_error("PCX image claims %d bits per plane.  "
                 "Only 1, 2, 4, and 8 are possible.",
                 pcxHeader.bitsPerPixel);
    }
    pm_close(ifP);

    return 0;
}



