/* rasttopnm.c - read a Sun rasterfile and produce a portable anymap
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

#include "pm_c_util.h"
#include "mallocvar.h"
#include "shhopt.h"
#include "pnm.h"
#include "rast.h"



struct cmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFileName;
    unsigned int index;
    unsigned int dumpheader;
    unsigned int dumpcolormap;
};



static void
parseCommandLine(int argc, const char ** argv,
                 struct cmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   Note that the file spec array we return is stored in the storage that
   was passed to us as the argv array.
-----------------------------------------------------------------------------*/
    optEntry * option_def;   /* Used by OPTENT3 */
    optStruct3 opt;

    unsigned int option_def_index;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */

    opt.opt_table = option_def;
    opt.short_allowed = false;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = false;  /* We have no parms that are negative numbers */

    OPTENT3(0,   "index",        OPT_FLAG,   NULL,
            &cmdlineP->index,          0);
    OPTENT3(0,   "dumpheader",   OPT_FLAG,   NULL,
            &cmdlineP->dumpheader,     0);
    OPTENT3(0,   "dumpcolormap", OPT_FLAG,   NULL,
            &cmdlineP->dumpcolormap,   0);

    pm_optParseOptions4(&argc, argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (argc-1 == 0)
        cmdlineP->inputFileName = "-";
    else if (argc-1 != 1)
        pm_error("Program takes zero or one argument (filename).  You "
                 "specified %d", argc-1);
    else
        cmdlineP->inputFileName = argv[1];

    free(option_def);
}



static bool
colorMapIsGrayscale(colormap_t const colorMap) {
/*----------------------------------------------------------------------------
   The color map contains only gray.
-----------------------------------------------------------------------------*/
    unsigned int i;
    bool grayscale;

    for (i = 0, grayscale = true; i < colorMap.length; ++i) {
        if (colorMap.map[CMX_RED][i] != colorMap.map[CMX_GRN][i] ||
            colorMap.map[CMX_GRN][i] != colorMap.map[CMX_BLU][i]) {
            grayscale = false;
        }
    }
    return grayscale;
}



static void
analyzeImage(struct rasterfile const header,
             colormap_t        const colorMap,
             int *             const formatP,
             xelval *          const maxvalP,
             bool *            const grayscaleP,
             xel *             const zeroP,
             xel *             const oneP) {

    bool const grayscale =
        header.ras_maplength == 0 || colorMapIsGrayscale(colorMap);

    *grayscaleP = grayscale;

    switch (header.ras_depth) {
    case 1:
        if (header.ras_maptype == RMT_NONE && header.ras_maplength == 0) {
            *maxvalP = 1;
            *formatP = PBM_TYPE;
            PNM_ASSIGN1(*zeroP, 1);
            PNM_ASSIGN1(*oneP, 0);
        } else if (header.ras_maptype == RMT_EQUAL_RGB &&
                   header.ras_maplength == 3*2) {

            if (grayscale) {
                *maxvalP = 255;
                *formatP = PGM_TYPE;
                PNM_ASSIGN1(*zeroP, colorMap.map[CMX_RED][0]);
                PNM_ASSIGN1(*oneP,  colorMap.map[CMX_RED][1]);
            } else {
                *maxvalP = 255;
                *formatP = PPM_TYPE;
                PPM_ASSIGN(*zeroP,
                           colorMap.map[CMX_RED][0],
                           colorMap.map[CMX_GRN][0],
                           colorMap.map[CMX_BLU][0]);
                PPM_ASSIGN(*oneP,
                           colorMap.map[CMX_RED][1],
                           colorMap.map[CMX_GRN][1],
                           colorMap.map[CMX_BLU][1]);
            }
        } else
            pm_error(
                "this depth-1 rasterfile has a non-standard colormap - "
                "type %ld length %ld",
                header.ras_maptype, header.ras_maplength);
        break;

    case 8:
        if (grayscale) {
            *maxvalP = 255;
            *formatP = PGM_TYPE;
        } else if (header.ras_maptype == RMT_EQUAL_RGB) {
            *maxvalP = 255;
            *formatP = PPM_TYPE;
        } else
            pm_error(
                "this depth-8 rasterfile has a non-standard colormap - "
                "type %ld length %ld",
                header.ras_maptype, header.ras_maplength);
        break;

    case 24:
    case 32:
        if (header.ras_maptype == RMT_NONE && header.ras_maplength == 0);
        else if (header.ras_maptype == RMT_RAW);
        else if (header.ras_maplength == 3*256);
        else
            pm_error(
                "this depth-%ld rasterfile has a non-standard colormap - "
                "type %ld length %ld",
                header.ras_depth, header.ras_maptype, header.ras_maplength);
        *maxvalP = 255;
        *formatP = PPM_TYPE;
        break;

    default:
        pm_error("invalid depth: %ld.  Can handle only depth 1, 8, 24, or 32.",
                 header.ras_depth);
    }
}



static void
reportOutputType(int const format) {

    switch (PNM_FORMAT_TYPE(format)) {
    case PBM_TYPE:
        pm_message("writing PBM file");
        break;
    case PGM_TYPE:
        pm_message("writing PGM file");
        break;
    case PPM_TYPE:
        pm_message("writing PPM file");
        break;
    default:
        abort();
    }
}



static void
convertRowDepth1(const unsigned char * const rastLine,
                 unsigned int          const cols,
                 xel                   const zeroXel,
                 xel                   const oneXel,
                 xel *                 const xelrow) {

    unsigned int  col;
    unsigned char mask;
    unsigned int  lineCursor;

    for (col = 0, mask = 0x80, lineCursor = 0; col < cols; ++col) {
        if (mask == 0x00) {
            ++lineCursor;
            mask = 0x80;
        }
        xelrow[col] = (rastLine[lineCursor] & mask) ? oneXel : zeroXel;
        mask = mask >> 1;
    }
}



static void
convertRowDepth8(const unsigned char * const rastLine,
                 unsigned int          const cols,
                 bool                  const colorMapped,
                 bool                  const useIndexForColor,
                 bool                  const grayscale,
                 colormap_t            const colorMap,
                 xel *                 const xelrow) {
/*----------------------------------------------------------------------------
   Convert a line of raster data from the RAST input to a row of raster
   data for the PNM output.

   'rastLine' is the RAST row.  'xelrow' is where to put the PNM row.  'cols'
   is the number of pixels in the row.

   'colorMapped' means the RAST image is colormapped.  If so, 'colorMap'
   is the color map from the RAST file and 'useIndexForColor' means not
   to use that map but instead to create a PGM row of the colormap
   _indices_.

   'grayscale' means it is a grayscale image; the output is PGM.
-----------------------------------------------------------------------------*/
    unsigned int col;

    for (col = 0; col < cols; ++col) {
        unsigned int const index = rastLine[col];

        if (colorMapped && !useIndexForColor) {
            if (index >= colorMap.length) {
                pm_error("Invalid Sun rasterfile image.  Raster contains "
                         "color index %u, but color map has only %u entries",
                         index, colorMap.length);
            }
            if (grayscale)
                PNM_ASSIGN1(xelrow[col], colorMap.map[CMX_RED][index]);
            else
                PPM_ASSIGN(xelrow[col],
                           colorMap.map[CMX_RED][index],
                           colorMap.map[CMX_GRN][index],
                           colorMap.map[CMX_BLU][index]);
        } else
            PNM_ASSIGN1(xelrow[col], index);
    }
}



static void
convertRowRgb(const unsigned char * const rastLine,
              unsigned int          const cols,
              unsigned int          const depth,
              long                  const rastType,
              bool                  const colorMapped,
              bool                  const useIndexForColor,
              colormap_t            const colorMap,
              xel *                 const xelrow) {

    unsigned int col;
    unsigned int lineCursor;

    for (col = 0, lineCursor = 0; col < cols; ++col) {
        unsigned int r, g, b;

        if (depth == 32)
            ++lineCursor;
        if (rastType == RT_FORMAT_RGB) {
            r = rastLine[lineCursor++];
            g = rastLine[lineCursor++];
            b = rastLine[lineCursor++];
        } else {
            b = rastLine[lineCursor++];;
            g = rastLine[lineCursor++];;
            r = rastLine[lineCursor++];;
        }
        if (colorMapped && !useIndexForColor) {
            if (r >= colorMap.length ||
                g >= colorMap.length ||
                b >= colorMap.length) {
                pm_error("Invalid Sun rasterfile image.  Raster contains "
                         "a pixel with color indices %u, %u, and %u, "
                         "but color map has only %u entries",
                         r, g, b, colorMap.length);
            }
            PPM_ASSIGN(xelrow[col],
                       colorMap.map[CMX_RED][r],
                       colorMap.map[CMX_GRN][g],
                       colorMap.map[CMX_BLU][b]);
        } else
            PPM_ASSIGN(xelrow[col], r, g, b);
    }
}



static void
writePnm(FILE *                 const ofP,
         const struct pixrect * const pixRectP,
         unsigned int           const cols,
         unsigned int           const rows,
         xelval                 const maxval,
         int                    const format,
         unsigned int           const depth,
         long                   const rastType,
         bool                   const grayscale,
         bool                   const colorMapped,
         colormap_t             const colorMap,
         xel                    const zeroXel,
         xel                    const oneXel,
         bool                   const useIndexForColor) {

    struct mpr_data const mprData = *pixRectP->pr_data;

    xel * xelrow;
    unsigned int row;
    unsigned char * lineStart;

    pnm_writepnminit(ofP, cols, rows, maxval, format, 0);

    xelrow = pnm_allocrow(cols);

    reportOutputType(format);

    for (row = 0, lineStart = mprData.md_image;
         row < rows;
         ++row, lineStart += mprData.md_linebytes) {

        switch (depth) {
        case 1:
            convertRowDepth1(lineStart, cols, zeroXel, oneXel, xelrow);
            break;
        case 8:
            convertRowDepth8(lineStart, cols, colorMapped, useIndexForColor,
                             grayscale, colorMap, xelrow);
            break;
        case 24:
        case 32:
            convertRowRgb(lineStart, cols, depth, rastType, colorMapped,
                          useIndexForColor, colorMap, xelrow);
            break;
        default:
            pm_error("Invalid depth value: %u", depth);
        }
        pnm_writepnmrow(ofP, xelrow, cols, maxval, format, 0);
    }

    pnm_freerow(xelrow);
}



static void
dumpHeader(struct rasterfile const header) {

    const char * typeName;

    switch (header.ras_type) {
    case RT_OLD:            typeName = "old";           break;
    case RT_STANDARD:       typeName = "standard";      break;
    case RT_BYTE_ENCODED:   typeName = "byte encoded";  break;
    case RT_FORMAT_RGB:     typeName = "format rgb";    break;
    case RT_FORMAT_TIFF:    typeName = "format_tiff";   break;
    case RT_FORMAT_IFF:     typeName = "format_iff";    break;
    case RT_EXPERIMENTAL:   typeName = "experimental";  break;
    default:                typeName = "???";
    }

    pm_message("type: %s (%lu)", typeName, (unsigned long)header.ras_type);
    pm_message("%luw x %lul x %lud",
               (unsigned long)header.ras_width,
               (unsigned long)header.ras_height,
               (unsigned long)header.ras_depth);
    pm_message("raster length: %lu", (unsigned long)header.ras_length);

    if (header.ras_maplength)
        pm_message("Has color map");
}



static void
dumpHeaderAnalysis(bool         const grayscale,
                   unsigned int const depth,
                   xel          const zero,
                   xel          const one) {

    pm_message("grayscale: %s", grayscale ? "YES" : "NO");

    if (depth == 1) {
        pm_message("Zero color: (%u,%u,%u)",
                   PNM_GETR(zero),
                   PNM_GETG(zero),
                   PNM_GETB(zero));

        pm_message("One color: (%u,%u,%u)",
                   PNM_GETR(one),
                   PNM_GETG(one),
                   PNM_GETB(one));
    }
}



static void
dumpColorMap(colormap_t const colorMap) {

    unsigned int i;
    const char * typeName;

    switch (colorMap.type) {
    case RMT_NONE:      typeName = "NONE";      break;
    case RMT_EQUAL_RGB: typeName = "EQUAL_RGB"; break;
    case RMT_RAW:       typeName = "RAW";       break;
    default:            typeName = "???";
    }

    pm_message("color map type = %s (%u)", typeName, colorMap.type);

    pm_message("color map size = %u", colorMap.length);

    for (i = 0; i < colorMap.length; ++i) {
        pm_message("color %u: (%u, %u, %u)", i,
                   (unsigned char)colorMap.map[CMX_RED][i],
                   (unsigned char)colorMap.map[CMX_GRN][i],
                   (unsigned char)colorMap.map[CMX_BLU][i]);
    }
}



int
main(int argc, const char ** const argv) {

    struct cmdlineInfo cmdline;
    FILE * ifP;
    struct rasterfile header;
    colormap_t colorMap;
    bool grayscale;
    struct pixrect * pr;
    int format;
    xelval maxval;
    xel zero, one;
    int rc;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFileName);

    rc = pr_load_header(ifP, &header);
    if (rc != 0 )
        pm_error("unable to read in rasterfile header");

    if (cmdline.dumpheader)
        dumpHeader(header);

    if (header.ras_maplength != 0) {
        int rc;

        rc = pr_load_colormap(ifP, &header, &colorMap);

        if (rc != 0 )
            pm_error("unable to read colormap from RAST file");

        if (cmdline.dumpcolormap)
            dumpColorMap(colorMap);
    } else {
        colorMap.map[CMX_RED] = NULL;
        colorMap.map[CMX_GRN] = NULL;
        colorMap.map[CMX_BLU] = NULL;
    }
    analyzeImage(header, colorMap, &format, &maxval, &grayscale, &zero, &one);

    if (cmdline.dumpheader)
        dumpHeaderAnalysis(grayscale, header.ras_depth, zero, one);

    pr = pr_load_image(ifP, &header, NULL);
    if (pr == NULL )
        pm_error("unable to read in the image from the rasterfile");

    if (cmdline.index && header.ras_maplength == 0)
        pm_error("You requested to use color map indices as colors (-index), "
                 "but this is not a color mapped image");

    writePnm(stdout, pr, header.ras_width, header.ras_height, maxval, format,
             header.ras_depth, header.ras_type, grayscale,
             header.ras_maplength > 0, colorMap, zero, one, cmdline.index);

    mem_free(pr);

    if (colorMap.map[CMX_RED])
        free(colorMap.map[CMX_RED]);
    if (colorMap.map[CMX_GRN])
        free(colorMap.map[CMX_GRN]);
    if (colorMap.map[CMX_BLU])
        free(colorMap.map[CMX_BLU]);

    pm_close(ifP);
    pm_close(stdout);

    return 0;
}



