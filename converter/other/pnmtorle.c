/*
 * This is derived from the file of the same name dated June 5, 1995,
 * copied from the Army High Performance Computing Research Center's
 * media-tools.tar.gz package, received from 
 * http://www.arc.umn.edu/gvl-software/media-tools.tar.gz on 2000.04.13.
 *
 * This software is copyrighted as noted below.  It may be freely copied,
 * modified, and redistributed, provided that the copyright notice is
 * preserved on all copies.
 *
 * There is no warranty or other guarantee of fitness for this software,
 * it is provided solely "as is".  Bug reports or fixes may be sent
 * to the author, who may or may not act on them as he desires.
 *
 * You may not include this software in a program or other software product
 * without supplying the source, or without informing the end-user that the
 * source is available for no extra charge.
 *
 * If you modify this software, you should include a notice giving the
 * name of the person performing the modification, the date of modification,
 * and the reason for such modification.
 */
/*
 * pnmtorle - A program which will convert pbmplus (ppm or pgm) images
 *            to Utah's "rle" image format.
 *
 * Author:      Wes Barris (wes@msc.edu)
 *              AHPCRC
 *              Minnesota Supercomputer Center, Inc.
 * Date:        March 30, 1994
 * Copyright (c) Minnesota Supercomputer Center, Inc.
 * 
 * 2000.04.13 adapted for Netpbm by Bryan Henderson.  Quieted compiler 
 *            warnings.
 *
 */
/*-----------------------------------------------------
 * System includes.
 */
#include <string.h>
#include <stdio.h>
#include "pnm.h"
#include "mallocvar.h"
#include "rle.h"

#define VPRINTF if (verbose || header) fprintf

typedef unsigned char U_CHAR;
/*
 * Global variables.
 */
static FILE    *fp;
static rle_hdr hdr;
static int  format;
static int width, height;
static int verbose = 0, header = 0, do_alpha = 0;
static gray    maxval;
/*-----------------------------------------------------------------------------
 *                                        Read the pnm image file header.
 */
static void 
read_pnm_header(void) {

    pnm_readpnminit(fp, &width, &height, &maxval, &format);
    switch (format) {
    case PBM_FORMAT:
        VPRINTF(stderr, "Image type: plain pbm format\n");
        break;
    case RPBM_FORMAT:
        VPRINTF(stderr, "Image type: raw pbm format\n");
        break;
    case PGM_FORMAT:
        VPRINTF(stderr, "Image type: plain pgm format\n");
        break;
    case RPGM_FORMAT:
        VPRINTF(stderr, "Image type: raw pgm format\n");
        break;
    case PPM_FORMAT:
        VPRINTF(stderr, "Image type: plain ppm format\n");
        break;
    case RPPM_FORMAT:
        VPRINTF(stderr, "Image type: raw ppm format\n");
        break;
    }
    VPRINTF(stderr, "Full image: %dx%d\n", width, height);
    VPRINTF(stderr, "Maxval:     %d\n", maxval);
    if (do_alpha)
        VPRINTF(stderr, "Computing alpha channel...\n");
}



static void 
write_rle_header(void) {

    hdr.xmin    = 0;
    hdr.xmax    = width-1;
    hdr.ymin    = 0;
    hdr.ymax    = height-1;
    hdr.background = 0;
    switch (format) {
    case PBM_FORMAT:
    case RPBM_FORMAT:
    case PGM_FORMAT:
    case RPGM_FORMAT:
        hdr.ncolors = 1;
        RLE_SET_BIT(hdr, RLE_RED);
        break;
    case PPM_FORMAT:
    case RPPM_FORMAT:
        hdr.ncolors = 3;
        RLE_SET_BIT(hdr, RLE_RED);
        RLE_SET_BIT(hdr, RLE_GREEN);
        RLE_SET_BIT(hdr, RLE_BLUE);
        break;
    }
    if (do_alpha) {
        hdr.alpha = 1;
        RLE_SET_BIT(hdr, RLE_ALPHA);
    }
    rle_put_setup(&hdr);
}



static void 
write_rle_data(void) {

    unsigned int scan;
    xel * xelrow;
    rle_pixel *** scanlines;

    MALLOCARRAY(xelrow, width);
    MALLOCARRAY(scanlines, height);

    RLE_CHECK_ALLOC(hdr.cmd, scanlines, "scanline pointers");

    for (scan = 0; scan < height; ++scan) {
        int rc;
        rc = rle_row_alloc(&hdr, &scanlines[scan]);
        RLE_CHECK_ALLOC(hdr.cmd, rc >= 0, "pixel memory");
    }
    /* Loop through the pnm files image window, read data and flip vertically.
     */
    switch (format) {
    case PBM_FORMAT:
    case RPBM_FORMAT: {
        unsigned int scan;
        for (scan = 0; scan < height; ++scan) {
            rle_pixel ** const scanline = scanlines[height - scan - 1];
            unsigned int col;
            pnm_readpnmrow(fp, xelrow, width, maxval, format);
            for (col = 0; col < width; ++col) {
                scanline[RLE_RED][col] = PNM_GET1(xelrow[col]) ? 255 : 0;
                if (do_alpha)
                    scanline[RLE_ALPHA][col] = scanline[RLE_RED][col];
            }
        }
    } break;
    case PGM_FORMAT:
    case RPGM_FORMAT: {
        unsigned int scan;
        for (scan = 0; scan < height; ++scan) {
            rle_pixel ** const scanline = scanlines[height - scan - 1];
            unsigned int col;
            pnm_readpnmrow(fp, xelrow, width, maxval, format);
            for (col = 0; col < width; ++col) {
                scanline[RLE_RED][col] = PNM_GET1(xelrow[col]);
                if (do_alpha)
                    scanline[RLE_ALPHA][col] =
                        scanline[RLE_RED][col] ? 255 : 0;
            }
        }
    } break;
    case PPM_FORMAT:
    case RPPM_FORMAT: {
        unsigned int scan;
        for (scan = 0; scan < height; scan++) {
            rle_pixel ** const scanline = scanlines[height - scan - 1];
            unsigned int col;
            pnm_readpnmrow(fp, xelrow, width, maxval, format);
            for (col = 0; col < width; ++col) {
                scanline[RLE_RED][col]   = PPM_GETR(xelrow[col]);
                scanline[RLE_GREEN][col] = PPM_GETG(xelrow[col]);
                scanline[RLE_BLUE][col]  = PPM_GETB(xelrow[col]);
                if (do_alpha)
                    scanline[RLE_ALPHA][col] =
                        (scanline[RLE_RED][col] ||
                         scanline[RLE_GREEN][col] ||
                         scanline[RLE_BLUE][col] ? 255 : 0);
            }
        }
        } break;
    }
    /* Write out data in URT order (bottom to top). */
    for (scan = 0; scan < height; ++scan)
        rle_putrow(scanlines[scan], width, &hdr);

    for (scan = 0; scan < height; ++scan)
        rle_row_free(&hdr, scanlines[scan]);
    free(scanlines);
    free(xelrow);

    VPRINTF(stderr, "Done -- write eof to RLE data.\n");
    rle_puteof(&hdr);
}



static void
skip_data(FILE      * const fp,
          int         const width,
          int         const height,
          gray        const maxval,
          int         const format) {

    xel * xelrow;
    unsigned int scan;

    MALLOCARRAY(xelrow, width);
    if (xelrow == NULL)
        pm_error("Failed to allocate memory for row of %u pixels", width);

    for(scan=0; scan < height; ++scan)
        pnm_readpnmrow(fp, xelrow, width, maxval, format);

    free(xelrow);
}



int
main(int argc, char **  argv) {

    const char * pnmname;
    const char * outname;
    int oflag;
    int eof;

    pnm_init(&argc, argv);

    pnmname = NULL;  /* initial value */
    outname = NULL;  /* initial value */

    /* Get those options. */
    if (!scanargs(argc,argv,
                  "% v%- h%- a%- o%-outfile!s pnmfile%s\n(\
\tConvert a PNM file to URT RLE format.\n\
\t-a\tFake an alpha channel.  Alpha=0 when input=0, 255 otherwise.\n\
\t-h\tPrint header of PNM file and exit.\n\
\t-v\tVerbose mode.)",
                  &verbose,
                  &header,
                  &do_alpha,
                  &oflag, &outname,
                  &pnmname))
        exit(-1);

    hdr = *rle_hdr_init(NULL);
    rle_names(&hdr, cmd_name(argv), outname, 0);

    /* Open the file. */
    if (pnmname == NULL) {
        fp = pm_openr("-");
    } else {
        fp = pm_openr(pnmname);
    }

    hdr.rle_file = rle_open_f( hdr.cmd, outname, "wb" );

    for (eof = 0; !eof; ) {
        read_pnm_header();

        if (header)
            skip_data(fp, width, height, maxval, format);
        else {
            rle_addhist(argv, NULL, &hdr);
            write_rle_header();
            write_rle_data();
        }
        pnm_nextimage(fp, &eof);
    }
    pm_close(fp);

    return 0;
}
