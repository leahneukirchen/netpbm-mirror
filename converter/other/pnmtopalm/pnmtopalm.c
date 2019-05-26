/* pnmtopalm.c - read a PNM image and write a Palm Bitmap file
 *
 * Inspired by and using methods from ppmtoTbmp.c by Ian Goldberg
 * <iang@cs.berkeley.edu>, which was based on ppmtopuzz.c by Jef
 * Poskanzer, from the netpbm-1mar1994 package.
 *
 * Mods for multiple bits per pixel were added to ppmtoTbmp.c by
 * George Caswell <tetsujin@sourceforge.net> and Bill Janssen
 * <bill@janssen.org>.
 *
 * Major fixes and new capability added by Paul Bolle <pebolle@tiscali.nl>
 * in late 2004 / early 2005.
 *
 * See LICENSE file for licensing information.
 *
 * References for the Palm Bitmap format:
 *
 * https://web.archive.org/web/20030621112139/http://www.palmos.com:80/dev/support/docs/
 * https://web.archive.org/web/20030413080018/http://www.palmos.com:80/dev/support/docs/palmos/ReferenceTOC.html
 *
 * http://www.trantor.de/kawt/doc/palmimages.html
 * (above retrieved August 2017)
 */

#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <limits.h>

#include "pm_c_util.h"
#include "pnm.h"
#include "shhopt.h"
#include "mallocvar.h"
#include "runlength.h"

#include "palm.h"
#include "palmcolormap.h"

enum CompressionType {COMP_NONE, COMP_SCANLINE, COMP_RLE, COMP_PACKBITS};

struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFilespec;  /* Filespecs of input files */
    const char * transparent;
    unsigned int depthSpec;
    unsigned int depth;
    unsigned int maxdepthSpec;
    unsigned int maxdepth;
    enum CompressionType compression;
    unsigned int verbose;
    unsigned int colormap;
    unsigned int offset;
    unsigned int density;
    unsigned int withdummy;
};



static void
parseCommandLine(int argc, const char ** argv, struct CmdlineInfo *cmdlineP) {
/*----------------------------------------------------------------------------
   Note that the file spec array we return is stored in the storage that
   was passed to us as the argv array.
-----------------------------------------------------------------------------*/
    optStruct3 opt;  /* set by OPTENT3 */
    optEntry *option_def;
    unsigned int option_def_index;

    unsigned int transSpec, densitySpec;
    unsigned int scanline_compression, rle_compression, packbits_compression;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENTRY */
    OPTENT3(0, "transparent",      OPT_STRING,
            &cmdlineP->transparent, &transSpec, 0);
    OPTENT3(0, "depth",            OPT_UINT,
            &cmdlineP->depth,       &cmdlineP->depthSpec, 0);
    OPTENT3(0, "maxdepth",         OPT_UINT,
            &cmdlineP->maxdepth,    &cmdlineP->maxdepthSpec, 0);
    OPTENT3(0, "scanline_compression", OPT_FLAG,
            NULL,                   &scanline_compression, 0);
    OPTENT3(0, "rle_compression",  OPT_FLAG,
            NULL,                   &rle_compression, 0);
    OPTENT3(0, "packbits_compression", OPT_FLAG,
            NULL,                   &packbits_compression, 0);
    OPTENT3(0, "verbose",          OPT_FLAG,
            NULL,                   &cmdlineP->verbose, 0);
    OPTENT3(0, "colormap",         OPT_FLAG,
            NULL,                   &cmdlineP->colormap, 0);
    OPTENT3(0, "offset",           OPT_FLAG,
            NULL,                   &cmdlineP->offset, 0);
    OPTENT3(0, "density",          OPT_UINT,
            &cmdlineP->density,     &densitySpec, 0);
    OPTENT3(0, "withdummy",        OPT_FLAG,
            NULL,                   &cmdlineP->withdummy, 0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE; /* We have some short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We have no parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdline_p and others. */

    if (cmdlineP->depthSpec) {
        if (cmdlineP->depth != 1 && cmdlineP->depth != 2
            && cmdlineP->depth != 4 && cmdlineP->depth != 8
            && cmdlineP->depth != 16)
            pm_error("invalid value for -depth: %u.  Valid values are "
                     "1, 2, 4, 8, and 16", cmdlineP->depth);
    }

    if (cmdlineP->maxdepthSpec) {
        if (cmdlineP->maxdepth != 1 && cmdlineP->maxdepth != 2
            && cmdlineP->maxdepth != 4 && cmdlineP->maxdepth != 8
            && cmdlineP->maxdepth != 16)
            pm_error("invalid value for -maxdepth: %u.  Valid values are "
                     "1, 2, 4, 8, and 16", cmdlineP->maxdepth);
    }

    if (cmdlineP->depthSpec && cmdlineP->maxdepthSpec &&
        cmdlineP->depth > cmdlineP->maxdepth)
        pm_error("-depth value (%u) is greater than -maxdepth (%u) value.",
                 cmdlineP->depth, cmdlineP->maxdepth);

    if (!transSpec)
        cmdlineP->transparent = NULL;

    if (densitySpec) {
        if (cmdlineP->density != PALM_DENSITY_LOW &&
            cmdlineP->density != PALM_DENSITY_ONEANDAHALF &&
            cmdlineP->density != PALM_DENSITY_DOUBLE &&
            cmdlineP->density != PALM_DENSITY_TRIPLE &&
            cmdlineP->density != PALM_DENSITY_QUADRUPLE)
            pm_error("Invalid value for -density: %u.  Valid values are "
                     "%u, %u, %u, %u and %u.", cmdlineP->density,
                     PALM_DENSITY_LOW, PALM_DENSITY_ONEANDAHALF,
                     PALM_DENSITY_DOUBLE, PALM_DENSITY_TRIPLE,
                     PALM_DENSITY_QUADRUPLE);
    } else
        cmdlineP->density = PALM_DENSITY_LOW;

    if (cmdlineP->density != PALM_DENSITY_LOW && cmdlineP->withdummy)
            pm_error("You can't specify -withdummy with -density value %u.  "
                     "It is valid only with low density (%u)",
                     cmdlineP->density, PALM_DENSITY_LOW);

    if (cmdlineP->withdummy && !cmdlineP->offset)
        pm_error("-withdummy does not make sense without -offset");

    if (scanline_compression + rle_compression + packbits_compression > 1)
        pm_error("You may specify only one of -scanline_compression, "
                 "-rle_compression, and -packbits_compression");
    else {
        if (scanline_compression)
            cmdlineP->compression = COMP_SCANLINE;
        else if (rle_compression)
            cmdlineP->compression = COMP_RLE;
        else if (packbits_compression)
            cmdlineP->compression = COMP_PACKBITS;
        else
            cmdlineP->compression = COMP_NONE;
    }

    if (argc-1 > 1)
        pm_error("This program takes at most 1 argument: the file name.  "
                 "You specified %u", argc-1);
    else if (argc-1 > 0)
        cmdlineP->inputFilespec = argv[1];
    else
        cmdlineP->inputFilespec = "-";
}



static xelval
scaleSample(pixval const arg,
            pixval const oldMaxval,
            pixval const newMaxval) {

    return (arg * newMaxval + oldMaxval/2) / oldMaxval;
}



static void
determinePalmFormatPgm(xelval               const maxval,
                       bool                 const bppSpecified,
                       unsigned int         const bpp,
                       bool                 const maxBppSpecified,
                       unsigned int         const maxBpp,
                       bool                 const wantCustomColormap,
                       enum CompressionType const compression,
                       bool                 const verbose,
                       unsigned int *       const bppP) {

    /* We can usually handle this one, but may not have enough pixels.  So
       check.
    */

    if (wantCustomColormap)
        pm_error("You specified -colormap with a black and white input"
                 "image.  -colormap is valid only with color.");
    if (bppSpecified)
        *bppP = bpp;
    else if (maxBppSpecified && (maxval >= (1 << maxBpp)))
        *bppP = maxBpp;
    else if (compression != COMP_NONE && maxval > 255)
        *bppP = 8;
    else if (maxval > 16)
        *bppP = 4;
    else {
        /* scale to minimum number of bpp needed */
        unsigned int bpp;
        for (bpp = 1;  (1 << bpp) < maxval;  bpp *= 2)
            ;
        *bppP = bpp;
    }
    if (verbose)
        pm_message("output is grayscale %u bits-per-pixel", *bppP);
}



static void
validateImageAgainstStandardColormap(const Colormap * const colormapP,
                                     xel **           const xels,
                                     unsigned int     const cols,
                                     unsigned int     const rows,
                                     xelval           const maxval) {
/*----------------------------------------------------------------------------
   Abort program if the image xels[][] (which is 'cols' x 'rows') contains a
   color not in the colormap *colormapP, giving an error message assuming the
   user chose the standard Palm colormap.
-----------------------------------------------------------------------------*/
    unsigned int row;

    for (row = 0; row < rows; ++row) {
        unsigned int col;

        for (col = 0; col < cols; ++col) {
            ColormapEntry const searchTarget =
                palmcolor_mapEntryColorFmPixel(xels[row][col], maxval, 255);

            ColormapEntry * const foundEntryP =
                (bsearch(&searchTarget,
                         colormapP->color_entries, colormapP->ncolors,
                         sizeof(ColormapEntry), palmcolor_compare_colors));
            if (!foundEntryP)
                pm_error(
                    "A color in the input image is not in the standard Palm "
                    "8-bit color palette.  Either adjust the colors in the "
                    "input with 'pnmremap' and the 'palmcolor8.map' file "
                    "(see manual) or specify -colormap or -depth=16");
        }
    }
}



static void
determinePalmFormatPpm(unsigned int         const cols,
                       unsigned int         const rows,
                       xelval               const maxval,
                       xel **               const xels,
                       bool                 const bppSpecified,
                       unsigned int         const bpp,
                       bool                 const maxBppSpecified,
                       unsigned int         const maxBpp,
                       bool                 const wantCustomColormap,
                       enum CompressionType const compression,
                       bool                 const verbose,
                       unsigned int *       const bppP,
                       bool *               const directColorP,
                       Colormap **          const colormapPP) {

    /* We don't attempt to identify PPM files that are actually
       monochrome.  So there are two options here: either 8-bit with a
       colormap, either the standard one or a custom one, or 16-bit direct
       color.  In the colormap case, if 'wantCustomColormap' is true (not
       recommended by Palm) we will put in our own colormap that has the
       colors of the input image; otherwise we will select the default
       Palm colormap and will fail if the input image has any colors that
       are not in that map (user should use Pnmremap and the
       palmcolor8.map file that comes with Netpbm to avoid this).  We try
       for colormapped first, since it works on more PalmOS devices.
    */
    if ((bppSpecified && bpp == 16) ||
        (!bppSpecified && maxBppSpecified && maxBpp == 16)) {
        /* we do the 16-bit direct color */
        *directColorP = TRUE;
        *colormapPP = NULL;
        *bppP = 16;
    } else if (!wantCustomColormap) {
        /* colormapped with the standard colormap */
        Colormap * colormapP;

        if ((bppSpecified && bpp != 8) || (maxBppSpecified && maxBpp < 8))
            pm_error("Must use depth of 8 for color Palm Bitmap without "
                     "custom color table.");
        colormapP = palmcolor_build_default_8bit_colormap();
        validateImageAgainstStandardColormap(colormapP,
                                             xels, cols, rows, maxval);

        *colormapPP = colormapP;
        *bppP = 8;
        *directColorP = FALSE;
        if (verbose)
            pm_message("Output is color with default colormap at 8 bpp");
    } else {
        /* colormapped with a custom colormap */
        *colormapPP =
            palmcolor_build_custom_8bit_colormap(xels, rows, cols, maxval);
        for (*bppP = 1; (1 << *bppP) < (*colormapPP)->ncolors; *bppP *= 2);
        if (bppSpecified) {
            if (bpp >= *bppP)
                *bppP = bpp;
            else
                pm_error("Too many colors for specified depth.  "
                         "Specified depth is %u bits; would need %u to "
                         "represent the %u colors in the image.  "
                         "Use pnmquant to reduce.",
                         maxBpp, *bppP, (*colormapPP)->ncolors);
        } else if (maxBppSpecified && maxBpp < *bppP) {
            pm_error("Too many colors for specified max depth.  "
                     "Specified maximum is %u bits; would need %u to "
                     "represent the %u colors in the image.  "
                     "Use pnmquant to reduce.",
                     maxBpp, *bppP, (*colormapPP)->ncolors);
        } else if (compression != COMP_NONE && *bppP > 8) {
            pm_error("Too many colors for a compressed image.  "
                     "Maximum is 256; the image has %u",
                     (*colormapPP)->ncolors);
        }
        *directColorP = FALSE;
        if (verbose)
            pm_message("Output is color with custom colormap "
                       "with %u colors at %u bpp",
                       (*colormapPP)->ncolors, *bppP);
    }
}



static void
determinePalmFormat(unsigned int         const cols,
                    unsigned int         const rows,
                    xelval               const maxval,
                    int                  const format,
                    xel **               const xels,
                    bool                 const bppSpecified,
                    unsigned int         const bpp,
                    bool                 const maxBppSpecified,
                    unsigned int         const maxBpp,
                    bool                 const wantCustomColormap,
                    enum CompressionType const compression,
                    bool                 const verbose,
                    unsigned int *       const bppP,
                    bool *               const directColorP,
                    Colormap **          const colormapPP) {
/*----------------------------------------------------------------------------
   Determine what kind of Palm output file to make.

   Also compute the colormap, if there is to be one.  This could be either one
   we make up, that needs to go into the image, or a standard one.
-----------------------------------------------------------------------------*/
    if (compression != COMP_NONE) {
        if (bppSpecified && bpp > 8)
            pm_error("You requested %u bits per pixel and compression.  "
                     "This program does not know how to generate a "
                     "compressed image with more than 8 bits per pixel",
                     bpp);
        if (maxBppSpecified && maxBpp > 8)
            pm_error("You requested %u max bits per pixel and compression.  "
                     "This program does not know how to generate a "
                     "compressed image with more than 8 bits per pixel",
                     maxBpp);
    }
    if (PNM_FORMAT_TYPE(format) == PBM_TYPE) {
        if (wantCustomColormap)
            pm_error("You specified -colormap with a black and white input "
                     "image.  -colormap is valid only with color.");
        if (bppSpecified)
            *bppP = bpp;
        else
            *bppP = 1;    /* no point in wasting bits */
        *directColorP = FALSE;
        *colormapPP = NULL;
        if (verbose)
            pm_message("output is black and white");
    } else if (PNM_FORMAT_TYPE(format) == PGM_TYPE) {
        determinePalmFormatPgm(maxval,
                               bppSpecified, bpp, maxBppSpecified, maxBpp,
                               wantCustomColormap, compression,
                               verbose,
                               bppP);

        *directColorP = FALSE;
        *colormapPP = NULL;
    } else if (PNM_FORMAT_TYPE(format) == PPM_TYPE) {
        determinePalmFormatPpm(cols, rows, maxval, xels, bppSpecified, bpp,
                               maxBppSpecified, maxBpp,
                               wantCustomColormap, compression, verbose,
                               bppP, directColorP, colormapPP);
    } else {
        pm_error("unknown format 0x%x on input file", (unsigned) format);
    }

    if (compression != COMP_NONE)
        assert(*bppP <= 8);
}



static const char *
formatName(int const format) {

    const char * retval;

    switch(PNM_FORMAT_TYPE(format)) {
    case PBM_TYPE: retval = "black and white"; break;
    case PGM_TYPE: retval = "grayscale";       break;
    case PPM_TYPE: retval = "color";           break;
    default:       retval = "???";             break;
    }
    return retval;
}



static void
findTransparentColor(const char *   const colorSpec,
                     pixval         const newMaxval,
                     bool           const directColor,
                     pixval         const maxval,
                     Colormap *     const colormapP,
                     xel *          const transcolorP,
                     unsigned int * const transindexP) {

    *transcolorP = ppm_parsecolor(colorSpec, maxval);
    if (!directColor) {
        ColormapEntry const searchTarget =
            palmcolor_mapEntryColorFmPixel(*transcolorP, maxval, newMaxval);
        ColormapEntry * const foundEntryP =
            (bsearch(&searchTarget,
                     colormapP->color_entries, colormapP->ncolors,
                     sizeof(ColormapEntry), palmcolor_compare_colors));
        if (!foundEntryP) {
            pm_error("Specified transparent color %s not found "
                     "in colormap.", colorSpec);
        } else
            *transindexP = (*foundEntryP >> 24) & 0xFF;
    }
}



static unsigned int
bitmapVersion(unsigned int         const bpp,
              bool                 const colormapped,
              bool                 const transparent,
              enum CompressionType const compression,
              unsigned int         const density) {
/*----------------------------------------------------------------------------
   Return the version number of the oldest version that can represent
   the specified attributes.
-----------------------------------------------------------------------------*/
    unsigned int version;
    /* we need Version 1 if we use more than 1 bpp,
       Version 2 if we use compression or transparency,
       Version 3 if density is 108 or higher
    */
    if (density > PALM_DENSITY_LOW)
        version = 3;
    else if (transparent || compression != COMP_NONE)
        version = 2;
    else if (bpp > 1 || colormapped)
        version = 1;
    else
        version = 0;

    return version;
}



static void
writeCommonHeader(unsigned int         const cols,
                  unsigned int         const rows,
                  unsigned int         const rowbytes,
                  enum CompressionType const compression,
                  bool                 const colormapped,
                  bool                 const transparent,
                  bool                 const directColor,
                  unsigned int         const bpp,
                  unsigned int         const version) {
/*----------------------------------------------------------------------------
   Write the first 10 bytes of the Palm Bitmap header.
   These are common to all encodings (versions 0, 1, 2 and 3).
-----------------------------------------------------------------------------*/
    unsigned short flags;

    if (cols > USHRT_MAX)
        pm_error("Too many columns for Palm Bitmap: %u", cols);
    pm_writebigshort(stdout, cols);    /* width */
    if (rows > USHRT_MAX)
        pm_error("Too many rows for Palm Bitmap: %u", rows);
    pm_writebigshort(stdout, rows);    /* height */
    if (rowbytes > USHRT_MAX)
        pm_error("Too many bytes per row for Palm Bitmap: %u", rowbytes);
    pm_writebigshort(stdout, rowbytes);

    flags = 0;  /* initial value */
    if (compression != COMP_NONE)
        flags |= PALM_IS_COMPRESSED_FLAG;
    if (colormapped)
        flags |= PALM_HAS_COLORMAP_FLAG;
    if (transparent)
        flags |= PALM_HAS_TRANSPARENCY_FLAG;
    if (directColor)
        flags |= PALM_DIRECT_COLOR_FLAG;
    pm_writebigshort(stdout, flags);
    assert(bpp <= UCHAR_MAX);
    fputc(bpp, stdout);

    fputc(version, stdout);
}



static unsigned char
compressionFieldValue(enum CompressionType const compression) {

    unsigned char retval;

    switch (compression) {
    case COMP_SCANLINE:
        retval = PALM_COMPRESSION_SCANLINE;
        break;
    case COMP_RLE:
        retval = PALM_COMPRESSION_RLE;
        break;
    case COMP_PACKBITS:
        retval = PALM_COMPRESSION_PACKBITS;
        break;
    case COMP_NONE:
        retval = 0x00;  /* empty */
        break;
    }
    return retval;
}



static void
writeRemainingHeaderLow(unsigned int         const nextDepthOffset,
                        unsigned int         const transindex,
                        enum CompressionType const compression,
                        unsigned int         const bpp) {
/*----------------------------------------------------------------------------
   Write last 6 bytes of a low density Palm Bitmap header.
-----------------------------------------------------------------------------*/
    if (nextDepthOffset > USHRT_MAX)
        pm_error("Image too large for Palm Bitmap");

    pm_writebigshort(stdout, nextDepthOffset);

    if (bpp != 16) {
        assert(transindex <= UCHAR_MAX);
        fputc(transindex, stdout);    /* transparent index */
    } else
        fputc(0, stdout);    /* the DirectInfoType will hold this info */

    fputc(compressionFieldValue(compression), stdout);

    pm_writebigshort(stdout, 0);  /* reserved by Palm */
}



static void
writeRemainingHeaderHigh(unsigned int         const bpp,
                         enum CompressionType const compression,
                         unsigned int         const density,
                         xelval               const maxval,
                         bool                 const transparent,
                         xel                  const transcolor,
                         unsigned int         const transindex,
                         unsigned int         const nextBitmapOffset) {
/*----------------------------------------------------------------------------
   Write last 16 bytes of a high density Palm Bitmap header.
-----------------------------------------------------------------------------*/
    if ((nextBitmapOffset >> 31) > 1)
        pm_error("Image too large for Palm Bitmap.  nextBitmapOffset "
            "value doesn't fit in 4 bytes");

    fputc(0x18, stdout); /* size of this high density header */

    if (bpp != 16)
        fputc(PALM_FORMAT_INDEXED, stdout);
    else
        fputc(PALM_FORMAT_565, stdout);

    fputc(0x00, stdout); /* unused */

    fputc(compressionFieldValue(compression), stdout);

    pm_writebigshort(stdout, density);

    if (transparent) {
        if (bpp == 16) {
            /* Blind guess here */
            fputc(0, stdout);
            fputc(scaleSample(PPM_GETR(transcolor), maxval, 255), stdout);
            fputc(scaleSample(PPM_GETG(transcolor), maxval, 255), stdout);
            fputc(scaleSample(PPM_GETB(transcolor), maxval, 255), stdout);
        } else {
            assert(transindex <= UCHAR_MAX);
            fputc(0, stdout);
            fputc(0, stdout);
            fputc(0, stdout);
            fputc(transindex, stdout);   /* transparent index */
        }
    } else
        pm_writebiglong(stdout, 0);

    pm_writebiglong(stdout, nextBitmapOffset);
}



static void
writeDummy() {
/*----------------------------------------------------------------------------
   Write a dummy Palm Bitmap header.  This is a 16 byte header, of
   type version 1 and with (only) pixelSize set to 0xFF.

   An old viewer will see this as invalid because of the pixelSize, and stop
   reading the stream.  A new viewer will recognize this for what it is
   (a dummy header designed to stop old viewers from reading further in
   the stream) and continue reading the stream.  Presumably, what follows
   in the stream is understandable by a new viewer, but would confuse an
   old one.
-----------------------------------------------------------------------------*/
    pm_writebiglong(stdout, 0x00);
    pm_writebiglong(stdout, 0x00);
    fputc(0xFF, stdout);               /* pixelSize */
    fputc(0x01, stdout);               /* version */
    pm_writebigshort(stdout, 0x00);
    pm_writebiglong(stdout, 0x00);
}



static void
writeColormap(bool         const explicitColormap,
              Colormap *   const colormapP,
              bool         const directColor,
              unsigned int const bpp,
              bool         const transparent,
              xel          const transcolor,
              xelval       const maxval,
              unsigned int const version) {

    /* if there's a colormap, write it out */
    if (explicitColormap) {
        unsigned int row;
        if (!colormapP)
            pm_error("Internal error: user specified -colormap, but we did "
                     "not generate a colormap.");
        qsort(colormapP->color_entries, colormapP->ncolors,
              sizeof(ColormapEntry), palmcolor_compare_indices);
        pm_writebigshort( stdout, colormapP->ncolors );
        for (row = 0;  row < colormapP->ncolors; ++row)
            pm_writebiglong (stdout, colormapP->color_entries[row]);
    }

    if (directColor && (version < 3)) {
        /* write the DirectInfoType (8 bytes) */
        if (bpp == 16) {
            fputc(5, stdout);   /* # of bits of red */
            fputc(6, stdout);   /* # of bits of green */
            fputc(5, stdout);   /* # of bits of blue */
            fputc(0, stdout);   /* reserved by Palm */
        } else
            pm_error("Don't know how to create %u bit DirectColor bitmaps.",
                     bpp);
        if (transparent) {
            fputc(0, stdout);
            fputc(scaleSample(PPM_GETR(transcolor) , maxval, 255), stdout);
            fputc(scaleSample(PPM_GETG(transcolor) , maxval, 255), stdout);
            fputc(scaleSample(PPM_GETB(transcolor) , maxval, 255), stdout);
        } else
            pm_writebiglong(stdout, 0);     /* no transparent color */
    }
}



static void
computeRawRowDirectColor(const xel *     const xelrow,
                         unsigned int    const cols,
                         xelval          const maxval,
                         unsigned char * const rowdata) {
/*----------------------------------------------------------------------------
  Compute a row of Palm data in raw (uncompressed) form for an image that
  uses direct color (really, true color: each pixel contains RGB intensities
  as distinct R, G, and B numbers).

  In this format, each pixel is 16 bits: 5 red, 6 green, 5 blue.

  'xelrow' is the image contents of row.  It is 'cols' columns wide and
  samples are based on maxval 'maxval'.

  Put the output data at 'rowdata'.
-----------------------------------------------------------------------------*/
    unsigned int col;
    unsigned char * outCursor;

    for (col = 0, outCursor = &rowdata[0]; col < cols; ++col) {
        unsigned int const color =
            (scaleSample(PPM_GETR(xelrow[col]), maxval, 31) << 11) |
            (scaleSample(PPM_GETG(xelrow[col]), maxval, 63) <<  5) |
            (scaleSample(PPM_GETB(xelrow[col]), maxval, 31) <<  0);

        *outCursor++ = (color >> 8) & 0xFF;
        *outCursor++ = color & 0xFF;
    }
}



static void
computeRawRowNonDirect(const xel *     const xelrow,
                       unsigned int    const cols,
                       xelval          const maxval,
                       unsigned int    const bpp,
                       Colormap *      const colormapP,
                       unsigned int    const newMaxval,
                       unsigned char * const rowdata) {
/*----------------------------------------------------------------------------
  Compute a row of Palm data in raw (uncompressed) form for an image that
  does not have a raster whose elements are explicit R, G, and B
  intensities.

  If 'colormapP' is non-null, the pixel is an index into that colormap.
  'newMaxval' is meaningless.

  If 'colormapP' is null, the pixel is a grayscale intensity, on a scale with
  maximum value 'newMaxval'.  (N.B. this is really direct color, but for some
  reason it's historically lumped in with the paletted formats).

  'xelrow' is the image contents of row.  It is 'cols' columns wide and
  samples are based on maxval 'maxval'.

  Put the output data at 'rowdata', using 'bpp' bits per pixel.
-----------------------------------------------------------------------------*/
    unsigned int col;
    unsigned char * outCursor;
        /* Points to next slot in 'rowdata' we will fill */
    unsigned char outbyte;
        /* Accumulated bits to be output */
    unsigned char outbit;
        /* The lowest bit number we want to access for this pixel */

    outbyte = 0x00;  /* initial value */
    outCursor = &rowdata[0];  /* Start at the beginning of the row */

    for (outbit = 8 - bpp, col = 0; col < cols; ++col) {
        unsigned int color;
        if (!colormapP) {
            /* we assume grayscale, and use simple scaling */
            color = (PNM_GET1(xelrow[col]) * newMaxval)/maxval;
            if (color > newMaxval)
                pm_error("oops.  Bug in color re-calculation code.  "
                         "color of %u.", color);
            color = newMaxval - color; /* note grayscale maps are inverted */
        } else {
            ColormapEntry const searchTarget =
                palmcolor_mapEntryColorFmPixel(xelrow[col], maxval, 255);
            ColormapEntry * const foundEntryP =
                bsearch(&searchTarget,
                        colormapP->color_entries,
                        colormapP->ncolors,
                        sizeof(ColormapEntry),
                        palmcolor_compare_colors);
            if (!foundEntryP) {
                pm_error("INERNAL ERROR: "
                         "Color (%u,%u,%u) not found in colormap, "
                         "though it was supposedly there before",
                         PPM_GETR(xelrow[col]),
                         PPM_GETG(xelrow[col]),
                         PPM_GETB(xelrow[col]));
            }
            color = (*foundEntryP >> 24) & 0xFF;
        }

        if (color > newMaxval)
            pm_error("oops.  Bug in color re-calculation code.  "
                     "color of %u.", color);
        outbyte |= (color << outbit);
        if (outbit == 0) {
            /* Bit buffer is full.  Flush to to rowdata. */
            *outCursor++ = outbyte;
            outbyte = 0x00;
            outbit = 8 - bpp;
        } else
            outbit -= bpp;
    }
    if ((cols % (8 / bpp)) != 0) {
        /* Flush bits remaining in the bit buffer to rowdata */
        *outCursor++ = outbyte;
    }
}


typedef struct {
/*----------------------------------------------------------------------------
   A buffer to which one can write bytes sequentially.
-----------------------------------------------------------------------------*/
    char * buffer;
    unsigned int allocatedSize;
    unsigned int occupiedSize;
} SeqBuffer;


static void
seqBuffer_create(SeqBuffer ** const bufferPP) {

    SeqBuffer * bufferP;

    MALLOCVAR_NOFAIL(bufferP);

    bufferP->allocatedSize = 4096;
    MALLOCARRAY(bufferP->buffer, bufferP->allocatedSize);
    if (bufferP == NULL)
        pm_error("Unable to allocate %u bytes of buffer",
                 bufferP->allocatedSize);
    bufferP->occupiedSize = 0;

    *bufferPP = bufferP;
}



static void
seqBuffer_destroy(SeqBuffer * const bufferP) {

    free(bufferP->buffer);
    free(bufferP);
}



static void
seqBuffer_addByte(SeqBuffer *   const bufferP,
                  unsigned char const newByte) {
/*-----------------------------------------------------------------------------
  Append one byte to buffer, expanding with realloc() whenever necessary.

  Buffer is initially 4096 bytes.  It is doubled with each expansion.
  A combination of large image size (maximum 65535 x 65535), high
  resolution (each pixel can occupy more than one byte) and poor
  compression can lead to an arithmetic overflow.
  Abort with error if an arithmetic overflow is detected during doubling.
-----------------------------------------------------------------------------*/
    assert(bufferP->allocatedSize >= bufferP->occupiedSize);

    if (bufferP->allocatedSize == bufferP->occupiedSize) {
        unsigned int const newSize = bufferP->allocatedSize * 2;

        if (newSize <= bufferP->allocatedSize)
            pm_error("Image too large.  Arithmetic overflow trying to "
                     "expand buffer beyond %u bytes.",
                     bufferP->allocatedSize);

        REALLOCARRAY(bufferP->buffer, newSize);
        if (bufferP->buffer == NULL)
            pm_error("Couldn't (re)allocate %u bytes of memory "
                     "for buffer.", newSize);

        bufferP->allocatedSize = newSize;
    }
    bufferP->buffer[bufferP->occupiedSize++] = newByte;
}



static unsigned int
seqBuffer_length(SeqBuffer * const bufferP) {
    return bufferP->occupiedSize;
}



static void
seqBuffer_writeOut(SeqBuffer * const bufferP,
                   FILE *      const fileP) {

    size_t bytesWritten;

    bytesWritten = fwrite(bufferP->buffer, sizeof(char),
                          bufferP->occupiedSize, fileP);

    if (bytesWritten != bufferP->occupiedSize)
        pm_error("fwrite() failed to write out the buffer.");
}



static void
copyRowToBuffer(const unsigned char * const rowdata,
                unsigned int          const rowbytes,
                SeqBuffer *           const rasterBufferP) {

    unsigned int pos;
    for (pos = 0; pos < rowbytes; ++pos)
        seqBuffer_addByte(rasterBufferP, rowdata[pos]);
}



static void
scanlineCompressAndBufferRow(const unsigned char * const rowdata,
                             unsigned int          const rowbytes,
                             SeqBuffer *           const rasterBufferP,
                             const unsigned char * const lastrow) {
/*----------------------------------------------------------------------------
   Take the raw Palm Bitmap row 'rowdata', which is 'rowbytes'
   columns, and add the scanline-compressed representation of it to
   the buffer with handle 'rasterBufferP'.

   'lastrow' is the raw contents of the row immediately before the one
   we're compressing -- i.e. we compress with respect to that row.  This
   function does not work on the first row of an image.
-----------------------------------------------------------------------------*/
    unsigned int pos;

    for (pos = 0;  pos < rowbytes;  pos += 8) {
        unsigned int const limit = MIN(rowbytes - pos, 8);

        unsigned char map;
            /* mask indicating which of the next 8 pixels are
               different from the previous row, and therefore present
               in the file immediately following the map byte.
            */
        unsigned char differentPixels[8];
        unsigned char *outptr;
        unsigned char outbit;

        for (outbit = 0, map = 0x00, outptr = differentPixels;
             outbit < limit;
             ++outbit) {
            if (!lastrow
                || (lastrow[pos + outbit] != rowdata[pos + outbit])) {
                map |= (1 << (7 - outbit));
                *outptr++ = rowdata[pos + outbit];
            }
        }

        seqBuffer_addByte(rasterBufferP, map);
        {
            unsigned int j;
            for (j = 0; j < (outptr - differentPixels); ++j)
                seqBuffer_addByte(rasterBufferP, differentPixels[j]);
        }
    }
}



static void
rleCompressAndBufferRow(const unsigned char * const rowdata,
                        unsigned int          const rowbytes,
                        SeqBuffer *           const rasterBufferP) {
/*----------------------------------------------------------------------------
   Take the raw Palm Bitmap row 'rowdata', which is 'rowbytes' bytes,
   and add the rle-compressed representation of it to the buffer with
   handle 'rasterBufferP'.
-----------------------------------------------------------------------------*/
    unsigned int pos;

    /* we output a count of the number of bytes a value is
       repeated, followed by that byte value
    */
    pos = 0;
    while (pos < rowbytes) {
        unsigned int repeatcount;
        for (repeatcount = 1;
             repeatcount < (rowbytes - pos) && repeatcount  < 255;
             ++repeatcount)
            if (rowdata[pos + repeatcount] != rowdata[pos])
                break;

        seqBuffer_addByte(rasterBufferP, repeatcount);
        seqBuffer_addByte(rasterBufferP, rowdata[pos]);
        pos += repeatcount;
    }
}



static void
packbitsCompressAndBufferRow(const unsigned char * const rowdata,
                             unsigned int          const rowbytes,
                             SeqBuffer *           const rasterBufferP) {
/*----------------------------------------------------------------------------
   Take the raw Palm Bitmap row 'rowdata', which is 'rowbytes' bytes, and
   add the packbits-compressed representation of it to the buffer
   with handle 'rasterBufferP'.
-----------------------------------------------------------------------------*/
    unsigned char * compressedData;
    size_t          compressedDataCt;
    unsigned int    byteCt;

    pm_rlenc_allocoutbuf(&compressedData, rowbytes, PM_RLE_PACKBITS);
    pm_rlenc_compressbyte(rowdata, compressedData, PM_RLE_PACKBITS,
                          rowbytes, &compressedDataCt);

    for (byteCt = 0; byteCt < compressedDataCt; ++byteCt)
        seqBuffer_addByte(rasterBufferP, compressedData[byteCt]);

    free(compressedData);
}



static void
bufferRowFromRawRowdata(const unsigned char *  const rowdata,
                        unsigned int           const rowbytes,
                        enum CompressionType   const compression,
                        const unsigned char *  const lastrow,
                        SeqBuffer *            const rasterBufferP) {
/*----------------------------------------------------------------------------
   Starting with a raw (uncompressed) Palm raster line, do the
   compression identified by 'compression' and add the compressed row
   to the buffer with handle 'rasterBufferP'.

   If 'compression' indicates scanline compression, 'lastrow' is the
   row immediately preceding this one in the image (and this function
   doesn't work on the first row of an image).  Otherwise, 'lastrow'
   is meaningless.
-----------------------------------------------------------------------------*/
    switch (compression) {
    case COMP_NONE:
        copyRowToBuffer(rowdata, rowbytes, rasterBufferP);
        break;
    case COMP_SCANLINE:
        scanlineCompressAndBufferRow(rowdata, rowbytes, rasterBufferP,
                                     lastrow);
        break;
    case COMP_RLE:
        rleCompressAndBufferRow(rowdata, rowbytes, rasterBufferP);
        break;
    case COMP_PACKBITS:
        packbitsCompressAndBufferRow(rowdata, rowbytes, rasterBufferP);
        break;
    }
}



static void
bufferRow(const xel *          const xelrow,
          unsigned int         const cols,
          xelval               const maxval,
          unsigned int         const rowbytes,
          unsigned int         const bpp,
          unsigned int         const newMaxval,
          enum CompressionType const compression,
          bool                 const directColor,
          Colormap *           const colormapP,
          unsigned char *      const rowdata,
          unsigned char *      const lastrow,
          SeqBuffer *          const rasterBufferP) {
/*----------------------------------------------------------------------------
   Add a row of the Palm Bitmap raster to buffer 'rasterBufferP'.

   'xelrow' is the image contents of row.  It is 'cols' columns wide and
   samples are based on maxval 'maxval'.

   If 'compression' indicates scanline compression, 'lastrow' is the
   row immediately preceding this one in the image (and this function
   doesn't work on the first row of an image).  Otherwise, 'lastrow'
   is meaningless.

   'rowdata' is a work buffer 'rowbytes' in size.
-----------------------------------------------------------------------------*/
    if (directColor)
        computeRawRowDirectColor(xelrow, cols, maxval, rowdata);
    else
        computeRawRowNonDirect(xelrow, cols, maxval, bpp, colormapP, newMaxval,
                               rowdata);

    bufferRowFromRawRowdata(rowdata, rowbytes, compression,
                            lastrow, rasterBufferP);
}



static void
bufferRaster(xel **               const xels,
             unsigned int         const cols,
             unsigned int         const rows,
             xelval               const maxval,
             unsigned int         const rowbytes,
             unsigned int         const bpp,
             unsigned int         const newMaxval,
             enum CompressionType const compression,
             bool                 const directColor,
             Colormap *           const colormapP,
             SeqBuffer **         const rasterBufferPP) {

    unsigned char * rowdata;
    unsigned char * lastrow;
    unsigned int row;

    seqBuffer_create(rasterBufferPP);

    MALLOCARRAY_NOFAIL(rowdata, rowbytes);
    if (compression == COMP_SCANLINE)
        MALLOCARRAY_NOFAIL(lastrow, rowbytes);
    else
        lastrow = NULL;

    /* clear pad bytes to suppress valgrind error */
    rowdata[rowbytes - 1] = rowdata[rowbytes - 2] = 0x00;

    /* And write out the data. */
    for (row = 0; row < rows; ++row) {
        bufferRow(xels[row], cols, maxval, rowbytes, bpp, newMaxval,
                  compression,
                  directColor, colormapP, rowdata, row > 0 ? lastrow : NULL,
                  *rasterBufferPP);

        if (compression == COMP_SCANLINE)
            memcpy(lastrow, rowdata, rowbytes);
    }
    free(rowdata);
    if (compression == COMP_SCANLINE)
        free(lastrow);
}



static void
computeOffsetStuff(bool                 const offsetWanted,
                   unsigned int         const version,
                   bool                 const directColor,
                   enum CompressionType const compression,
                   bool                 const colormapped,
                   unsigned int         const colormapColorCount,
                   unsigned int         const sizePlusRasterSize,
                   unsigned int *       const nextDepthOffsetP,
                   unsigned int *       const nextBitmapOffsetP,
                   unsigned int *       const padBytesRequiredP) {

    if (offsetWanted) {
        /* Offset is measured in 4-byte words (double words in
           Intel/Microsoft terminology).  Account for header,
           colormap, and raster size and round up
        */
        unsigned int const headerSize = ((version < 3) ? 16 : 24);
        unsigned int const colormapSize =
            (colormapped ? (2 + colormapColorCount * 4) : 0);
        if (version < 3) {
            unsigned int const directSize =
                (directColor && version < 3) ? 8 : 0;
            if (compression != COMP_NONE && sizePlusRasterSize > USHRT_MAX)
                pm_error("Oversized compressed bitmap: %u bytes",
                         sizePlusRasterSize);
            *padBytesRequiredP = 4 - (sizePlusRasterSize + headerSize +
                                      directSize + colormapSize) % 4;
            *nextDepthOffsetP =
                (sizePlusRasterSize + headerSize +
                 directSize + colormapSize + *padBytesRequiredP) / 4;
        } else {
            if (compression != COMP_NONE && (sizePlusRasterSize >> 31) > 1)
                pm_error("Oversized compressed bitmap: %u bytes",
                         sizePlusRasterSize);
            /* Does version 3 need padding? Probably won't hurt */
            *padBytesRequiredP = 4 - (sizePlusRasterSize + headerSize +
                                      colormapSize) % 4;
            *nextBitmapOffsetP = sizePlusRasterSize + headerSize +
                colormapSize + *padBytesRequiredP;
        }
    } else {
        *padBytesRequiredP = 0;
        *nextDepthOffsetP = 0;
        *nextBitmapOffsetP = 0;
    }
}



static void
writeRasterSize(unsigned int const sizePlusRasterSize,
                unsigned int const version,
                FILE *       const fileP) {
/*----------------------------------------------------------------------------
   Write to file 'fileP' a raster size field for a Palm Bitmap version
   'version' header, indicating 'sizePlusRasterSize' bytes.
-----------------------------------------------------------------------------*/
    if (version < 3)
        pm_writebigshort(fileP, sizePlusRasterSize);
    else
        pm_writebiglong(fileP, sizePlusRasterSize);
}



static void
writeBitmap(xel **               const xels,
            unsigned int         const cols,
            unsigned int         const rows,
            xelval               const maxval,
            unsigned int         const rowbytes,
            unsigned int         const bpp,
            unsigned int         const newMaxval,
            enum CompressionType const compression,
            bool                 const transparent,
            bool                 const directColor,
            bool                 const offsetWanted,
            bool                 const colormapped,
            Colormap *           const colormapP,
            unsigned int         const transindex,
            xel                  const transcolor,
            unsigned int         const version,
            unsigned int         const density,
            bool                 const withdummy) {

    unsigned int sizePlusRasterSize;
    unsigned int nextDepthOffset;
    unsigned int nextBitmapOffset;
        /* Offset from the beginning of the image we write to the beginning
           of the next one, assuming user writes another one following this
           one.
           nextDepthOffset is used in encodings 1, 2 and is in 4 byte words
           nextBitmapOffset is used in encoding 3, is in 4 bytes
        */
    unsigned int padBytesRequired;
        /* Number of bytes of padding we need to put after the image in
           order to align properly for User to add the next image to the
           stream.
        */
    SeqBuffer * rasterBufferP;

    writeCommonHeader(cols, rows, rowbytes, compression, colormapped,
                      transparent, directColor, bpp, version);

    bufferRaster(xels, cols, rows, maxval, rowbytes, bpp, newMaxval,
                 compression, directColor, colormapP, &rasterBufferP);

    /* rasterSize itself takes 2 or 4 bytes */
    if (version < 3)
        sizePlusRasterSize = 2 + seqBuffer_length(rasterBufferP);
    else
        sizePlusRasterSize = 4 + seqBuffer_length(rasterBufferP);

    computeOffsetStuff(offsetWanted, version, directColor, compression,
                       colormapped, colormapped ? colormapP->ncolors : 0,
                       sizePlusRasterSize,
                       &nextDepthOffset, &nextBitmapOffset,
                       &padBytesRequired);

    if (version < 3)
        writeRemainingHeaderLow(nextDepthOffset, transindex, compression, bpp);
    else
        writeRemainingHeaderHigh(bpp, compression, density,
                                 maxval, transparent, transcolor,
                                 transindex, nextBitmapOffset);

    writeColormap(colormapped, colormapP, directColor, bpp,
                  transparent, transcolor, maxval, version);

    if (compression != COMP_NONE)
        writeRasterSize(sizePlusRasterSize, version, stdout);

    seqBuffer_writeOut(rasterBufferP, stdout);

    seqBuffer_destroy(rasterBufferP);

    {
        unsigned int i;
        for (i = 0; i < padBytesRequired; ++i)
            fputc(0x00, stdout);
    }

    if (withdummy)
        writeDummy();
}



int
main( int argc, const char **argv ) {
    struct CmdlineInfo cmdline;
    unsigned int version;
    FILE* ifP;
    xel** xels;
    xel transcolor;
    unsigned int transindex;
    int rows, cols;
    unsigned int rowbytes;
    xelval maxval;
    int format;
    unsigned int bpp;
    bool directColor;
    unsigned int newMaxval;
    Colormap * colormapP;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFilespec);

    xels = pnm_readpnm(ifP, &cols, &rows, &maxval, &format);
    pm_close(ifP);

    if (cmdline.verbose)
        pm_message("Input is %ux%u %s, maxval %u",
                   cols, rows, formatName(format), maxval);

    determinePalmFormat(cols, rows, maxval, format, xels,
                        cmdline.depthSpec, cmdline.depth,
                        cmdline.maxdepthSpec, cmdline.maxdepth,
                        cmdline.colormap, cmdline.compression, cmdline.verbose,
                        &bpp, &directColor, &colormapP);

    newMaxval = (1 << bpp) - 1;

    if (cmdline.transparent)
        findTransparentColor(cmdline.transparent, newMaxval, directColor,
                             maxval, colormapP, &transcolor, &transindex);
    else
        transindex = 0;

    rowbytes = ((cols + (16 / bpp -1)) / (16 / bpp)) * 2;
        /* bytes per row - always a word boundary */

    version = bitmapVersion(bpp, cmdline.colormap, !!cmdline.transparent,
                            cmdline.compression, cmdline.density);

    writeBitmap(xels, cols, rows, maxval,
                rowbytes, bpp, newMaxval, cmdline.compression,
                !!cmdline.transparent, directColor, cmdline.offset,
                cmdline.colormap, colormapP, transindex, transcolor,
                version, cmdline.density, cmdline.withdummy);

    return 0;
}
