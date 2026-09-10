/* tgatoppm.c - read a TrueVision Targa file and write a portable pixmap
**
** Partially based on tga2rast, version 1.0, by Ian MacPhedran.
**
** Copyright (C) 1989 by Jef Poskanzer.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

#define _DEFAULT_SOURCE 1  /* New name for SVID & BSD source defines */
#define _BSD_SOURCE 1      /* Make sure strdup() is in string.h */
#define _XOPEN_SOURCE 500  /* Make sure strdup() is in string.h */

#include <stdbool.h>
#include <string.h>

#include "pm_c_util.h"
#include "ppm.h"
#include "tga.h"
#include "shhopt.h"
#include "nstring.h"

#define MAXCOLORS 16384

static pixel ColorMap[MAXCOLORS];
static gray  AlphaMap[MAXCOLORS];

struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFilename;
    unsigned int headerdump;
    const char * alphaFilename;
    bool         alphaStdout;
};



static void
parseCommandLine(int argc, const char ** argv,
                   struct CmdlineInfo *cmdlineP) {
/*----------------------------------------------------------------------------
   Note that many of the strings that this function returns in the
   *cmdlineP structure are actually in the supplied argv array.  And
   sometimes, one of these strings is actually just a suffix of an entry
   in argv!
-----------------------------------------------------------------------------*/
    optEntry *option_def = malloc(100*sizeof(optEntry));
        /* Instructions to OptParseOptions2 on how to parse our options.
         */
    optStruct3 opt;

    unsigned int option_def_index;

    unsigned int alpha_spec;

    option_def_index = 0;   /* incremented by OPTENTRY */
    OPTENT3(0, "headerdump", OPT_FLAG,   NULL, &cmdlineP->headerdump,   0);
    OPTENT3(0, "debug",      OPT_FLAG,   NULL, &cmdlineP->headerdump,   0);
    OPTENT3(0, "alphaout",   OPT_STRING, &cmdlineP->alphaFilename,
            &alpha_spec, 0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We have no parms that are negative numbers */

    pm_optParseOptions4(&argc, argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (argc - 1 == 0)
        cmdlineP->inputFilename = "-";  /* he wants stdin */
    else if (argc - 1 == 1)
        cmdlineP->inputFilename = strdup(argv[1]);
    else
        pm_error("Too many arguments.  The only argument accepted "
                 "is the input file specification");

    if (alpha_spec &&
        streq(cmdlineP->alphaFilename, "-"))
        cmdlineP->alphaStdout = true;
    else
        cmdlineP->alphaStdout = false;

    if (!alpha_spec)
        cmdlineP->alphaFilename = NULL;
}



static unsigned char
getbyte(FILE * const ifP) {

    unsigned char c;

    if (fread((char*) &c, 1, 1, ifP) != 1)
        pm_error("EOF / read error");

    return c;
}



static void
interpretTgaHeader(struct ImageHeader const tgaHead,
                   unsigned int *     const rowsP,
                   unsigned int *     const colsP,
                   bool *             const mappedP,
                   pixval *           const maxvalP,
                   bool *             const rlencodedP,
                   unsigned int *     const firstColormapIndexP,
                   unsigned int *     const colormapLengthP) {

    *rowsP = ((unsigned int) tgaHead.Height_lo) +
        ((unsigned int) tgaHead.Height_hi) * 256;
    *colsP = ((unsigned int) tgaHead.Width_lo)  +
        ((unsigned int) tgaHead.Width_hi)  * 256;

    switch (tgaHead.ImgType) {
    case TGA_Map:
    case TGA_RGB:
    case TGA_Mono:
    case TGA_RLEMap:
    case TGA_RLERGB:
    case TGA_RLEMono:
        break;
    case TGA_CompMap:
    case TGA_CompMap4:
        pm_error("Targa image type %d (compressed color-mapped data). "
                 "Cannot handle this format.", tgaHead.ImgType);
        break;
    default:
        pm_error("unknown Targa image type %d", tgaHead.ImgType);
    }

    if (tgaHead.ImgType == TGA_Map ||
        tgaHead.ImgType == TGA_RLEMap ||
        tgaHead.ImgType == TGA_CompMap ||
        tgaHead.ImgType == TGA_CompMap4) {

        /* Color-mapped image */

        if (tgaHead.CoMapType != 1)
            pm_error(
                "mapped image (type %d) with color map type != 1",
                tgaHead.ImgType );
        *mappedP = true;
        /* Figure maxval from CoSize. */
        switch (tgaHead.CoSize) {
        case 8:
        case 24:
        case 32:
            *maxvalP = 255;
            break;

        case 15:
        case 16:
            *maxvalP = 31;
            break;

        default:
            pm_error(
                "unknown colormap pixel size - %d", tgaHead.CoSize );
        }
    } else {
        /* Not colormap, so figure maxval from PixelSize. */
        *mappedP = false;
        switch (tgaHead.PixelSize) {
        case 8:
        case 24:
        case 32:
            *maxvalP = 255;
            break;

        case 15:
        case 16:
            *maxvalP = 31;
            break;

        default:
            pm_error("unknown pixel size - %d", tgaHead.PixelSize);
        }
    }
    *rlencodedP =
        tgaHead.ImgType == TGA_RLEMap ||
        tgaHead.ImgType == TGA_RLERGB ||
        tgaHead.ImgType == TGA_RLEMono
        ;

    *firstColormapIndexP = tgaHead.Index_hi  * 256 + tgaHead.Index_lo;
    *colormapLengthP     = tgaHead.Length_hi * 256 + tgaHead.Length_lo;
}



static int RLE_count = 0, RLE_flag = 0;



static void
handleRun(FILE * const ifP,
          bool   const rlencoded,
          bool * const repeatP) {

    if (rlencoded) {
        if (RLE_count == 0) {
            /* Have to restart run. */
            unsigned char i;
            i = getbyte(ifP);
            RLE_flag = (i & 0x80);
            if (RLE_flag == 0) {
                /* Stream of unencoded pixels. */
                RLE_count = i + 1;
            } else {
                /* Single pixel replicated. */
                RLE_count = i - 127;
            }
            /* Decrement count & get pixel. */
            --RLE_count;
            *repeatP = false;
        } else {
            /* Have already read count & (at least) first pixel. */
            --RLE_count;
            if (RLE_flag != 0) {
                /* Replicated pixels. */
                *repeatP = true;
            } else
                *repeatP = false;
        }
    } else
        *repeatP = false;
}



static void
getPixel(FILE *       const ifP,
         pixel *      const colorP,
         unsigned int const size,
         bool         const rlencoded,
         bool         const mapped,
         gray *       const alphaP) {

    static pixval red, grn, blu;
    static pixval alpha;
    static unsigned int l;
    bool repeat;
        /* Next pixel is just a repeat (from an encoded run) */

    handleRun(ifP, rlencoded, &repeat);

    if (repeat) {
        /* Use red, grn, blu, alpha, and l from prior call to getPixel */
    } else {
        /* Read appropriate number of bytes, break into RGB. */
        switch (size) {
        case 8:             /* Grayscale, read and triplicate. */
            red = grn = blu = l = getbyte(ifP);
            alpha = 0;
            break;

        case 16:            /* 5 bits each of red green and blue. */
        case 15: {           /* Watch byte order. */
            unsigned char j, k;
            j = getbyte(ifP);
            k = getbyte(ifP);
            l = ((unsigned int)k << 8) + j;
            red = (k & 0x7C) >> 2;
            grn = ((k & 0x03) << 3) + ((j & 0xE0) >> 5);
            blu = j & 0x1F;
            alpha = 0;
        } break;

        case 32:            /* 8 bits each of blue, green, red, and alpha */
        case 24:            /* 8 bits each of blue, green, and red. */
            blu = getbyte(ifP);
            grn = getbyte(ifP);
            red = getbyte(ifP);
            if (size == 32)
                alpha = getbyte(ifP);
            else
                alpha = 0;
            l = 0;
            break;

        default:
            pm_error("unknown pixel size (#2) - %u", size);
        }
    }
    if (mapped) {
        *colorP = ColorMap[l];
        *alphaP = AlphaMap[l];
    } else {
        PPM_ASSIGN(*colorP, red, grn, blu);
        *alphaP = alpha;
    }
}



static void
readTgaHeader(FILE *               const ifP,
              struct ImageHeader * const tgaP) {

    unsigned char flags;

    tgaP->IdLength  = getbyte(ifP);
    tgaP->CoMapType = getbyte(ifP);
    tgaP->ImgType   = getbyte(ifP);
    tgaP->Index_lo  = getbyte(ifP);
    tgaP->Index_hi  = getbyte(ifP);
    tgaP->Length_lo = getbyte(ifP);
    tgaP->Length_hi = getbyte(ifP);
    tgaP->CoSize    = getbyte(ifP);
    tgaP->X_org_lo  = getbyte(ifP);
    tgaP->X_org_hi  = getbyte(ifP);
    tgaP->Y_org_lo  = getbyte(ifP);
    tgaP->Y_org_hi  = getbyte(ifP);
    tgaP->Width_lo  = getbyte(ifP);
    tgaP->Width_hi  = getbyte(ifP);
    tgaP->Height_lo = getbyte(ifP);
    tgaP->Height_hi = getbyte(ifP);
    tgaP->PixelSize = getbyte(ifP);
    flags           = getbyte(ifP);

    tgaP->AttBits = flags & 0xf;
    tgaP->Rsrvd   = (flags & 0x10) >> 4;
    tgaP->OrgBit  = (flags & 0x20) >> 5;
    tgaP->IntrLve = (flags & 0xc0) >> 6;

    if (tgaP->IdLength != 0) {
        ImageIDField junk;
        fread(junk, 1, (int) tgaP->IdLength, ifP);
    }
}



static void
getMapEntry(FILE * const ifP, pixel * Value, int Size, gray * Alpha) {

    unsigned char j, k, r, g, b, a;

    /* Read appropriate number of bytes, break into rgb & put in map. */
    switch ( Size )
    {
    case 8:             /* Grayscale, read and triplicate. */
        r = g = b = getbyte( ifP );
        a = 0;
        break;

    case 16:            /* 5 bits each of red green and blue. */
    case 15:            /* Watch for byte order. */
        j = getbyte( ifP );
        k = getbyte( ifP );
        r = ( k & 0x7C ) >> 2;
        g = ( ( k & 0x03 ) << 3 ) + ( ( j & 0xE0 ) >> 5 );
        b = j & 0x1F;
        a = 0;
        break;

    case 32:            /* 8 bits each of blue, green, red, and alpha */
    case 24:            /* 8 bits each of blue green and red. */
        b = getbyte( ifP );
        g = getbyte( ifP );
        r = getbyte( ifP );
        if ( Size == 32 )
            a = getbyte( ifP );
        else
            a = 0;
        break;

    default:
        pm_error( "unknown colormap pixel size (#2) - %d", Size );
    }
    PPM_ASSIGN( *Value, r, g, b );
    *Alpha = a;
}



static void
dumpHeader(struct ImageHeader const tga_head) {
    const char * imgTypeName;
    switch(tga_head.ImgType) {
    case TGA_Map:      imgTypeName = "TGA_Map";      break;
    case TGA_RGB:      imgTypeName = "TGA_RGB";      break;
    case TGA_Mono:     imgTypeName = "TGA_Mono";     break;
    case TGA_RLEMap:   imgTypeName = "TGA_RLEMap";   break;
    case TGA_RLERGB:   imgTypeName = "TGA_RLERGB";   break;
    case TGA_RLEMono:  imgTypeName = "TGA_RLEMono";  break;
    case TGA_CompMap:  imgTypeName = "TGA_CompMap";  break;
    case TGA_CompMap4: imgTypeName = "TGA_CompMap4"; break;
    default:           imgTypeName = "unknown";
    }
    pm_message( "IdLength = %d", (int) tga_head.IdLength );
    pm_message( "CoMapType = %d", (int) tga_head.CoMapType );
    pm_message( "ImgType = %d (%s)", (int) tga_head.ImgType, imgTypeName );
    pm_message( "Index_lo = %d", (int) tga_head.Index_lo );
    pm_message( "Index_hi = %d", (int) tga_head.Index_hi );
    pm_message( "Length_lo = %d", (int) tga_head.Length_lo );
    pm_message( "Length_hi = %d", (int) tga_head.Length_hi );
    pm_message( "CoSize = %d", (int) tga_head.CoSize );
    pm_message( "X_org_lo = %d", (int) tga_head.X_org_lo );
    pm_message( "X_org_hi = %d", (int) tga_head.X_org_hi );
    pm_message( "Y_org_lo = %d", (int) tga_head.Y_org_lo );
    pm_message( "Y_org_hi = %d", (int) tga_head.Y_org_hi );
    pm_message( "Width_lo = %d", (int) tga_head.Width_lo );
    pm_message( "Width_hi = %d", (int) tga_head.Width_hi );
    pm_message( "Height_lo = %d", (int) tga_head.Height_lo );
    pm_message( "Height_hi = %d", (int) tga_head.Height_hi );
    pm_message( "PixelSize = %d", (int) tga_head.PixelSize );
    pm_message( "AttBits = %d", (int) tga_head.AttBits );
    pm_message( "Rsrvd = %d", (int) tga_head.Rsrvd );
    pm_message( "OrgBit = %d", (int) tga_head.OrgBit );
    pm_message( "IntrLve = %d", (int) tga_head.IntrLve );
}



static void
readColormap(FILE *       const ifP,
             unsigned int const firstColormapIndex,
             unsigned int const colormapLength,
             unsigned int const coSize,
             pixel *      const colorMap,
             gray *       const alphaMap) {

    unsigned int const colormapEnd = firstColormapIndex + colormapLength;

    unsigned int i;

    if (colormapEnd > MAXCOLORS)
        pm_error("too many colors - %u.  Max we can handle is %u",
                 colormapEnd, MAXCOLORS);

    for (i = firstColormapIndex; i < colormapEnd; ++i)
        getMapEntry(ifP, &ColorMap[i], coSize, &AlphaMap[i]);
}



static void
readRaster(FILE *        const ifP,
           unsigned int  const cols,
           unsigned int  const rows,
           unsigned int  const pixelSize,
           unsigned char const intrLve,
           unsigned char const orgBit,
           bool          const rlencoded,
           bool          const mapped,
           pixel ***     const pixelsP,
           gray ***      const alphaP) {
/*---------a-------------------------------------------------------------------
  Read the Targa file body and convert to portable format.

  Return the color raster and alpha raster in newly malloced arrays
  *pixelsP, and *alphaP, respectively.
-----------------------------------------------------------------------------*/
    pixel ** pixels;
    gray **  alpha;
    unsigned int row, truerow, baserow;

    pixels = ppm_allocarray(cols, rows);
    alpha  = pgm_allocarray(cols, rows);

    for (row = 0, truerow = 0, baserow = 0; row < rows; ++row) {
        unsigned int const realrow =
            orgBit == 0 ? rows - truerow - 1 : truerow;

        unsigned int col;

        for (col = 0; col < cols; ++col)
            getPixel(ifP, &(pixels[realrow][col]),
                     pixelSize, rlencoded, mapped,
                     &(alpha[realrow][col]));
        if (intrLve == TGA_IL_Four)
            truerow += 4;
        else if (intrLve == TGA_IL_Two)
            truerow += 2;
        else
            ++truerow;
        if (truerow >= rows)
            truerow = ++baserow;
    }
    *pixelsP = pixels;
    *alphaP  = alpha;
}



int
main(int argc, const char ** argv) {

    struct CmdlineInfo cmdline;
    struct ImageHeader tgaHead;
    FILE * ifP;
    FILE * ofP;
    FILE * alphaFP;
    unsigned int rows, cols;
    pixval maxval;
    bool rlencoded;
    bool mapped;
    unsigned int firstColormapIndex;
    unsigned int colormapLength;
    pixel ** pixels;   /* The image array in ppm format */
    gray ** alpha;     /* The alpha channel array in pgm format */

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFilename);

    if (cmdline.alphaStdout)
        alphaFP = stdout;
    else if (cmdline.alphaFilename == NULL)
        alphaFP = NULL;
    else
        alphaFP = pm_openw(cmdline.alphaFilename);

    if (cmdline.alphaStdout)
        ofP = NULL;
    else
        ofP = stdout;

    readTgaHeader(ifP, &tgaHead);

    if (cmdline.headerdump)
        dumpHeader(tgaHead);

    interpretTgaHeader(tgaHead, &rows, &cols, &mapped, &maxval, &rlencoded,
                       &firstColormapIndex, &colormapLength);

    if (tgaHead.CoMapType != 0) {
        /* There's a color map.  Read it */
        readColormap(ifP, firstColormapIndex, colormapLength, tgaHead.CoSize,
                     ColorMap, AlphaMap);
    }

    readRaster(ifP, cols, rows,
               tgaHead.PixelSize, tgaHead.IntrLve, tgaHead.OrgBit,
               rlencoded, mapped,
               &pixels, &alpha);

    pm_close(ifP);

    if (ofP)
        ppm_writeppm(ofP, pixels, cols, rows, maxval, 0);

    if (alphaFP)
        pgm_writepgm(alphaFP, alpha, cols, rows, maxval, 0);

    pgm_freearray(alpha,  rows);
    ppm_freearray(pixels, rows);

    if (ofP)
        pm_close(ofP);
    if (alphaFP)
        pm_close(alphaFP);

    return 0;
}


