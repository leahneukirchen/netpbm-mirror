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



int
main(int argc, const char ** const argv) {

    struct cmdlineInfo cmdline;
    FILE * ifP;
    struct rasterfile header;
    colormap_t pr_colormap;
    bool grayscale;
    struct pixrect * pr;
    xel * xelrow;
    int rows, cols, format;
    unsigned int row;
    unsigned int depth;
    xelval maxval;
    xel zero, one;
    int linesize;
    unsigned char * data;
    unsigned char * byteP;
    int rc;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    /* Read in the rasterfile.  First the header. */
    rc = pr_load_header(ifP, &header);
    if (rc != 0 )
        pm_error("unable to read in rasterfile header");

    cols = header.ras_width;
    rows = header.ras_height;
    depth = header.ras_depth;

    if (cols <= 0)
        pm_error( "invalid cols: %d", cols );
    if (rows <= 0)
        pm_error( "invalid rows: %d", rows );

    /* If there is a color map, read it. */
    if (header.ras_maplength != 0) {
        int rc;
        unsigned int i;
        
        rc = pr_load_colormap(ifP, &header, &pr_colormap);
        
        if (rc != 0 )
            pm_error("unable to skip colormap data");

        for (i = 0, grayscale = true; i < header.ras_maplength / 3; ++i) {
            if (pr_colormap.map[0][i] != pr_colormap.map[1][i] ||
                pr_colormap.map[1][i] != pr_colormap.map[2][i]) {
                grayscale = false;
            }
        }
    } else
        grayscale = true;

    /* Check the depth and color map. */
    switch (depth) {
    case 1:
        if (header.ras_maptype == RMT_NONE && header.ras_maplength == 0) {
            maxval = 1;
            format = PBM_TYPE;
            PNM_ASSIGN1(zero, maxval);
            PNM_ASSIGN1(one, 0);
        } else if (header.ras_maptype == RMT_EQUAL_RGB &&
                   header.ras_maplength == 6) {
            if (grayscale) {
                maxval = 255;
                format = PGM_TYPE;
                PNM_ASSIGN1( zero, pr_colormap.map[0][0] );
                PNM_ASSIGN1( one, pr_colormap.map[0][1] );
            } else {
                maxval = 255;
                format = PPM_TYPE;
                PPM_ASSIGN(
                    zero, pr_colormap.map[0][0], pr_colormap.map[1][0],
                    pr_colormap.map[2][0]);
                PPM_ASSIGN(
                    one, pr_colormap.map[0][1], pr_colormap.map[1][1],
                    pr_colormap.map[2][1]);
            }
        } else
            pm_error(
                "this depth-1 rasterfile has a non-standard colormap - "
                "type %ld length %ld",
                header.ras_maptype, header.ras_maplength);
        break;

    case 8:
        if (grayscale) {
            maxval = 255;
            format = PGM_TYPE;
        } else if (header.ras_maptype == RMT_EQUAL_RGB) {
            maxval = 255;
            format = PPM_TYPE;
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
                "this depth-%d rasterfile has a non-standard colormap - "
                "type %ld length %ld",
                depth, header.ras_maptype, header.ras_maplength);
        maxval = 255;
        format = PPM_TYPE;
        break;

    default:
        pm_error("invalid depth: %d.  Can handle only depth 1, 8, 24, or 32.",
                 depth);
    }

    /* Now load the data.  The pixrect returned is a memory pixrect. */
    pr = pr_load_image(ifP, &header, NULL);
    if (pr == NULL )
        pm_error("unable to read in the image from the rasterfile" );

    linesize = ((struct mpr_data*) pr->pr_data)->md_linebytes;
    data = ((struct mpr_data*) pr->pr_data)->md_image;

    /* Now write out the anymap. */
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

    for (row = 0; row < rows; ++row) {
        byteP = data;
        switch (depth) {
        case 1: {
            unsigned int col;
            unsigned char mask;
            for (col = 0, mask = 0x80; col < cols; ++col) {
                if (mask == 0x00) {
                    ++byteP;
                    mask = 0x80;
                }
                xelrow[col] = (*byteP & mask) ? one : zero;
                mask = mask >> 1;
            }
        } break;
        case 8: {
            unsigned int col;
            for (col = 0; col < cols; ++col) {
                if (header.ras_maplength == 0)
                    PNM_ASSIGN1(xelrow[col], *byteP);
                else if (grayscale)
                    PNM_ASSIGN1(xelrow[col], pr_colormap.map[0][*byteP]);
                else
                    PPM_ASSIGN(xelrow[col],
                               pr_colormap.map[0][*byteP],
                               pr_colormap.map[1][*byteP],
                               pr_colormap.map[2][*byteP]);
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
                if (header.ras_type == RT_FORMAT_RGB) {
                    r = *byteP++;
                    g = *byteP++;
                    b = *byteP++;
                } else {
                    b = *byteP++;
                    g = *byteP++;
                    r = *byteP++;
                }
                if (header.ras_maplength == 0)
                    PPM_ASSIGN(xelrow[col], r, g, b);
                else
                    PPM_ASSIGN(xelrow[col],
                               pr_colormap.map[0][r],
                               pr_colormap.map[1][g],
                               pr_colormap.map[2][b]);
            }
        } break;
        default:
            pm_error("Invalid depth value: %u", depth);
        }
        data += linesize;
        pnm_writepnmrow(stdout, xelrow, cols, maxval, format, 0);
    }

    pm_close(ifP);
    pm_close(stdout);

    return 0;
}
