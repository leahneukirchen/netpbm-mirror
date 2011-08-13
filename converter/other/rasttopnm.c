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
    const char *inputFileName;
};



static void
parseCommandLine(int argc, const char ** argv,
                 struct cmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   Note that the file spec array we return is stored in the storage that
   was passed to us as the argv array.
-----------------------------------------------------------------------------*/
    optEntry * option_def;
        /* Instructions to OptParseOptions2 on how to parse our options.
         */
    optStruct3 opt;

    unsigned int option_def_index;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
 
    opt.opt_table = option_def;
    opt.short_allowed = false;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = false;  /* We have no parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (argc-1 == 0) 
        cmdlineP->inputFileName = "-";
    else if (argc-1 != 1)
        pm_error("Program takes zero or one argument (filename).  You "
                 "specified %d", argc-1);
    else
        cmdlineP->inputFileName = argv[1];
}



static bool
colorMapIsGrayscale(colormap_t   const colorMap,
                    unsigned int const mapLength) {

    unsigned int i;
    bool grayscale;

    for (i = 0, grayscale = true; i < mapLength / 3; ++i) {
        if (colorMap.map[0][i] != colorMap.map[1][i] ||
            colorMap.map[1][i] != colorMap.map[2][i]) {
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
        header.ras_maplength == 0 ||
        colorMapIsGrayscale(colorMap, header.ras_maplength);

    switch (header.ras_depth) {
    case 1:
        if (header.ras_maptype == RMT_NONE && header.ras_maplength == 0) {
            *maxvalP = 1;
            *formatP = PBM_TYPE;
            PNM_ASSIGN1(*zeroP, 1);
            PNM_ASSIGN1(*oneP, 0);
        } else if (header.ras_maptype == RMT_EQUAL_RGB &&
                   header.ras_maplength == 6) {
            if (grayscale) {
                *maxvalP = 255;
                *formatP = PGM_TYPE;
                PNM_ASSIGN1(*zeroP, colorMap.map[0][0]);
                PNM_ASSIGN1(*oneP, colorMap.map[0][1]);
            } else {
                *maxvalP = 255;
                *formatP = PPM_TYPE;
                PPM_ASSIGN(
                    *zeroP, colorMap.map[0][0], colorMap.map[1][0],
                    colorMap.map[2][0]);
                PPM_ASSIGN(
                    *oneP, colorMap.map[0][1], colorMap.map[1][1],
                    colorMap.map[2][1]);
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
        else if (header.ras_maptype == RMT_RAW || header.ras_maplength == 768);
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
writePnm(FILE *                 const ofP,
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
         const struct pixrect * const pixRectP) {

    unsigned int const lineSize =
        ((struct mpr_data*) pixRectP->pr_data)->md_linebytes;
    unsigned char * const data =
        ((struct mpr_data*) pixRectP->pr_data)->md_image;

    xel * xelrow;
    unsigned int row;
    unsigned char * lineStart;

    pnm_writepnminit(stdout, cols, rows, maxval, format, 0);

    xelrow = pnm_allocrow(cols);
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

    for (row = 0, lineStart = data; row < rows; ++row, lineStart += lineSize) {
        unsigned char * byteP;

        byteP = lineStart; /* initial value */

        switch (depth) {
        case 1: {
            unsigned int col;
            unsigned char mask;
            for (col = 0, mask = 0x80; col < cols; ++col) {
                if (mask == 0x00) {
                    ++byteP;
                    mask = 0x80;
                }
                xelrow[col] = (*byteP & mask) ? oneXel : zeroXel;
                mask = mask >> 1;
            }
        } break;
        case 8: {
            unsigned int col;
            for (col = 0; col < cols; ++col) {
                if (!colorMapped)
                    PNM_ASSIGN1(xelrow[col], *byteP);
                else if (grayscale)
                    PNM_ASSIGN1(xelrow[col], colorMap.map[0][*byteP]);
                else
                    PPM_ASSIGN(xelrow[col],
                               colorMap.map[0][*byteP],
                               colorMap.map[1][*byteP],
                               colorMap.map[2][*byteP]);
                ++byteP;
            }
        } break;
        case 24:
        case 32: {
            unsigned int col;
            for (col = 0; col < cols; ++col) {
                xelval r, g, b;

                if (depth == 32)
                    ++byteP;
                if (rastType == RT_FORMAT_RGB) {
                    r = *byteP++;
                    g = *byteP++;
                    b = *byteP++;
                } else {
                    b = *byteP++;
                    g = *byteP++;
                    r = *byteP++;
                }
                if (!colorMapped)
                    PPM_ASSIGN(xelrow[col], r, g, b);
                else
                    PPM_ASSIGN(xelrow[col],
                               colorMap.map[0][r],
                               colorMap.map[1][g],
                               colorMap.map[2][b]);
            }
        } break;
        default:
            pm_error("Invalid depth value: %u", depth);
        }
        pnm_writepnmrow(stdout, xelrow, cols, maxval, format, 0);
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

    if (header.ras_maplength != 0) {
        int rc;
        
        rc = pr_load_colormap(ifP, &header, &colorMap);
        
        if (rc != 0 )
            pm_error("unable to read colormap from RAST file");
    }

    analyzeImage(header, colorMap, &format, &maxval, &grayscale, &zero, &one);

    pr = pr_load_image(ifP, &header, NULL);
    if (pr == NULL )
        pm_error("unable to read in the image from the rasterfile" );

    writePnm(stdout, header.ras_width, header.ras_height, maxval, format,
             header.ras_depth, header.ras_type, grayscale, 
             header.ras_maplength > 0, colorMap, zero, one, pr);

    pm_close(ifP);
    pm_close(stdout);

    return 0;
}
