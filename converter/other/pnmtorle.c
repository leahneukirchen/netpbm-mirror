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
 * 2022.03.06 revision by Akira F Urushibata
 *            use shhopt instead of scanargs
 *            proper handling of multiple image files with -h
 *
 */
/*-----------------------------------------------------
 * System includes.
 */
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include "pnm.h"
#include "mallocvar.h"
#include "rle.h"
#include "shhopt.h"
#include "pm_c_util.h"


struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inFileName;
    const char * outfile;
    unsigned int verbose;
    unsigned int header;
    unsigned int alpha;
};



static void
parseCommandLine(int argc, const char ** argv,
                 struct CmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   Note that the file spec array we return is stored in the storage that
   was passed to us as the argv array.
-----------------------------------------------------------------------------*/
    optEntry * option_def;
    /* Instructions to pm_optParseOptions3 on how to parse our options. */

    optStruct3 opt;

    unsigned int option_def_index;
    unsigned int outfileSpec;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENTRY */
    OPTENT3(0, "alpha",    OPT_FLAG,   NULL,  &cmdlineP->alpha,     0);
    OPTENT3(0, "header",   OPT_FLAG,   NULL,  &cmdlineP->header,    0);
    OPTENT3(0, "verbose",  OPT_FLAG,   NULL,  &cmdlineP->verbose,   0);
    OPTENT3(0, "outfile",  OPT_STRING, &cmdlineP->outfile,
                                              &outfileSpec,         0);

    opt.opt_table = option_def;
    opt.short_allowed = false;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = false;  /* We have no parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    free(option_def);

    if (argc-1 == 0)
        cmdlineP->inFileName = "-";
    else if (argc-1 != 1) {
        pm_error("Program takes zero or one argument (filename).  You "
                 "specified %d", argc-1);
    }
    else
        cmdlineP->inFileName = argv[1];

    if (!outfileSpec)
        cmdlineP->outfile = "-";
}



static void
readPnmHeader(bool      const verbose,
              bool      const wantAlpha,
              FILE    * const ifP,
              int     * const widthP,
              int     * const heightP,
              gray    * const maxvalP,
              int     * const formatP) {
/*-----------------------------------------------------------------------------
  Read the pnm image file header.
---------------------------------------------------------------------------- */
    int   width;
    int   height;
    gray  maxval;
    int   format;
    const char * type;

    pnm_readpnminit(ifP, &width, &height, &maxval, &format);

    switch (format) {
    case PBM_FORMAT:
        type="plain pbm";
        break;
    case RPBM_FORMAT:
        type="raw pbm";
        break;
    case PGM_FORMAT:
        type="plain pgm";
        break;
    case RPGM_FORMAT:
        type="raw pgm";
        break;
    case PPM_FORMAT:
        type="plain ppm";
        break;
    case RPPM_FORMAT:
        type="raw ppm";
        break;
    }
    if (verbose) {
        pm_message("Image type: %s format", type);
        pm_message("Full image: %dx%d", width, height);
        pm_message("Maxval:     %d", maxval);

        if (wantAlpha)
            pm_message("Computing alpha channel...");
    }
    *widthP  = width;
    *heightP = height;
    *maxvalP = maxval;
    *formatP = format;
}



static void
writeRleHeader(bool         const wantAlpha,
               int          const format,
               unsigned int const width,
               unsigned int const height,
               rle_hdr *    const hdrP) {

    rle_hdr hdr;

    hdr = *hdrP;  /* initial value */

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
    if (wantAlpha) {
        hdr.alpha = 1;
        RLE_SET_BIT(hdr, RLE_ALPHA);
    }
    rle_put_setup(&hdr);

    *hdrP = hdr;
}



static void
writeRleData(bool         const verbose,
             bool         const wantAlpha,
             FILE      *  const ifP,
             rle_hdr   *  const hdrP,
             unsigned int const width,
             unsigned int const height,
             gray         const maxval,
             int          const format) {

    unsigned int scan;
    xel * xelrow;
    rle_pixel *** scanlines;

    MALLOCARRAY(xelrow, width);
    if (xelrow == NULL)
        pm_error("Failed to allocate memory for row of %u pixels", width);

    MALLOCARRAY(scanlines, height);
    if (scanlines == NULL)
        pm_error("Failed to allocate memory for %u scanline pointers", height);

    for (scan = 0; scan < height; ++scan) {
        int rc;
        rc = rle_row_alloc(hdrP, &scanlines[scan]);
        if (rc < 0)
            pm_error("Failed to allocate memory for a scanline");
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
            pnm_readpnmrow(ifP, xelrow, width, maxval, format);
            for (col = 0; col < width; ++col) {
                scanline[RLE_RED][col] = PNM_GET1(xelrow[col]) ? 255 : 0;
                if (wantAlpha)
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
            pnm_readpnmrow(ifP, xelrow, width, maxval, format);
            for (col = 0; col < width; ++col) {
                scanline[RLE_RED][col] = PNM_GET1(xelrow[col]);
                if (wantAlpha)
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

            pnm_readpnmrow(ifP, xelrow, width, maxval, format);

            for (col = 0; col < width; ++col) {
                scanline[RLE_RED][col]   = PPM_GETR(xelrow[col]);
                scanline[RLE_GREEN][col] = PPM_GETG(xelrow[col]);
                scanline[RLE_BLUE][col]  = PPM_GETB(xelrow[col]);
                if (wantAlpha)
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
        rle_putrow(scanlines[scan], width, hdrP);

    for (scan = 0; scan < height; ++scan)
        rle_row_free(hdrP, scanlines[scan]);
    free(scanlines);
    free(xelrow);

    if (verbose)
        pm_message("Done -- write eof to RLE data.");

    rle_puteof(hdrP);
}



static void
skipData(FILE      *  const ifP,
         unsigned int const width,
         unsigned int const height,
         gray         const maxval,
         int          const format) {

    xel * xelrow;
    unsigned int scan;

    MALLOCARRAY(xelrow, width);
    if (xelrow == NULL)
        pm_error("Failed to allocate memory for row of %u pixels", width);

    for (scan=0; scan < height; ++scan)
        pnm_readpnmrow(ifP, xelrow, width, maxval, format);

    free(xelrow);
}



int
main(int argc, const char ** argv) {

    struct CmdlineInfo cmdline;

    FILE   * ifP;
    rle_hdr hdr;
    int  format;
    int  width, height;
    gray maxval;
    bool verbose;
    const char ** argvWork;
    unsigned int i;
    int eof;

    MALLOCARRAY_NOFAIL(argvWork, argc + 1);

    for (i = 0; i < argc; ++i)  /* Make a copy of argv */
        argvWork[i] = argv[i];

    pm_proginit(&argc, argvWork);

    parseCommandLine(argc, argvWork, &cmdline);

    verbose = cmdline.verbose || cmdline.header;

    hdr = *rle_hdr_init(NULL);

    rle_names(&hdr, "pnmtorle", cmdline.outfile, 0);

    /* Open the file. */
    assert(cmdline.inFileName != NULL);
    ifP = pm_openr(cmdline.inFileName);

    hdr.rle_file = rle_open_f(hdr.cmd, cmdline.outfile, "wb");

    for (eof = 0; !eof; ) {
        readPnmHeader(verbose, cmdline.alpha, ifP,
                      &width, &height, &maxval, &format);

        if (cmdline.header) {
            skipData(ifP, width, height, maxval, format);
        } else {
            rle_addhist(argv, NULL, &hdr);
            writeRleHeader(cmdline.alpha, format, width, height, &hdr);
            writeRleData(verbose, cmdline.alpha, ifP, &hdr,
                         width, height, maxval, format);
        }
        pnm_nextimage(ifP, &eof);
    }

    pm_close(ifP);

    return 0;
}



